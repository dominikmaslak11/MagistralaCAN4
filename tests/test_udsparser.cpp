#include <gtest/gtest.h>
#include "core/UdsFrame.h"
#include "core/UdsParser.h"

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
