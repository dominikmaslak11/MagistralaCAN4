#include <gtest/gtest.h>
#include "core/ObdFrame.h"

// ── Helper ───────────────────────────────────────────────────

static CanFrame makeRaw(uint32_t id, std::initializer_list<uint8_t> bytes) {
    CanFrame f{};
    f.id  = id;
    f.dlc = static_cast<uint8_t>(bytes.size());
    int i = 0;
    for (uint8_t b : bytes) f.data[i++] = b;
    return f;
}

// ── ObdFrame::isObd ──────────────────────────────────────────

TEST(ObdParserTest, IsObd_BroadcastId) {
    EXPECT_TRUE(ObdFrame::isObd(makeRaw(0x7DF, {0x02, 0x01, 0x0C})));
}

TEST(ObdParserTest, IsObd_UnicastRequest) {
    EXPECT_TRUE(ObdFrame::isObd(makeRaw(0x7E0, {0x02, 0x01, 0x0D})));
}

TEST(ObdParserTest, IsObd_Response) {
    EXPECT_TRUE(ObdFrame::isObd(makeRaw(0x7E8, {0x04, 0x41, 0x0C, 0x0F, 0xA0})));
}

TEST(ObdParserTest, IsObd_NonObdId_False) {
    EXPECT_FALSE(ObdFrame::isObd(makeRaw(0x100, {0x02, 0x01, 0x0C})));
    EXPECT_FALSE(ObdFrame::isObd(makeRaw(0x7F0, {0x01})));
}

// ── ObdFrame::fromCanFrame — request parsing ─────────────────

TEST(ObdParserTest, FromCanFrame_Request_ModeAndPid) {
    // 7DF 02 01 0C — Mode 1 PID 0x0C (Engine RPM)
    CanFrame raw = makeRaw(0x7DF, {0x02, 0x01, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00});
    ObdFrame f = ObdFrame::fromCanFrame(raw);
    EXPECT_EQ(f.type, ObdFrame::Request);
    EXPECT_EQ(f.mode, 0x01);
    EXPECT_EQ(f.pid,  0x0C);
}

TEST(ObdParserTest, FromCanFrame_Response_ModeAndPid) {
    // 7E8 04 41 0C 0F A0 — Mode 1 response PID 0x0C
    CanFrame raw = makeRaw(0x7E8, {0x04, 0x41, 0x0C, 0x0F, 0xA0});
    ObdFrame f = ObdFrame::fromCanFrame(raw);
    EXPECT_EQ(f.type, ObdFrame::Response);
    EXPECT_EQ(f.mode, 0x01);
    EXPECT_EQ(f.pid,  0x0C);
}

TEST(ObdParserTest, FromCanFrame_TooShort_Unknown) {
    CanFrame raw = makeRaw(0x7E8, {0x01});
    ObdFrame f = ObdFrame::fromCanFrame(raw);
    EXPECT_EQ(f.type, ObdFrame::Unknown);
}

TEST(ObdParserTest, FromCanFrame_NonObdId_Unknown) {
    CanFrame raw = makeRaw(0x200, {0x02, 0x01, 0x0D});
    ObdFrame f = ObdFrame::fromCanFrame(raw);
    EXPECT_EQ(f.type, ObdFrame::Unknown);
}

// ── ObdParser metadata ───────────────────────────────────────

TEST(ObdParserTest, ModeNameKnown) {
    ObdParser p;
    EXPECT_EQ(p.modeName(0x01), "Current Data");
    EXPECT_EQ(p.modeName(0x03), "Stored DTC");
    EXPECT_EQ(p.modeName(0x09), "Vehicle Information");
}

TEST(ObdParserTest, ModeNameUnknown_ReturnsHex) {
    ObdParser p;
    QString name = p.modeName(0xFF);
    EXPECT_TRUE(name.contains("ff", Qt::CaseInsensitive));
}

TEST(ObdParserTest, PidNameKnown) {
    ObdParser p;
    EXPECT_EQ(p.pidName(0x01, 0x0C), "Engine RPM");
    EXPECT_EQ(p.pidName(0x01, 0x0D), "Vehicle Speed");
    EXPECT_EQ(p.pidName(0x01, 0x05), "Engine Coolant Temp");
}

TEST(ObdParserTest, PidNameUnknown_ReturnsHex) {
    ObdParser p;
    QString name = p.pidName(0x01, 0xAB);
    EXPECT_TRUE(name.contains("00AB", Qt::CaseInsensitive) ||
                name.contains("ab",   Qt::CaseInsensitive));
}

TEST(ObdParserTest, PidUnit) {
    ObdParser p;
    EXPECT_EQ(p.pidUnit(0x01, 0x0C), "rpm");
    EXPECT_EQ(p.pidUnit(0x01, 0x0D), "km/h");
}

// ── ObdParser::decodePidValue ────────────────────────────────

TEST(ObdParserTest, DecodeRpm_TwoBytes) {
    // RPM = (256*A + B) / 4
    // data: [0]=len [1]=mode [2]=pid [3]=A [4]=B
    // A=0x0F B=0xA0 → raw = 0x0FA0 = 4000 → 4000 * 0.25 = 1000 rpm
    ObdParser p;
    const uint8_t data[] = {0x04, 0x41, 0x0C, 0x0F, 0xA0};
    EXPECT_NEAR(p.decodePidValue(0x01, 0x0C, data, 5), 1000.0, 0.01);
}

TEST(ObdParserTest, DecodeVehicleSpeed_OneByte) {
    // Speed = A km/h
    ObdParser p;
    const uint8_t data[] = {0x03, 0x41, 0x0D, 80};
    EXPECT_NEAR(p.decodePidValue(0x01, 0x0D, data, 4), 80.0, 0.01);
}

TEST(ObdParserTest, DecodeCoolantTemp_OneByte) {
    // Coolant = A - 40 °C
    ObdParser p;
    const uint8_t data[] = {0x03, 0x41, 0x05, 90};  // 90 - 40 = 50°C
    EXPECT_NEAR(p.decodePidValue(0x01, 0x05, data, 4), 50.0, 0.01);
}

TEST(ObdParserTest, DecodeThrottlePosition_OneByte) {
    // Throttle = A * 100/255
    ObdParser p;
    const uint8_t data[] = {0x03, 0x41, 0x11, 0xFF};  // 255 * 100/255 = 100%
    EXPECT_NEAR(p.decodePidValue(0x01, 0x11, data, 4), 100.0, 0.1);
}

TEST(ObdParserTest, DecodeTimingAdvance_NegativeOffset) {
    // Timing = A * 0.5 - 64
    ObdParser p;
    const uint8_t data[] = {0x03, 0x41, 0x0E, 128};  // 128 * 0.5 - 64 = 0°
    EXPECT_NEAR(p.decodePidValue(0x01, 0x0E, data, 4), 0.0, 0.01);
}

TEST(ObdParserTest, DecodePid_TooShortBuffer_ReturnsZero) {
    ObdParser p;
    // RPM is 2-byte — len=4 means only A byte, no B → should return 0
    const uint8_t data[] = {0x03, 0x41, 0x0C, 0x0F};
    EXPECT_DOUBLE_EQ(p.decodePidValue(0x01, 0x0C, data, 4), 0.0);
}

TEST(ObdParserTest, DecodeUnknownPid_ReturnsZero) {
    ObdParser p;
    const uint8_t data[] = {0x03, 0x41, 0xAB, 0x10};
    EXPECT_DOUBLE_EQ(p.decodePidValue(0x01, 0xAB, data, 4), 0.0);
}

// ── ObdFrame::toString ───────────────────────────────────────

TEST(ObdParserTest, ToString_Request) {
    CanFrame raw = makeRaw(0x7DF, {0x02, 0x01, 0x0C, 0, 0, 0, 0, 0});
    ObdFrame f = ObdFrame::fromCanFrame(raw);
    QString s = f.toString();
    EXPECT_TRUE(s.contains("REQ"));
    EXPECT_TRUE(s.contains("01", Qt::CaseInsensitive));
}

TEST(ObdParserTest, ToString_Response) {
    CanFrame raw = makeRaw(0x7E8, {0x04, 0x41, 0x0D, 80, 0});
    ObdFrame f = ObdFrame::fromCanFrame(raw);
    QString s = f.toString();
    EXPECT_TRUE(s.contains("RESP"));
}

TEST(ObdParserTest, KnownModes_ContainsMode1) {
    ObdParser p;
    EXPECT_TRUE(p.knownModes().contains(0x01));
}
