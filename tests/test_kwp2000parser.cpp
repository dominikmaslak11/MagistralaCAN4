#include <gtest/gtest.h>
#include "core/Kwp2000Parser.h"

// ── Request parsing ───────────────────────────────────────────────────────────

TEST(Kwp2000Parser, RequestSingleByte) {
    Kwp2000Frame f = Kwp2000Parser::parse({0x10});
    EXPECT_FALSE(f.isResponse);
    EXPECT_FALSE(f.isNegative);
    EXPECT_EQ(f.sid, 0x10u);
    EXPECT_TRUE(f.payload.empty());
}

TEST(Kwp2000Parser, RequestWithParams) {
    Kwp2000Frame f = Kwp2000Parser::parse({0x21, 0x05});
    EXPECT_FALSE(f.isResponse);
    EXPECT_EQ(f.sid, 0x21u);
    ASSERT_EQ(f.payload.size(), 1u);
    EXPECT_EQ(f.payload[0], 0x05u);
}

TEST(Kwp2000Parser, TesterPresent) {
    Kwp2000Frame f = Kwp2000Parser::parse({0x3E});
    EXPECT_EQ(f.sid, Kwp2000Sid::TesterPresent);
    EXPECT_FALSE(f.isResponse);
}

// ── Positive response ─────────────────────────────────────────────────────────

TEST(Kwp2000Parser, PositiveResponse) {
    // Response to StartDiagSession: 0x50 = 0x10 | 0x40
    Kwp2000Frame f = Kwp2000Parser::parse({0x50, 0x01});
    EXPECT_TRUE(f.isResponse);
    EXPECT_TRUE(f.isPositive);
    EXPECT_FALSE(f.isNegative);
    EXPECT_EQ(f.sid, 0x10u); // original SID without 0x40
    ASSERT_EQ(f.payload.size(), 1u);
    EXPECT_EQ(f.payload[0], 0x01u);
}

TEST(Kwp2000Parser, PositiveResponseNoPayload) {
    // 0x7E = TesterPresent response (0x3E | 0x40)
    Kwp2000Frame f = Kwp2000Parser::parse({0x7E});
    EXPECT_TRUE(f.isPositive);
    EXPECT_EQ(f.sid, Kwp2000Sid::TesterPresent);
    EXPECT_TRUE(f.payload.empty());
}

// ── Negative response ─────────────────────────────────────────────────────────

TEST(Kwp2000Parser, NegativeResponse) {
    Kwp2000Frame f = Kwp2000Parser::parse({0x7F, 0x10, 0x22});
    EXPECT_TRUE(f.isResponse);
    EXPECT_TRUE(f.isNegative);
    EXPECT_FALSE(f.isPositive);
    EXPECT_EQ(f.sid, 0x10u);
    EXPECT_EQ(f.nrc, 0x22u);
}

TEST(Kwp2000Parser, NegativeResponseIncomplete) {
    // Only 0x7F — SID and NRC default to 0
    Kwp2000Frame f = Kwp2000Parser::parse({0x7F});
    EXPECT_TRUE(f.isNegative);
    EXPECT_EQ(f.sid, 0u);
    EXPECT_EQ(f.nrc, 0u);
}

TEST(Kwp2000Parser, NegativeResponseBusyRepeat) {
    Kwp2000Frame f = Kwp2000Parser::parse({0x7F, 0x27, Kwp2000Nrc::BusyRepeatRequest});
    EXPECT_EQ(f.nrc, Kwp2000Nrc::BusyRepeatRequest);
    EXPECT_EQ(f.sid, Kwp2000Sid::SecurityAccess);
}

// ── Empty input ───────────────────────────────────────────────────────────────

TEST(Kwp2000Parser, EmptyInput) {
    Kwp2000Frame f = Kwp2000Parser::parse({});
    EXPECT_EQ(f.sid, 0u);
    EXPECT_FALSE(f.isResponse);
}

// ── LRC computation ───────────────────────────────────────────────────────────

TEST(Kwp2000Parser, LrcEmpty) {
    EXPECT_EQ(Kwp2000Parser::computeLrc(nullptr, 0), 0u);
}

TEST(Kwp2000Parser, LrcSingleByte) {
    uint8_t d[] = {0xAB};
    EXPECT_EQ(Kwp2000Parser::computeLrc(d, 1), 0xABu);
}

TEST(Kwp2000Parser, LrcXorCancels) {
    uint8_t d[] = {0x10, 0x01, 0x11}; // 0x10 ^ 0x01 ^ 0x11 = 0x00
    EXPECT_EQ(Kwp2000Parser::computeLrc(d, 3), 0x00u);
}

TEST(Kwp2000Parser, LrcGeneral) {
    uint8_t d[] = {0x3E, 0x00}; // 0x3E ^ 0x00 = 0x3E
    EXPECT_EQ(Kwp2000Parser::computeLrc(d, 2), 0x3Eu);
}

// ── Service and NRC names ─────────────────────────────────────────────────────

TEST(Kwp2000Parser, ServiceNameKnown) {
    EXPECT_EQ(Kwp2000Parser::serviceName(0x10), "StartDiagnosticSession");
    EXPECT_EQ(Kwp2000Parser::serviceName(0x3E), "TesterPresent");
    EXPECT_EQ(Kwp2000Parser::serviceName(0x7F), "NegativeResponse");
}

TEST(Kwp2000Parser, ServiceNameUnknown) {
    std::string name = Kwp2000Parser::serviceName(0xAA);
    EXPECT_NE(name.find("Unknown"), std::string::npos);
}

TEST(Kwp2000Parser, NrcDescriptions) {
    EXPECT_NE(Kwp2000Parser::nrcDescription(Kwp2000Nrc::GeneralReject).find("reject"),
              std::string::npos);
    EXPECT_NE(Kwp2000Parser::nrcDescription(Kwp2000Nrc::SecurityAccessDenied).find("denied"),
              std::string::npos);
    EXPECT_NE(Kwp2000Parser::nrcDescription(0x99).find("Unknown"), std::string::npos);
}

// ── isRequestSid ─────────────────────────────────────────────────────────────

TEST(Kwp2000Parser, IsRequestSid) {
    EXPECT_TRUE(Kwp2000Parser::isRequestSid(0x10));
    EXPECT_TRUE(Kwp2000Parser::isRequestSid(0x3E));
    EXPECT_FALSE(Kwp2000Parser::isRequestSid(0x7F)); // negative response marker
    EXPECT_FALSE(Kwp2000Parser::isRequestSid(0x50)); // 0x10 | 0x40 — positive response
    EXPECT_FALSE(Kwp2000Parser::isRequestSid(0x7E)); // TesterPresent positive response
}
