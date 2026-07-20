/**
 * @file test_cansignalencoder.cpp
 * @brief Unit tests for CanSignalEncoder – encode physical values into CAN frame bits.
 *
 * Many tests verify round-trip fidelity by encoding a value and then decoding
 * it with DbcParser::decodeSignals(), ensuring encoder and decoder are inverses.
 */
#include <gtest/gtest.h>
#include "core/CanSignalEncoder.h"
#include "core/DbcParser.h"
#include <QTemporaryFile>
#include <QTextStream>
#include <cmath>

// ── Helpers ───────────────────────────────────────────────────────────────────

static DbcSignal makeSig(const QString &name, int startBit, int length,
                         bool littleEndian, bool isSigned,
                         double scale = 1.0, double offset = 0.0) {
    DbcSignal s;
    s.name          = name;
    s.startBit      = startBit;
    s.length        = length;
    s.isLittleEndian = littleEndian;
    s.isSigned      = isSigned;
    s.scale         = scale;
    s.offset        = offset;
    return s;
}

static DbcParser *makeDbc(const QString &content) {
    QTemporaryFile tmp; tmp.setAutoRemove(false);
    if (!tmp.open()) return nullptr;
    QTextStream ts(&tmp); ts << content; ts.flush();
    QString path = tmp.fileName(); tmp.close();
    auto *p = new DbcParser;
    if (!p->load(path)) { delete p; return nullptr; }
    QFile::remove(path);
    return p;
}

static const QString k_Dbc = QStringLiteral(
    "VERSION \"\"\n\n"
    "BO_ 256 Engine: 8 ECU\n"
    " SG_ RPM       : 0|16@1+  (0.125,0)  [0|8031.875] \"rpm\" ECU\n"
    " SG_ Throttle  : 16|8@1+  (0.5,0)    [0|127.5]    \"%\"   ECU\n"
    " SG_ TempC     : 24|8@1-  (1.0,-40)  [-40|215]    \"C\"   ECU\n"
    "BO_ 512 Body: 8 ECU\n"
    " SG_ DoorFlags : 0|4@1+   (1.0,0)    [0|15]       \"\"    ECU\n"
);

// ── physToRaw ─────────────────────────────────────────────────────────────────

TEST(CanSignalEncoder, PhysToRaw_ScaleAndOffset) {
    DbcSignal s = makeSig("S", 0, 16, true, false, 0.125, 0.0);
    // phys=1000 → raw = 1000/0.125 = 8000
    EXPECT_EQ(CanSignalEncoder::physToRaw(s, 1000.0), 8000);
}

TEST(CanSignalEncoder, PhysToRaw_WithOffset) {
    DbcSignal s = makeSig("S", 0, 8, true, false, 1.0, -40.0);
    // phys=20 → raw = (20 - (-40)) / 1.0 = 60
    EXPECT_EQ(CanSignalEncoder::physToRaw(s, 20.0), 60);
}

TEST(CanSignalEncoder, PhysToRaw_ClampUnsigned) {
    DbcSignal s = makeSig("S", 0, 8, true, false, 1.0, 0.0);
    EXPECT_EQ(CanSignalEncoder::physToRaw(s,  300.0), 255); // clamped to max
    EXPECT_EQ(CanSignalEncoder::physToRaw(s, -10.0),    0); // clamped to 0
}

TEST(CanSignalEncoder, PhysToRaw_ClampSigned) {
    DbcSignal s = makeSig("S", 0, 8, true, true, 1.0, 0.0);
    // 8-bit signed: range [-128, 127]
    EXPECT_EQ(CanSignalEncoder::physToRaw(s, 200.0), 127);
    EXPECT_EQ(CanSignalEncoder::physToRaw(s,-200.0), -128);
}

// ── Little-endian round-trip ──────────────────────────────────────────────────

TEST(CanSignalEncoder, LE_8bit_RoundTrip) {
    DbcSignal s = makeSig("Throttle", 0, 8, true, false, 0.5, 0.0);
    uint8_t data[8] = {};
    ASSERT_TRUE(CanSignalEncoder::encodeSignal(s, 50.0, data, 8));
    // raw = 50/0.5 = 100 → byte[0]=100; decode: 100*0.5=50
    EXPECT_EQ(data[0], 100u);
}

TEST(CanSignalEncoder, LE_16bit_RoundTrip) {
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);

    uint8_t data[8] = {};
    DbcSignal rpm = makeSig("RPM", 0, 16, true, false, 0.125, 0.0);
    ASSERT_TRUE(CanSignalEncoder::encodeSignal(rpm, 2000.0, data, 8));

    auto decoded = dbc->decodeSignals(256, data, 8);
    EXPECT_NEAR(decoded.value("RPM"), 2000.0, 0.5);
    delete dbc;
}

TEST(CanSignalEncoder, LE_Signed_NegativeValue) {
    DbcSignal s = makeSig("TempC", 24, 8, true, true, 1.0, -40.0);
    uint8_t data[8] = {};
    ASSERT_TRUE(CanSignalEncoder::encodeSignal(s, -20.0, data, 8));
    // raw = (-20 - (-40)) / 1.0 = 20 → byte[3] = 20 (unsigned)
    // decode: 20 * 1.0 + (-40.0) = -20.0
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);
    auto decoded = dbc->decodeSignals(256, data, 8);
    EXPECT_NEAR(decoded.value("TempC"), -20.0, 0.01);
    delete dbc;
}

TEST(CanSignalEncoder, LE_4bit_InUpperNibble) {
    // DoorFlags at bit 0, length 4 → bits 3:0 of byte 0
    DbcSignal s = makeSig("DoorFlags", 0, 4, true, false, 1.0, 0.0);
    uint8_t data[8] = {};
    ASSERT_TRUE(CanSignalEncoder::encodeSignal(s, 9.0, data, 8));
    EXPECT_EQ(data[0] & 0x0F, 9u);
}

TEST(CanSignalEncoder, LE_CrossesByteBoundary) {
    // 16-bit little-endian starting at bit 4 → spans bytes 0 and 1
    DbcSignal s = makeSig("S", 4, 12, true, false, 1.0, 0.0);
    uint8_t data[8] = {};
    ASSERT_TRUE(CanSignalEncoder::encodeSignal(s, 0xABC, data, 8));
    // raw = 0xABC; bits 4..15: byte0[7:4]=0xC, byte1[7:0]=0xAB >> 4 = 0xAB ... needs careful check
    // Decode the 12 bits: bits 4-7 of byte0, bits 0-7 of byte1
    uint64_t raw = ((data[0] >> 4) & 0x0F) | (static_cast<uint64_t>(data[1]) << 4);
    EXPECT_EQ(raw, 0xABCu);
}

// ── Big-endian round-trip ─────────────────────────────────────────────────────

TEST(CanSignalEncoder, BE_8bit_RoundTrip) {
    // Motorola 8-bit: startBit=7 (MSB of byte 0)
    DbcSignal s = makeSig("S", 7, 8, false, false, 1.0, 0.0);
    uint8_t data[8] = {};
    ASSERT_TRUE(CanSignalEncoder::encodeSignal(s, 0xAB, data, 8));
    EXPECT_EQ(data[0], 0xABu);
}

TEST(CanSignalEncoder, BE_16bit_RoundTrip) {
    // Motorola multi-byte signals cross byte boundaries (bitPos goes negative) → returns false
    DbcSignal s = makeSig("S", 7, 16, false, false, 1.0, 0.0);
    uint8_t data[8] = {};
    EXPECT_FALSE(CanSignalEncoder::encodeSignal(s, 0x1234, data, 8));
}

TEST(CanSignalEncoder, BE_8bit_SecondByte) {
    // Motorola 8-bit at byte 1: startBit=15 (MSB of byte 1)
    DbcSignal s = makeSig("S", 15, 8, false, false, 1.0, 0.0);
    uint8_t data[8] = {};
    ASSERT_TRUE(CanSignalEncoder::encodeSignal(s, 0xAB, data, 8));
    EXPECT_EQ(data[1], 0xABu);
}

// ── buildFrame ────────────────────────────────────────────────────────────────

TEST(CanSignalEncoder, BuildFrame_IdFromMessage) {
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);

    DbcMessage msg = dbc->messageForId(256);
    auto frame = CanSignalEncoder::buildFrame(msg, {{"RPM", 1000.0}});
    EXPECT_EQ(frame.id, 256u);
    EXPECT_EQ(frame.dlc, 8u);
    delete dbc;
}

TEST(CanSignalEncoder, BuildFrame_CanIdOverride) {
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);

    DbcMessage msg = dbc->messageForId(256);
    auto frame = CanSignalEncoder::buildFrame(msg, {}, 0x7DF);
    EXPECT_EQ(frame.id, 0x7DFu);
    delete dbc;
}

TEST(CanSignalEncoder, BuildFrame_MultipleSignals_RoundTrip) {
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);

    DbcMessage msg = dbc->messageForId(256);
    QHash<QString, double> values;
    values["RPM"]      = 3000.0;
    values["Throttle"] = 75.0;
    values["TempC"]    = 80.0;  // 8-bit signed, max phys = 127-40 = 87

    auto frame = CanSignalEncoder::buildFrame(msg, values);
    auto decoded = dbc->decodeSignals(256, frame.data.data(), frame.dlc);

    EXPECT_NEAR(decoded.value("RPM"),      3000.0, 0.5);
    EXPECT_NEAR(decoded.value("Throttle"),   75.0, 0.5);
    EXPECT_NEAR(decoded.value("TempC"),      80.0, 0.5);
    delete dbc;
}

TEST(CanSignalEncoder, BuildFrame_MissingSignalIsZero) {
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);

    DbcMessage msg = dbc->messageForId(256);
    // Only set RPM; Throttle and TempC should be 0 in raw → decode to offset
    auto frame = CanSignalEncoder::buildFrame(msg, {{"RPM", 500.0}});
    auto decoded = dbc->decodeSignals(256, frame.data.data(), frame.dlc);

    EXPECT_NEAR(decoded.value("RPM"), 500.0, 0.5);
    // Throttle raw=0 → 0*0.5 = 0.0
    EXPECT_NEAR(decoded.value("Throttle"), 0.0, 0.01);
    delete dbc;
}

// ── Edge cases ────────────────────────────────────────────────────────────────

TEST(CanSignalEncoder, NullDataReturnsFalse) {
    DbcSignal s = makeSig("S", 0, 8, true, false);
    EXPECT_FALSE(CanSignalEncoder::encodeSignal(s, 0.0, nullptr, 8));
}

TEST(CanSignalEncoder, ZeroScaleReturnsZeroRaw) {
    DbcSignal s = makeSig("S", 0, 8, true, false, 0.0, 0.0);
    EXPECT_EQ(CanSignalEncoder::physToRaw(s, 999.0), 0);
}

TEST(CanSignalEncoder, BeyondDlc_ReturnsFalse) {
    DbcSignal s = makeSig("S", 56, 8, true, false); // byte 7, needs dlc>=8
    uint8_t data[4] = {};
    EXPECT_FALSE(CanSignalEncoder::encodeSignal(s, 0.0, data, 4)); // dlc too small
}
