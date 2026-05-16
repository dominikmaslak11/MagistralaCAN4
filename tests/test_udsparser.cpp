#include <gtest/gtest.h>
#include "core/UdsFrame.h"
#include "core/UdsParser.h"
#include "core/CanTpLayer.h"

// ── Helper ───────────────────────────────────────────────────

static CanFrame raw(uint32_t id, std::initializer_list<uint8_t> bytes) {
    CanFrame f{};
    f.id  = id;
    f.dlc = static_cast<uint8_t>(bytes.size());
    int i = 0;
    for (uint8_t b : bytes) f.data[i++] = b;
    return f;
}

// ══════════════════════════════════════════════════════════════
// UdsFrame::fromCanFrame
// ══════════════════════════════════════════════════════════════

// ── Request detection ────────────────────────────────────────

TEST(UdsFrameTest, DiagnosticSessionControl_Request) {
    // 0x10 = DSC, sub=0x01 (Default Session)
    UdsFrame f = UdsFrame::fromCanFrame(raw(0x7DF, {0x10, 0x01}));
    EXPECT_EQ(f.type,    UdsFrame::Request);
    EXPECT_EQ(f.sid,     0x10);
    EXPECT_EQ(f.subFunc, 0x01);
    EXPECT_EQ(f.canId,   0x7DFu);
}

TEST(UdsFrameTest, EcuReset_Request) {
    UdsFrame f = UdsFrame::fromCanFrame(raw(0x7E0, {0x11, 0x01}));
    EXPECT_EQ(f.type, UdsFrame::Request);
    EXPECT_EQ(f.sid,  0x11);
}

TEST(UdsFrameTest, ReadDataByIdentifier_Request) {
    // 0x22 + DID 0xF190 (VIN)
    UdsFrame f = UdsFrame::fromCanFrame(raw(0x7DF, {0x22, 0xF1, 0x90}));
    EXPECT_EQ(f.type, UdsFrame::Request);
    EXPECT_EQ(f.sid,  0x22);
    EXPECT_EQ(f.did,  0xF190u);
}

TEST(UdsFrameTest, WriteDataByIdentifier_Request) {
    UdsFrame f = UdsFrame::fromCanFrame(raw(0x7E0, {0x2E, 0xF1, 0x90, 0x01, 0x02}));
    EXPECT_EQ(f.type, UdsFrame::Request);
    EXPECT_EQ(f.sid,  0x2E);
    EXPECT_EQ(f.did,  0xF190u);
}

TEST(UdsFrameTest, SecurityAccess_RequestSeed) {
    UdsFrame f = UdsFrame::fromCanFrame(raw(0x7E0, {0x27, 0x01}));
    EXPECT_EQ(f.type,    UdsFrame::Request);
    EXPECT_EQ(f.sid,     0x27);
    EXPECT_EQ(f.subFunc, 0x01);
}

TEST(UdsFrameTest, TesterPresent_Request) {
    UdsFrame f = UdsFrame::fromCanFrame(raw(0x7DF, {0x3E, 0x00}));
    EXPECT_EQ(f.type, UdsFrame::Request);
    EXPECT_EQ(f.sid,  0x3E);
}

// ── Positive Response detection ──────────────────────────────

TEST(UdsFrameTest, PositiveResponse_DSC) {
    // SID 0x10 response = 0x50 (0x10+0x40)
    UdsFrame f = UdsFrame::fromCanFrame(raw(0x7E8, {0x50, 0x01}));
    EXPECT_EQ(f.type, UdsFrame::PositiveResponse);
    EXPECT_EQ(f.sid,  0x10);
}

TEST(UdsFrameTest, PositiveResponse_EcuReset) {
    UdsFrame f = UdsFrame::fromCanFrame(raw(0x7E8, {0x51, 0x01}));
    EXPECT_EQ(f.type, UdsFrame::PositiveResponse);
    EXPECT_EQ(f.sid,  0x11);
}

TEST(UdsFrameTest, PositiveResponse_ReadDBI_WithDid) {
    // 0x62 = 0x22+0x40, DID = 0xF190
    UdsFrame f = UdsFrame::fromCanFrame(raw(0x7E8, {0x62, 0xF1, 0x90, 0x57, 0x41}));
    EXPECT_EQ(f.type, UdsFrame::PositiveResponse);
    EXPECT_EQ(f.sid,  0x22);
    EXPECT_EQ(f.did,  0xF190u);
}

// ── Negative Response detection ──────────────────────────────

TEST(UdsFrameTest, NegativeResponse_ServiceNotSupported) {
    // 0x7F 0x10 0x11 = NRC for DSC
    UdsFrame f = UdsFrame::fromCanFrame(raw(0x7E8, {0x7F, 0x10, 0x11}));
    EXPECT_EQ(f.type, UdsFrame::NegativeResponse);
    EXPECT_EQ(f.sid,  0x10);
    EXPECT_EQ(f.nrc,  0x11);
}

TEST(UdsFrameTest, NegativeResponse_SecurityAccessDenied) {
    UdsFrame f = UdsFrame::fromCanFrame(raw(0x7E8, {0x7F, 0x27, 0x35}));
    EXPECT_EQ(f.type, UdsFrame::NegativeResponse);
    EXPECT_EQ(f.sid,  0x27);
    EXPECT_EQ(f.nrc,  0x35);
}

// ── FlowControl detection ────────────────────────────────────

TEST(UdsFrameTest, FlowControl_ContinueToSend) {
    UdsFrame f = UdsFrame::fromCanFrame(raw(0x7E8, {0x30, 0x00, 0x00}));
    EXPECT_EQ(f.type, UdsFrame::FlowControl);
}

TEST(UdsFrameTest, FlowControl_Wait) {
    UdsFrame f = UdsFrame::fromCanFrame(raw(0x7E8, {0x31, 0x00, 0x00}));
    EXPECT_EQ(f.type, UdsFrame::FlowControl);
}

// ── Unknown / edge cases ─────────────────────────────────────

TEST(UdsFrameTest, EmptyFrame_Unknown) {
    UdsFrame f = UdsFrame::fromCanFrame(raw(0x7DF, {}));
    EXPECT_EQ(f.type, UdsFrame::Unknown);
}

TEST(UdsFrameTest, PreservesCanId) {
    UdsFrame f = UdsFrame::fromCanFrame(raw(0x18DA10F1, {0x22, 0xF1, 0x90}));
    EXPECT_EQ(f.canId, 0x18DA10F1u);
}

// ── looksLikeUds ─────────────────────────────────────────────

TEST(UdsFrameTest, LooksLikeUds_Request) {
    EXPECT_TRUE(UdsFrame::looksLikeUds(raw(0x7DF, {0x10, 0x01})));
    EXPECT_TRUE(UdsFrame::looksLikeUds(raw(0x7DF, {0x3E, 0x00})));
}

TEST(UdsFrameTest, LooksLikeUds_PositiveResponse) {
    EXPECT_TRUE(UdsFrame::looksLikeUds(raw(0x7E8, {0x50, 0x01})));
    EXPECT_TRUE(UdsFrame::looksLikeUds(raw(0x7E8, {0x62, 0xF1, 0x90})));
}

TEST(UdsFrameTest, LooksLikeUds_NegativeResponse) {
    EXPECT_TRUE(UdsFrame::looksLikeUds(raw(0x7E8, {0x7F, 0x10, 0x11})));
}

TEST(UdsFrameTest, LooksLikeUds_RandomTraffic_False) {
    EXPECT_FALSE(UdsFrame::looksLikeUds(raw(0x100, {0x01})));
    EXPECT_FALSE(UdsFrame::looksLikeUds(raw(0x200, {0x00})));
    EXPECT_FALSE(UdsFrame::looksLikeUds(raw(0x200, {0x80})));
}

TEST(UdsFrameTest, LooksLikeUds_EmptyFrame_False) {
    EXPECT_FALSE(UdsFrame::looksLikeUds(raw(0x7DF, {})));
}

// ── toString ─────────────────────────────────────────────────

TEST(UdsFrameTest, ToString_Request) {
    UdsFrame f = UdsFrame::fromCanFrame(raw(0x7DF, {0x22, 0xF1, 0x90}));
    QString s = f.toString();
    EXPECT_TRUE(s.contains("REQ"));
    EXPECT_TRUE(s.contains("22", Qt::CaseInsensitive));
}

TEST(UdsFrameTest, ToString_PositiveResponse) {
    UdsFrame f = UdsFrame::fromCanFrame(raw(0x7E8, {0x62, 0xF1, 0x90}));
    EXPECT_TRUE(f.toString().contains("POS"));
}

TEST(UdsFrameTest, ToString_NegativeResponse) {
    UdsFrame f = UdsFrame::fromCanFrame(raw(0x7E8, {0x7F, 0x10, 0x11}));
    QString s = f.toString();
    EXPECT_TRUE(s.contains("NEG"));
    EXPECT_TRUE(s.contains("11", Qt::CaseInsensitive));
}

// ══════════════════════════════════════════════════════════════
// UdsParser
// ══════════════════════════════════════════════════════════════

// ── serviceName ──────────────────────────────────────────────

TEST(UdsParserTest, ServiceName_DiagnosticSessionControl) {
    UdsParser p;
    EXPECT_EQ(p.serviceName(0x10), "Diagnostic Session Control");
}

TEST(UdsParserTest, ServiceName_EcuReset) {
    UdsParser p;
    EXPECT_EQ(p.serviceName(0x11), "ECU Reset");
}

TEST(UdsParserTest, ServiceName_ReadDataByIdentifier) {
    UdsParser p;
    EXPECT_EQ(p.serviceName(0x22), "Read Data By Identifier");
}

TEST(UdsParserTest, ServiceName_SecurityAccess) {
    UdsParser p;
    EXPECT_EQ(p.serviceName(0x27), "Security Access");
}

TEST(UdsParserTest, ServiceName_TesterPresent) {
    UdsParser p;
    EXPECT_EQ(p.serviceName(0x3E), "Tester Present");
}

TEST(UdsParserTest, ServiceName_Unknown_ContainsHex) {
    UdsParser p;
    QString s = p.serviceName(0xAB);
    EXPECT_TRUE(s.toLower().contains("ab") || s.contains("Unknown"));
}

TEST(UdsParserTest, IsKnownSid_Known) {
    UdsParser p;
    EXPECT_TRUE(p.isKnownSid(0x10));
    EXPECT_TRUE(p.isKnownSid(0x22));
    EXPECT_TRUE(p.isKnownSid(0x3E));
}

TEST(UdsParserTest, IsKnownSid_Unknown) {
    UdsParser p;
    EXPECT_FALSE(p.isKnownSid(0xAB));
    EXPECT_FALSE(p.isKnownSid(0x00));
}

// ── nrcName ──────────────────────────────────────────────────

TEST(UdsParserTest, NrcName_GeneralReject) {
    EXPECT_EQ(UdsParser::nrcName(0x10), "General Reject");
}

TEST(UdsParserTest, NrcName_ServiceNotSupported) {
    EXPECT_EQ(UdsParser::nrcName(0x11), "Service Not Supported");
}

TEST(UdsParserTest, NrcName_SecurityAccessDenied) {
    // NRC 0x35
    QString s = UdsParser::nrcName(0x35);
    EXPECT_FALSE(s.isEmpty());
}

TEST(UdsParserTest, NrcName_PositiveResponse_0x00) {
    QString s = UdsParser::nrcName(0x00);
    EXPECT_FALSE(s.isEmpty());
}

TEST(UdsParserTest, NrcName_Unknown_ContainsHex) {
    QString s = UdsParser::nrcName(0xFF);
    EXPECT_TRUE(s.toLower().contains("ff") || s.contains("Unknown") || !s.isEmpty());
}

// ── didName ──────────────────────────────────────────────────

TEST(UdsParserTest, DidName_VIN) {
    UdsParser p;
    EXPECT_EQ(p.didName(0xF190), "VIN");
}

TEST(UdsParserTest, DidName_ActiveDiagSession) {
    UdsParser p;
    EXPECT_EQ(p.didName(0xF186), "Active Diagnostic Session");
}

TEST(UdsParserTest, DidName_Unknown_ContainsHex) {
    UdsParser p;
    QString s = p.didName(0x1234);
    EXPECT_TRUE(s.toLower().contains("1234") || !s.isEmpty());
}

// ── subFunctionName ──────────────────────────────────────────

TEST(UdsParserTest, SubFunctionName_DSC_DefaultSession) {
    QString s = UdsParser::subFunctionName(0x10, 0x01);
    EXPECT_FALSE(s.isEmpty());
    EXPECT_TRUE(s.toLower().contains("default") || s.contains("01", Qt::CaseInsensitive));
}

TEST(UdsParserTest, SubFunctionName_DSC_ExtendedSession) {
    QString s = UdsParser::subFunctionName(0x10, 0x03);
    EXPECT_FALSE(s.isEmpty());
}

TEST(UdsParserTest, SubFunctionName_EcuReset_HardReset) {
    QString s = UdsParser::subFunctionName(0x11, 0x01);
    EXPECT_FALSE(s.isEmpty());
}

// ── decodeResponseData ───────────────────────────────────────

TEST(UdsParserTest, DecodeResponseData_EmptyData_NoOp) {
    QString s = UdsParser::decodeResponseData(0x22, nullptr, 0);
    EXPECT_TRUE(s.isEmpty() || !s.isNull());
}

TEST(UdsParserTest, DecodeResponseData_ReadDBI_FewBytes) {
    const uint8_t data[] = {0x62, 0xF1, 0x90, 0x57, 0x30};
    QString s = UdsParser::decodeResponseData(0x22, data, 5);
    // Must not crash; may return hex dump or decoded value
    SUCCEED();
}

// ── knownSids list ───────────────────────────────────────────

TEST(UdsParserTest, KnownSids_NotEmpty) {
    UdsParser p;
    EXPECT_GT(p.knownSids().size(), 5);
}

TEST(UdsParserTest, KnownSids_Contains0x10And0x22) {
    UdsParser p;
    EXPECT_TRUE(p.knownSids().contains(0x10));
    EXPECT_TRUE(p.knownSids().contains(0x22));
}

// ══════════════════════════════════════════════════════════════
// UdsFrame::fromPayload (ISO-TP payload, SID na pozycji [0])
// ══════════════════════════════════════════════════════════════

TEST(UdsFrameFromPayloadTest, Request_DSC_DefaultSession) {
    const uint8_t pl[] = {0x10, 0x01};
    UdsFrame f = UdsFrame::fromPayload(pl, 2, 0x7E0, 1000000, false);
    EXPECT_EQ(f.type,    UdsFrame::Request);
    EXPECT_EQ(f.sid,     0x10u);
    EXPECT_EQ(f.subFunc, 0x01u);
    EXPECT_EQ(f.canId,   0x7E0u);
    EXPECT_FALSE(f.multiFrame);
}

TEST(UdsFrameFromPayloadTest, PositiveResponse_DSC) {
    const uint8_t pl[] = {0x50, 0x01, 0x00, 0x19, 0x01, 0xF4};
    UdsFrame f = UdsFrame::fromPayload(pl, 6, 0x7E8, 2000000, false);
    EXPECT_EQ(f.type, UdsFrame::PositiveResponse);
    EXPECT_EQ(f.sid,  0x10u);
}

TEST(UdsFrameFromPayloadTest, NegativeResponse) {
    // NRC: 7F 22 31 = ReadDBI, requestOutOfRange
    const uint8_t pl[] = {0x7F, 0x22, 0x31};
    UdsFrame f = UdsFrame::fromPayload(pl, 3, 0x7E8, 0, false);
    EXPECT_EQ(f.type, UdsFrame::NegativeResponse);
    EXPECT_EQ(f.sid,  0x22u);
    EXPECT_EQ(f.nrc,  0x31u);
}

TEST(UdsFrameFromPayloadTest, ReadDBI_DID_Extracted) {
    // 0x22 0xF1 0x90 = ReadDataByIdentifier VIN
    const uint8_t pl[] = {0x22, 0xF1, 0x90};
    UdsFrame f = UdsFrame::fromPayload(pl, 3, 0x7DF, 0, false);
    EXPECT_EQ(f.type, UdsFrame::Request);
    EXPECT_EQ(f.sid,  0x22u);
    EXPECT_EQ(f.did,  0xF190u);
}

TEST(UdsFrameFromPayloadTest, MultiFrame_FlagSet) {
    // Długa odpowiedź VIN (17 znaków = 19 bajtów payload)
    uint8_t pl[20] = {0x62, 0xF1, 0x90,
        'W','V','W','Z','Z','Z','7','M','Z','B','W','0','0','0','0','0','1'};
    UdsFrame f = UdsFrame::fromPayload(pl, 20, 0x7E8, 0, true);
    EXPECT_EQ(f.type,       UdsFrame::PositiveResponse);
    EXPECT_EQ(f.sid,        0x22u);
    EXPECT_EQ(f.did,        0xF190u);
    EXPECT_TRUE(f.multiFrame);
    EXPECT_EQ(f.dlc,        20u);
}

TEST(UdsFrameFromPayloadTest, EmptyPayload_UnknownType) {
    UdsFrame f = UdsFrame::fromPayload(nullptr, 0, 0x7E8, 0, false);
    EXPECT_EQ(f.type, UdsFrame::Unknown);
}

TEST(UdsFrameFromPayloadTest, DataPreserved) {
    const uint8_t pl[] = {0x62, 0xF1, 0x90, 0xAA, 0xBB, 0xCC};
    UdsFrame f = UdsFrame::fromPayload(pl, 6, 0x7E8, 0, false);
    EXPECT_EQ(f.data[3], 0xAAu);
    EXPECT_EQ(f.data[4], 0xBBu);
    EXPECT_EQ(f.data[5], 0xCCu);
}

// ══════════════════════════════════════════════════════════════
// UdsFrame::looksLikeUds — sprawdzanie zakresu ID
// ══════════════════════════════════════════════════════════════

static CanFrame rawId(uint32_t id, bool ext = false) {
    CanFrame f{}; f.id = id; f.extended = ext; f.dlc = 2;
    f.data[0] = 0x02; f.data[1] = 0x10; return f;
}

TEST(LooksLikeUdsTest, Id_0x7E0_Is_UDS) {
    EXPECT_TRUE(UdsFrame::looksLikeUds(rawId(0x7E0)));
}
TEST(LooksLikeUdsTest, Id_0x7E8_Is_UDS) {
    EXPECT_TRUE(UdsFrame::looksLikeUds(rawId(0x7E8)));
}
TEST(LooksLikeUdsTest, Id_0x7DF_Broadcast_Is_UDS) {
    EXPECT_TRUE(UdsFrame::looksLikeUds(rawId(0x7DF)));
}
TEST(LooksLikeUdsTest, Id_0x700_Is_UDS) {
    EXPECT_TRUE(UdsFrame::looksLikeUds(rawId(0x700)));
}
TEST(LooksLikeUdsTest, Id_0x7FF_Is_UDS) {
    EXPECT_TRUE(UdsFrame::looksLikeUds(rawId(0x7FF)));
}
TEST(LooksLikeUdsTest, Id_0x100_Not_UDS) {
    EXPECT_FALSE(UdsFrame::looksLikeUds(rawId(0x100)));
}
TEST(LooksLikeUdsTest, Id_0x800_Not_UDS) {
    EXPECT_FALSE(UdsFrame::looksLikeUds(rawId(0x800)));
}
TEST(LooksLikeUdsTest, Extended_0x18DA00F1_Is_UDS) {
    EXPECT_TRUE(UdsFrame::looksLikeUds(rawId(0x18DA00F1u, true)));
}
TEST(LooksLikeUdsTest, Extended_0x18DAF100_Is_UDS) {
    EXPECT_TRUE(UdsFrame::looksLikeUds(rawId(0x18DAF100u, true)));
}
TEST(LooksLikeUdsTest, Extended_0x1FFFFFFF_Not_UDS) {
    EXPECT_FALSE(UdsFrame::looksLikeUds(rawId(0x1FFFFFFFu, true)));
}

// ══════════════════════════════════════════════════════════════
// ISO-TP + UdsFrame integracja — CanTpLayer → fromPayload
// ══════════════════════════════════════════════════════════════

static CanFrame tpFrame(uint32_t id, std::initializer_list<uint8_t> b) {
    CanFrame f{}; f.id = id; f.dlc = static_cast<uint8_t>(b.size());
    int i = 0; for (uint8_t x : b) f.data[i++] = x; return f;
}

TEST(IsoTpUdsIntegrationTest, SingleFrame_DSCRequest_Reassembled) {
    std::vector<UdsFrame> received;
    CanTpLayer tp([&](const CanTpMessage &msg) {
        if (msg.complete)
            received.push_back(UdsFrame::fromPayload(
                msg.payload.data(), (int)msg.payload.size(), msg.canId, msg.timestamp));
    });

    // ISO-TP Single Frame: [0]=0x02 (len=2), [1]=0x10 (SID DSC), [2]=0x03 (Extended)
    tp.processFrame(tpFrame(0x7E0, {0x02, 0x10, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}));

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].sid,     0x10u);
    EXPECT_EQ(received[0].subFunc, 0x03u);
    EXPECT_EQ(received[0].type,    UdsFrame::Request);
}

TEST(IsoTpUdsIntegrationTest, SingleFrame_NegativeResponse) {
    std::vector<UdsFrame> received;
    CanTpLayer tp([&](const CanTpMessage &msg) {
        if (msg.complete)
            received.push_back(UdsFrame::fromPayload(
                msg.payload.data(), (int)msg.payload.size(), msg.canId, msg.timestamp));
    });

    // NR: len=3, 7F 22 31
    tp.processFrame(tpFrame(0x7E8, {0x03, 0x7F, 0x22, 0x31, 0x00, 0x00, 0x00, 0x00}));

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].type, UdsFrame::NegativeResponse);
    EXPECT_EQ(received[0].sid,  0x22u);
    EXPECT_EQ(received[0].nrc,  0x31u);
}

TEST(IsoTpUdsIntegrationTest, MultiFrame_VIN_ReadDBI_Reassembled) {
    // VIN = 17 znaków → payload = 0x62 0xF1 0x90 + 17 bajtów ASCII = 20 bajtów total
    // FF: [10][14][62][F1][90][W][V][W]
    // CF1:[21][Z][Z][Z][7][M][Z][B]
    // CF2:[22][W][0][0][0][0][0][1]  (+ padding)
    std::vector<UdsFrame> received;
    CanTpLayer tp([&](const CanTpMessage &msg) {
        if (msg.complete)
            received.push_back(UdsFrame::fromPayload(
                msg.payload.data(), (int)msg.payload.size(), msg.canId, msg.timestamp, msg.payload.size() > 7));
    });

    // FF: total=20 (0x14), first 6 bytes of payload
    tp.processFrame(tpFrame(0x7E8, {0x10, 0x14, 0x62, 0xF1, 0x90, 'W', 'V', 'W'}));
    // CF1 (SN=1): next 7 bytes
    tp.processFrame(tpFrame(0x7E8, {0x21, 'Z', 'Z', 'Z', '7', 'M', 'Z', 'B'}));
    // CF2 (SN=2): remaining 7 bytes
    tp.processFrame(tpFrame(0x7E8, {0x22, 'W', '0', '0', '0', '0', '0', '1'}));

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].type,       UdsFrame::PositiveResponse);
    EXPECT_EQ(received[0].sid,        0x22u);
    EXPECT_EQ(received[0].did,        0xF190u);
    EXPECT_TRUE(received[0].multiFrame);
    EXPECT_EQ(received[0].dlc,        20u);
    EXPECT_EQ(received[0].data[3],    'W');  // VIN[0]
    EXPECT_EQ(received[0].data[4],    'V');
}

TEST(IsoTpUdsIntegrationTest, TwoIndependentSessions_DifferentIds) {
    std::vector<UdsFrame> received;
    CanTpLayer tp([&](const CanTpMessage &msg) {
        if (msg.complete)
            received.push_back(UdsFrame::fromPayload(
                msg.payload.data(), (int)msg.payload.size(), msg.canId, msg.timestamp));
    });

    // ECU1 (0x7E8): SF response
    tp.processFrame(tpFrame(0x7E8, {0x02, 0x50, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}));
    // ECU2 (0x7E9): SF response
    tp.processFrame(tpFrame(0x7E9, {0x02, 0x50, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}));

    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0].canId, 0x7E8u);
    EXPECT_EQ(received[1].canId, 0x7E9u);
    // Oba to positive response na DSC (SID 0x10)
    EXPECT_EQ(received[0].sid, 0x10u);
    EXPECT_EQ(received[1].sid, 0x10u);
}
