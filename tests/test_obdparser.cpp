#include <gtest/gtest.h>
#include "core/ObdFrame.h"
#include "core/IsoTpReassembler.h"

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

// ── ObdParser::decodeDtc ─────────────────────────────────────

TEST(ObdParserTest, DecodeDtc_Zero_ReturnsEmpty) {
    EXPECT_TRUE(ObdParser::decodeDtc(0x0000).isEmpty());
}

TEST(ObdParserTest, DecodeDtc_P0300_MisfireDetected) {
    // P0300: bits[15:14]=00 (P), bits[13:12]=00, bits[11:8]=03, bits[7:4]=00, bits[3:0]=00
    // raw = 0b 00 00 0011 0000 0000 = 0x0300
    uint16_t raw = 0x0300;
    QString dtc = ObdParser::decodeDtc(raw);
    EXPECT_EQ(dtc, "P0300");
}

TEST(ObdParserTest, DecodeDtc_P1234) {
    // P=00, d1=01, d2=2, d3=3, d4=4
    // raw = 0b 00 01 0010 0011 0100 = 0x1234
    uint16_t raw = 0x1234;
    QString dtc = ObdParser::decodeDtc(raw);
    EXPECT_EQ(dtc, "P1234");
}

TEST(ObdParserTest, DecodeDtc_C_Category) {
    // C: bits[15:14]=01 → 0100 0000 0000 0000 | rest
    // C0001: bits[15:14]=01, d1=00, d2=0, d3=0, d4=1 = 0x4001
    uint16_t raw = 0x4001;
    QString dtc = ObdParser::decodeDtc(raw);
    EXPECT_TRUE(dtc.startsWith('C'));
}

TEST(ObdParserTest, DecodeDtc_B_Category) {
    // B: bits[15:14]=10 → 0x8000 base
    uint16_t raw = 0x8001;
    QString dtc = ObdParser::decodeDtc(raw);
    EXPECT_TRUE(dtc.startsWith('B'));
}

TEST(ObdParserTest, DecodeDtc_U_Category) {
    // U: bits[15:14]=11 → 0xC000 base
    uint16_t raw = 0xC001;
    QString dtc = ObdParser::decodeDtc(raw);
    EXPECT_TRUE(dtc.startsWith('U'));
}

TEST(ObdParserTest, DecodeDtc_FiveCharacters) {
    uint16_t raw = 0x0300;
    EXPECT_EQ(ObdParser::decodeDtc(raw).length(), 5);
}

// ── ObdParser::parseDtcs ─────────────────────────────────────

static ObdFrame makeObdResponse(uint8_t mode, std::initializer_list<uint8_t> payload) {
    // Build a CAN frame with proper OBD-II response format
    // data[0] = ISO-TP len, data[1] = mode+0x40, data[2..] = payload
    CanFrame cf{};
    cf.id  = 0x7E8;
    uint8_t modeResp = static_cast<uint8_t>(mode + 0x40);
    uint8_t totalLen = static_cast<uint8_t>(1 + payload.size());  // mode byte + payload bytes
    cf.data[0] = totalLen;
    cf.data[1] = modeResp;
    int i = 2;
    for (uint8_t b : payload) cf.data[i++] = b;
    cf.dlc = static_cast<uint8_t>(i);
    return ObdFrame::fromCanFrame(cf);
}

TEST(ObdParserTest, ParseDtcs_Mode03_TwoCodes) {
    ObdParser p;
    // Mode 03 response with 2 DTCs: 0x0300 (P0300) and 0x0100 (P0100)
    ObdFrame f = makeObdResponse(0x03, {0x03, 0x00, 0x01, 0x00});
    EXPECT_EQ(f.mode, 0x03u);
    EXPECT_EQ(f.type, ObdFrame::Response);
    QStringList dtcs = p.parseDtcs(f);
    ASSERT_EQ(dtcs.size(), 2);
    EXPECT_EQ(dtcs[0], "P0300");
    EXPECT_EQ(dtcs[1], "P0100");
}

TEST(ObdParserTest, ParseDtcs_Mode07_PendingCode) {
    ObdParser p;
    // Mode 07 pending DTC: 0x0171 (P0171 — System Too Lean Bank 1)
    ObdFrame f = makeObdResponse(0x07, {0x01, 0x71});
    QStringList dtcs = p.parseDtcs(f);
    ASSERT_EQ(dtcs.size(), 1);
    EXPECT_EQ(dtcs[0], "P0171");
}

TEST(ObdParserTest, ParseDtcs_NotResponseType_Empty) {
    ObdParser p;
    CanFrame cf{};
    cf.id = 0x7DF; cf.dlc = 2; cf.data[0] = 0x01; cf.data[1] = 0x03;
    ObdFrame f = ObdFrame::fromCanFrame(cf);
    EXPECT_TRUE(p.parseDtcs(f).isEmpty());
}

TEST(ObdParserTest, ParseDtcs_ZeroCode_Skipped) {
    ObdParser p;
    // DTC pair 0x0000 should be skipped (decodeDtc returns empty)
    ObdFrame f = makeObdResponse(0x03, {0x00, 0x00});
    QStringList dtcs = p.parseDtcs(f);
    EXPECT_TRUE(dtcs.isEmpty());
}

// ── New Mode 01 PIDs ─────────────────────────────────────────

TEST(ObdParserTest, NewPid_EngineOilTemp) {
    ObdParser p;
    EXPECT_EQ(p.pidName(0x01, 0x5C), "Engine Oil Temperature");
    EXPECT_EQ(p.pidUnit(0x01, 0x5C), "°C");
}

TEST(ObdParserTest, NewPid_FuelRailPressure) {
    ObdParser p;
    EXPECT_EQ(p.pidName(0x01, 0x22), "Fuel Rail Pressure (rel)");
}

TEST(ObdParserTest, NewPid_TimeMilOn) {
    ObdParser p;
    EXPECT_EQ(p.pidName(0x01, 0x4D), "Time MIL On");
    EXPECT_EQ(p.pidUnit(0x01, 0x4D), "min");
}

TEST(ObdParserTest, DecodeOilTemp_OneByte) {
    // Oil temp = A - 40
    ObdParser p;
    const uint8_t data[] = {0x03, 0x41, 0x5C, 140};  // 140 - 40 = 100°C
    EXPECT_NEAR(p.decodePidValue(0x01, 0x5C, data, 4), 100.0, 0.01);
}

// ── IsoTpReassembler ─────────────────────────────────────────────────────────

TEST(IsoTpTest, SingleFrame_ReturnsPayloadImmediately) {
    IsoTpReassembler r;
    // 7E8 04 41 0C 0F A0 — Mode 1 RPM response (single frame, len=4)
    CanFrame f = makeRaw(0x7E8, {0x04, 0x41, 0x0C, 0x0F, 0xA0, 0x00, 0x00, 0x00});
    IsoTpReassembler::Result out;
    ASSERT_TRUE(r.feed(f, out));
    EXPECT_EQ(out.canId, 0x7E8u);
    ASSERT_EQ(out.payload.size(), 4);
    EXPECT_EQ(static_cast<uint8_t>(out.payload[0]), 0x41u);
    EXPECT_EQ(static_cast<uint8_t>(out.payload[1]), 0x0Cu);
    EXPECT_EQ(static_cast<uint8_t>(out.payload[2]), 0x0Fu);
    EXPECT_EQ(static_cast<uint8_t>(out.payload[3]), 0xA0u);
}

TEST(IsoTpTest, SingleFrame_NonObdId_Ignored) {
    IsoTpReassembler r;
    CanFrame f = makeRaw(0x200, {0x04, 0x41, 0x0C, 0x0F, 0xA0});
    IsoTpReassembler::Result out;
    EXPECT_FALSE(r.feed(f, out));
}

TEST(IsoTpTest, SingleFrame_ZeroLength_Ignored) {
    IsoTpReassembler r;
    CanFrame f = makeRaw(0x7E8, {0x00, 0x41, 0x0C});
    IsoTpReassembler::Result out;
    EXPECT_FALSE(r.feed(f, out));
}

TEST(IsoTpTest, MultiFrame_TwoFrames_ReassemblesCorrectly) {
    // 4 DTCs: mode 03 payload = 1 (mode) + 8 (DTC bytes) = 9 bytes
    // FF: len=0x0009, first 6 payload bytes [0x43, DTC1_H, DTC1_L, DTC2_H, DTC2_L, DTC3_H]
    // CF: seq=1, next 3 bytes [DTC3_L, DTC4_H, DTC4_L] + padding
    IsoTpReassembler r;
    IsoTpReassembler::Result out;

    CanFrame ff = makeRaw(0x7E8, {0x10, 0x09, 0x43, 0x03, 0x00, 0x01, 0x00, 0x02});
    EXPECT_FALSE(r.feed(ff, out));

    CanFrame cf = makeRaw(0x7E8, {0x21, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00});
    ASSERT_TRUE(r.feed(cf, out));

    EXPECT_EQ(out.canId, 0x7E8u);
    ASSERT_EQ(out.payload.size(), 9);
    EXPECT_EQ(static_cast<uint8_t>(out.payload[0]), 0x43u);   // mode 03 response
    EXPECT_EQ(static_cast<uint8_t>(out.payload[1]), 0x03u);   // DTC1 high
    EXPECT_EQ(static_cast<uint8_t>(out.payload[2]), 0x00u);   // DTC1 low → P0300
}

TEST(IsoTpTest, MultiFrame_SequenceError_AbandonedSession) {
    IsoTpReassembler r;
    IsoTpReassembler::Result out;

    CanFrame ff = makeRaw(0x7E8, {0x10, 0x09, 0x43, 0x03, 0x00, 0x01, 0x00, 0x02});
    EXPECT_FALSE(r.feed(ff, out));

    // Wrong sequence (expect 0x21, send 0x22)
    CanFrame bad = makeRaw(0x7E8, {0x22, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00});
    EXPECT_FALSE(r.feed(bad, out));

    // Correct sequence after error — session was abandoned
    CanFrame cf = makeRaw(0x7E8, {0x21, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00});
    EXPECT_FALSE(r.feed(cf, out));
}

TEST(IsoTpTest, MultiFrame_ConcurrentSessions_DifferentCanIds) {
    IsoTpReassembler r;
    IsoTpReassembler::Result out;

    CanFrame ff1 = makeRaw(0x7E8, {0x10, 0x09, 0x43, 0x03, 0x00, 0x01, 0x00, 0x02});
    CanFrame ff2 = makeRaw(0x7E9, {0x10, 0x09, 0x43, 0x05, 0x00, 0x06, 0x00, 0x07});
    EXPECT_FALSE(r.feed(ff1, out));
    EXPECT_FALSE(r.feed(ff2, out));

    // Complete ECU 2 first
    CanFrame cf2 = makeRaw(0x7E9, {0x21, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00});
    ASSERT_TRUE(r.feed(cf2, out));
    EXPECT_EQ(out.canId, 0x7E9u);

    // Complete ECU 1
    CanFrame cf1 = makeRaw(0x7E8, {0x21, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00});
    ASSERT_TRUE(r.feed(cf1, out));
    EXPECT_EQ(out.canId, 0x7E8u);
}

TEST(IsoTpTest, FlowControl_Ignored) {
    IsoTpReassembler r;
    IsoTpReassembler::Result out;
    CanFrame fc = makeRaw(0x7E0, {0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    EXPECT_FALSE(r.feed(fc, out));
}

TEST(IsoTpTest, ResetAll_ClearsActiveSession) {
    IsoTpReassembler r;
    IsoTpReassembler::Result out;

    CanFrame ff = makeRaw(0x7E8, {0x10, 0x09, 0x43, 0x03, 0x00, 0x01, 0x00, 0x02});
    EXPECT_FALSE(r.feed(ff, out));

    r.resetAll();

    // Consecutive frame after reset should not produce output (no active session)
    CanFrame cf = makeRaw(0x7E8, {0x21, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00});
    EXPECT_FALSE(r.feed(cf, out));
}

// ── ObdFrame::fromPayload ─────────────────────────────────────────────────────

TEST(IsoTpTest, FromPayload_ModeAndPidParsed) {
    // Payload: [0x41, 0x0C, 0x0F, 0xA0] — Mode 1 RPM response
    QByteArray payload = QByteArray::fromHex("410C0FA0");
    ObdFrame f = ObdFrame::fromPayload(0x7E8, 12345, payload);
    EXPECT_EQ(f.type, ObdFrame::Response);
    EXPECT_EQ(f.mode, 0x01u);
    EXPECT_EQ(f.pid, 0x0Cu);
    EXPECT_EQ(f.canId, 0x7E8u);
    EXPECT_EQ(f.timestamp, 12345u);
}

TEST(IsoTpTest, FromPayload_DecodePidValue_Correct) {
    // RPM = (0x0F * 256 + 0xA0) * 0.25 = 1000 rpm
    QByteArray payload = QByteArray::fromHex("410C0FA0");
    ObdFrame f = ObdFrame::fromPayload(0x7E8, 0, payload);
    ObdParser p;
    double rpm = p.decodePidValue(f.mode, f.pid, f.data.data(), f.dlc);
    EXPECT_NEAR(rpm, 1000.0, 0.01);
}

TEST(IsoTpTest, FromPayload_DtcParsing_FourDtcs) {
    // Mode 03 response with 4 DTCs: 0x0300, 0x0100, 0x0200, 0x0400
    // payload: [0x43, 0x03, 0x00, 0x01, 0x00, 0x02, 0x00, 0x04, 0x00]
    QByteArray payload = QByteArray::fromHex("430300010002000400");
    ObdFrame f = ObdFrame::fromPayload(0x7E8, 0, payload);
    EXPECT_EQ(f.mode, 0x03u);
    EXPECT_EQ(f.type, ObdFrame::Response);
    ObdParser p;
    QStringList dtcs = p.parseDtcs(f);
    ASSERT_EQ(dtcs.size(), 4);
    EXPECT_EQ(dtcs[0], "P0300");
    EXPECT_EQ(dtcs[1], "P0100");
    EXPECT_EQ(dtcs[2], "P0200");
    EXPECT_EQ(dtcs[3], "P0400");
}

TEST(IsoTpTest, FromPayload_NonObdId_TypeUnknown) {
    QByteArray payload = QByteArray::fromHex("410C0FA0");
    ObdFrame f = ObdFrame::fromPayload(0x200, 0, payload);
    EXPECT_EQ(f.type, ObdFrame::Unknown);
}

TEST(IsoTpTest, SingleFrameThenFromPayload_RoundTripRpm) {
    // Full pipeline: CAN frame → reassembler → fromPayload → decodePidValue
    IsoTpReassembler r;
    IsoTpReassembler::Result res;
    CanFrame f = makeRaw(0x7E8, {0x04, 0x41, 0x0C, 0x1F, 0x40, 0x00, 0x00, 0x00});
    ASSERT_TRUE(r.feed(f, res));

    ObdFrame of = ObdFrame::fromPayload(res.canId, res.timestamp, res.payload);
    EXPECT_EQ(of.mode, 0x01u);
    EXPECT_EQ(of.pid, 0x0Cu);

    // RPM = (0x1F*256 + 0x40) * 0.25 = (7936 + 64) * 0.25 = 2000 rpm
    ObdParser p;
    EXPECT_NEAR(p.decodePidValue(of.mode, of.pid, of.data.data(), of.dlc), 2000.0, 0.01);
}
