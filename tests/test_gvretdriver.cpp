#include <gtest/gtest.h>
#include "core/GvretDriver.h"

// ── Helpers ───────────────────────────────────────────────────────────────────

static constexpr uint8_t MAGIC    = 0xF1;
static constexpr uint8_t CMD_OUT  = 0x00;  // ESP→PC
static constexpr uint8_t CMD_IN   = 0x01;  // PC→ESP
static constexpr uint8_t CMD_SETUP= 0x06;

// Build a raw GVRET CMD_FRAME_OUT packet (ESP→PC direction, what we parse)
static QByteArray makeOutPacket(uint32_t id, bool ext, uint8_t dlc,
                                std::initializer_list<uint8_t> data,
                                uint32_t ts = 0)
{
    if (ext) id |= 0x80000000UL;
    QByteArray b;
    b.append(static_cast<char>(MAGIC));
    b.append(static_cast<char>(CMD_OUT));
    // timestamp 4 bytes LE
    b.append(static_cast<char>( ts        & 0xFF));
    b.append(static_cast<char>((ts >>  8) & 0xFF));
    b.append(static_cast<char>((ts >> 16) & 0xFF));
    b.append(static_cast<char>((ts >> 24) & 0xFF));
    // id 4 bytes LE
    b.append(static_cast<char>( id        & 0xFF));
    b.append(static_cast<char>((id >>  8) & 0xFF));
    b.append(static_cast<char>((id >> 16) & 0xFF));
    b.append(static_cast<char>((id >> 24) & 0xFF));
    // len_bus: bus=0, dlc in low nibble
    b.append(static_cast<char>(dlc & 0x0F));
    for (uint8_t byte : data)
        b.append(static_cast<char>(byte));
    return b;
}

// ── baudStringToBps ───────────────────────────────────────────────────────────

TEST(GvretDriverTest, BaudString_250K) {
    EXPECT_EQ(GvretDriver::baudStringToBps("250K"), 250000u);
}

TEST(GvretDriverTest, BaudString_500K) {
    EXPECT_EQ(GvretDriver::baudStringToBps("500K"), 500000u);
}

TEST(GvretDriverTest, BaudString_1M) {
    EXPECT_EQ(GvretDriver::baudStringToBps("1M"), 1000000u);
}

TEST(GvretDriverTest, BaudString_125K) {
    EXPECT_EQ(GvretDriver::baudStringToBps("125K"), 125000u);
}

TEST(GvretDriverTest, BaudString_10K) {
    EXPECT_EQ(GvretDriver::baudStringToBps("10K"), 10000u);
}

TEST(GvretDriverTest, BaudString_Unknown_ReturnsZero) {
    EXPECT_EQ(GvretDriver::baudStringToBps("9600"), 0u);
    EXPECT_EQ(GvretDriver::baudStringToBps(""), 0u);
    EXPECT_EQ(GvretDriver::baudStringToBps("GARBAGE"), 0u);
}

// ── buildSetupBusPacket ───────────────────────────────────────────────────────

TEST(GvretDriverTest, SetupBusPacket_Length) {
    auto pkt = GvretDriver::buildSetupBusPacket(250000);
    EXPECT_EQ(pkt.size(), 8);
}

TEST(GvretDriverTest, SetupBusPacket_MagicAndCmd) {
    auto pkt = GvretDriver::buildSetupBusPacket(250000);
    EXPECT_EQ(static_cast<uint8_t>(pkt[0]), MAGIC);
    EXPECT_EQ(static_cast<uint8_t>(pkt[1]), CMD_SETUP);
}

TEST(GvretDriverTest, SetupBusPacket_BpsLittleEndian_250K) {
    auto pkt = GvretDriver::buildSetupBusPacket(250000);  // 0x0003D090
    EXPECT_EQ(static_cast<uint8_t>(pkt[2]), 0x90u);
    EXPECT_EQ(static_cast<uint8_t>(pkt[3]), 0xD0u);
    EXPECT_EQ(static_cast<uint8_t>(pkt[4]), 0x03u);
    EXPECT_EQ(static_cast<uint8_t>(pkt[5]), 0x00u);
}

TEST(GvretDriverTest, SetupBusPacket_BpsLittleEndian_1M) {
    auto pkt = GvretDriver::buildSetupBusPacket(1000000);  // 0x000F4240
    EXPECT_EQ(static_cast<uint8_t>(pkt[2]), 0x40u);
    EXPECT_EQ(static_cast<uint8_t>(pkt[3]), 0x42u);
    EXPECT_EQ(static_cast<uint8_t>(pkt[4]), 0x0Fu);
    EXPECT_EQ(static_cast<uint8_t>(pkt[5]), 0x00u);
}

TEST(GvretDriverTest, SetupBusPacket_BusAndFlagsZero) {
    auto pkt = GvretDriver::buildSetupBusPacket(500000);
    EXPECT_EQ(static_cast<uint8_t>(pkt[6]), 0x00u);  // busNum
    EXPECT_EQ(static_cast<uint8_t>(pkt[7]), 0x00u);  // flags
}

// ── buildGvretTxPacket ────────────────────────────────────────────────────────

TEST(GvretDriverTest, TxPacket_StandardId_Structure) {
    CanFrame f{};
    f.id       = 0x123;
    f.extended = false;
    f.dlc      = 3;
    f.data[0]  = 0xAA;
    f.data[1]  = 0xBB;
    f.data[2]  = 0xCC;

    auto pkt = GvretDriver::buildGvretTxPacket(f);
    ASSERT_EQ(pkt.size(), 7 + 3);

    EXPECT_EQ(static_cast<uint8_t>(pkt[0]), MAGIC);
    EXPECT_EQ(static_cast<uint8_t>(pkt[1]), CMD_IN);

    // ID little-endian, bit31 clear for standard
    uint32_t id = static_cast<uint8_t>(pkt[2])
               | (static_cast<uint32_t>(static_cast<uint8_t>(pkt[3])) <<  8)
               | (static_cast<uint32_t>(static_cast<uint8_t>(pkt[4])) << 16)
               | (static_cast<uint32_t>(static_cast<uint8_t>(pkt[5])) << 24);
    EXPECT_EQ(id & 0x80000000UL, 0u);   // not extended
    EXPECT_EQ(id & 0x7FFu, 0x123u);

    EXPECT_EQ(static_cast<uint8_t>(pkt[6]), 3u);  // len_bus = dlc (bus nibble = 0)
    EXPECT_EQ(static_cast<uint8_t>(pkt[7]), 0xAAu);
    EXPECT_EQ(static_cast<uint8_t>(pkt[8]), 0xBBu);
    EXPECT_EQ(static_cast<uint8_t>(pkt[9]), 0xCCu);
}

TEST(GvretDriverTest, TxPacket_ExtendedId_Bit31Set) {
    CanFrame f{};
    f.id       = 0x1FFFFFFF;
    f.extended = true;
    f.dlc      = 0;

    auto pkt = GvretDriver::buildGvretTxPacket(f);
    ASSERT_EQ(pkt.size(), 7);

    uint32_t id = static_cast<uint8_t>(pkt[2])
               | (static_cast<uint32_t>(static_cast<uint8_t>(pkt[3])) <<  8)
               | (static_cast<uint32_t>(static_cast<uint8_t>(pkt[4])) << 16)
               | (static_cast<uint32_t>(static_cast<uint8_t>(pkt[5])) << 24);
    EXPECT_NE(id & 0x80000000UL, 0u);   // extended bit set
    EXPECT_EQ(id & 0x1FFFFFFFu, 0x1FFFFFFFu);
}

TEST(GvretDriverTest, TxPacket_DlcClamped_To8) {
    CanFrame f{};
    f.id       = 0x100;
    f.extended = false;
    f.dlc      = 12;  // > 8 — should clamp

    auto pkt = GvretDriver::buildGvretTxPacket(f);
    EXPECT_EQ(pkt.size(), 7 + 8);  // clamped to 8
    EXPECT_EQ(static_cast<uint8_t>(pkt[6]), 8u);
}

TEST(GvretDriverTest, TxPacket_ZeroDlc_NoDataBytes) {
    CanFrame f{};
    f.id       = 0x7FF;
    f.extended = false;
    f.dlc      = 0;

    auto pkt = GvretDriver::buildGvretTxPacket(f);
    EXPECT_EQ(pkt.size(), 7);
    EXPECT_EQ(static_cast<uint8_t>(pkt[6]), 0u);
}

// ── parseGvretFrame ───────────────────────────────────────────────────────────

TEST(GvretDriverTest, Parse_StandardFrame) {
    QByteArray buf = makeOutPacket(0x123, false, 3, {0xAA, 0xBB, 0xCC});
    CanFrame f = GvretDriver::parseGvretFrame(buf);

    EXPECT_EQ(f.id,  0x123u);
    EXPECT_FALSE(f.extended);
    EXPECT_EQ(f.dlc, 3u);
    EXPECT_EQ(f.data[0], 0xAAu);
    EXPECT_EQ(f.data[1], 0xBBu);
    EXPECT_EQ(f.data[2], 0xCCu);
    EXPECT_TRUE(buf.isEmpty());
}

TEST(GvretDriverTest, Parse_ExtendedFrame) {
    QByteArray buf = makeOutPacket(0x18FF1234, true, 8,
                                   {1,2,3,4,5,6,7,8});
    CanFrame f = GvretDriver::parseGvretFrame(buf);

    EXPECT_EQ(f.id, 0x18FF1234u);
    EXPECT_TRUE(f.extended);
    EXPECT_EQ(f.dlc, 8u);
    EXPECT_EQ(f.data[7], 8u);
}

TEST(GvretDriverTest, Parse_EmptyBuffer_ReturnsDefault) {
    QByteArray buf;
    CanFrame f = GvretDriver::parseGvretFrame(buf);
    EXPECT_EQ(f.id,  0u);
    EXPECT_EQ(f.dlc, 0u);
}

TEST(GvretDriverTest, Parse_GarbagePrefix_SkippedToMagic) {
    QByteArray buf;
    buf.append('\x00'); buf.append('\xFF'); buf.append('\x42');
    buf.append(makeOutPacket(0x100, false, 1, {0xDE}));

    CanFrame f = GvretDriver::parseGvretFrame(buf);
    EXPECT_EQ(f.id, 0x100u);
    EXPECT_EQ(f.dlc, 1u);
    EXPECT_EQ(f.data[0], 0xDEu);
}

TEST(GvretDriverTest, Parse_IncompleteHeader_RetainsBuffer) {
    // Only 5 bytes — not enough for a full packet (min 11)
    QByteArray buf;
    buf.append(static_cast<char>(MAGIC));
    buf.append(static_cast<char>(CMD_OUT));
    buf.append('\x00'); buf.append('\x00'); buf.append('\x00');

    CanFrame f = GvretDriver::parseGvretFrame(buf);
    EXPECT_EQ(f.id,  0u);
    EXPECT_EQ(f.dlc, 0u);
    // Buffer should still contain the partial packet
    EXPECT_GE(buf.size(), 2);
}

TEST(GvretDriverTest, Parse_IncompleteData_RetainsBuffer) {
    // Full header (11 bytes) but dlc=4 and only 2 data bytes follow
    QByteArray buf = makeOutPacket(0x200, false, 4, {0x01, 0x02});
    // makeOutPacket adds 2 bytes of data even though dlc=4 — truncate last 2
    // actually makeOutPacket uses the initializer_list — it creates dlc field=4
    // but only appends 2 bytes, so the packet is short: 11+2=13, needs 11+4=15
    CanFrame f = GvretDriver::parseGvretFrame(buf);
    EXPECT_EQ(f.id,  0u);
    EXPECT_EQ(f.dlc, 0u);
    EXPECT_GE(buf.size(), 2);
}

TEST(GvretDriverTest, Parse_BackToBackFrames) {
    QByteArray buf;
    buf.append(makeOutPacket(0x111, false, 1, {0x01}));
    buf.append(makeOutPacket(0x222, false, 2, {0x02, 0x03}));

    CanFrame f1 = GvretDriver::parseGvretFrame(buf);
    EXPECT_EQ(f1.id, 0x111u);
    EXPECT_EQ(f1.dlc, 1u);

    CanFrame f2 = GvretDriver::parseGvretFrame(buf);
    EXPECT_EQ(f2.id, 0x222u);
    EXPECT_EQ(f2.dlc, 2u);

    EXPECT_TRUE(buf.isEmpty());
}

TEST(GvretDriverTest, Parse_ZeroIdZeroDlc_Skipped) {
    // A frame with id=0 and dlc=0 should be skipped (treated as "no data")
    QByteArray buf;
    buf.append(makeOutPacket(0x000, false, 0, {}));
    buf.append(makeOutPacket(0x123, false, 1, {0xAB}));

    CanFrame f = GvretDriver::parseGvretFrame(buf);
    // Should return the 0x123 frame (skipped the zero frame)
    EXPECT_EQ(f.id, 0x123u);
    EXPECT_EQ(f.dlc, 1u);
}

TEST(GvretDriverTest, Parse_SplitAcrossTwoReads) {
    // Simulate receiving the packet in two chunks
    QByteArray full = makeOutPacket(0x456, false, 2, {0xDE, 0xAD});
    QByteArray firstChunk  = full.left(8);   // partial
    QByteArray secondChunk = full.mid(8);    // rest

    // First chunk: incomplete — should return empty frame, keep buffer intact
    QByteArray buf = firstChunk;
    CanFrame f1 = GvretDriver::parseGvretFrame(buf);
    EXPECT_EQ(f1.id,  0u);
    EXPECT_EQ(f1.dlc, 0u);

    // Second chunk: now complete
    buf.append(secondChunk);
    CanFrame f2 = GvretDriver::parseGvretFrame(buf);
    EXPECT_EQ(f2.id, 0x456u);
    EXPECT_EQ(f2.dlc, 2u);
    EXPECT_EQ(f2.data[0], 0xDEu);
    EXPECT_EQ(f2.data[1], 0xADu);
}

TEST(GvretDriverTest, Parse_DlcOver8_Clamped) {
    // len_bus with high nibble bits and dlc nibble = 9 — clamp to 8
    QByteArray buf;
    buf.append(static_cast<char>(MAGIC));
    buf.append(static_cast<char>(CMD_OUT));
    for (int i = 0; i < 4; ++i) buf.append('\x00');  // ts
    for (int i = 0; i < 4; ++i) buf.append('\x00');  // id
    buf.append(static_cast<char>(0x09));              // dlc=9 in low nibble
    for (int i = 0; i < 8; ++i) buf.append(static_cast<char>(i)); // 8 data bytes

    CanFrame f = GvretDriver::parseGvretFrame(buf);
    EXPECT_EQ(f.dlc, 8u);  // clamped
}

// ── Round-trip: buildGvretTxPacket / parseGvretFrame ─────────────────────────

TEST(GvretDriverTest, RoundTrip_TxThenParse_StandardId) {
    // Note: TX uses CMD_IN (0x01), RX parser looks for CMD_OUT (0x00).
    // These are opposite directions — a true round-trip needs both ESP and PC.
    // We verify the TX packet format is internally consistent.
    CanFrame orig{};
    orig.id       = 0x7E8;
    orig.extended = false;
    orig.dlc      = 4;
    orig.data[0]  = 0x04;
    orig.data[1]  = 0x41;
    orig.data[2]  = 0x0D;
    orig.data[3]  = 0x7F;

    auto pkt = GvretDriver::buildGvretTxPacket(orig);
    ASSERT_EQ(pkt.size(), 7 + 4);

    // Re-extract ID from packet (bits [2..5])
    uint32_t id = static_cast<uint8_t>(pkt[2])
               | (static_cast<uint32_t>(static_cast<uint8_t>(pkt[3])) <<  8)
               | (static_cast<uint32_t>(static_cast<uint8_t>(pkt[4])) << 16)
               | (static_cast<uint32_t>(static_cast<uint8_t>(pkt[5])) << 24);
    EXPECT_EQ(id & 0x7FFu, 0x7E8u);
    EXPECT_EQ(id & 0x80000000UL, 0u);

    uint8_t dlc = static_cast<uint8_t>(pkt[6]);
    EXPECT_EQ(dlc, 4u);
    EXPECT_EQ(static_cast<uint8_t>(pkt[7]),  0x04u);
    EXPECT_EQ(static_cast<uint8_t>(pkt[8]),  0x41u);
    EXPECT_EQ(static_cast<uint8_t>(pkt[9]),  0x0Du);
    EXPECT_EQ(static_cast<uint8_t>(pkt[10]), 0x7Fu);
}

TEST(GvretDriverTest, RoundTrip_TxThenParse_ExtendedId) {
    CanFrame orig{};
    orig.id       = 0x1421003F;
    orig.extended = true;
    orig.dlc      = 2;
    orig.data[0]  = 0x00;
    orig.data[1]  = 0x01;

    auto pkt = GvretDriver::buildGvretTxPacket(orig);

    uint32_t rawId = static_cast<uint8_t>(pkt[2])
                   | (static_cast<uint32_t>(static_cast<uint8_t>(pkt[3])) <<  8)
                   | (static_cast<uint32_t>(static_cast<uint8_t>(pkt[4])) << 16)
                   | (static_cast<uint32_t>(static_cast<uint8_t>(pkt[5])) << 24);

    EXPECT_NE(rawId & 0x80000000UL, 0u);
    EXPECT_EQ(rawId & 0x1FFFFFFFu, 0x1421003Fu);
}
