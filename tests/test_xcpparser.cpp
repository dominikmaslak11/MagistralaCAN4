#include <gtest/gtest.h>
#include "core/XcpParser.h"

// ── Command parsing ───────────────────────────────────────────────────────────

TEST(XcpParser, ConnectCommandParsedAsResponse) {
    // Without direction context, 0xFF is treated as positive response
    // (same PID is used for CONNECT command on CMD channel)
    XcpFrame f = XcpParser::parse({XcpCmd::Connect, 0x00});
    EXPECT_EQ(f.pid, XcpCmd::Connect);
    EXPECT_TRUE(f.isPositive); // parser heuristic: 0xFF → positive response
    ASSERT_EQ(f.payload.size(), 1u);
    EXPECT_EQ(f.payload[0], 0x00u);
}

TEST(XcpParser, SetMtaCommand) {
    XcpFrame f = XcpParser::parse({XcpCmd::SetMta, 0x00, 0x00, 0x04, 0x00, 0x80, 0x00, 0x00});
    EXPECT_EQ(f.pid, XcpCmd::SetMta);
    EXPECT_EQ(f.dir, XcpFrame::Direction::Command);
    EXPECT_EQ(f.payload.size(), 7u);
}

TEST(XcpParser, DownloadCommand) {
    XcpFrame f = XcpParser::parse({XcpCmd::Download, 0x02, 0xFF, 0xFF});
    EXPECT_EQ(f.pid, XcpCmd::Download);
    EXPECT_FALSE(f.isNegative);
}

// ── Positive response ─────────────────────────────────────────────────────────

TEST(XcpParser, PositiveResponse) {
    // Response to CONNECT: 0xFF + resource byte + ...
    XcpFrame f = XcpParser::parse({0xFF, 0x07, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00});
    EXPECT_TRUE(f.isPositive);
    EXPECT_FALSE(f.isNegative);
    EXPECT_EQ(f.dir, XcpFrame::Direction::Response);
    EXPECT_EQ(f.pid, 0xFFu);
    EXPECT_EQ(f.payload.size(), 7u);
}

TEST(XcpParser, PositiveResponseSingleByte) {
    XcpFrame f = XcpParser::parse({0xFF});
    EXPECT_TRUE(f.isPositive);
    EXPECT_TRUE(f.payload.empty());
}

// ── Negative response ─────────────────────────────────────────────────────────

TEST(XcpParser, NegativeResponse) {
    XcpFrame f = XcpParser::parse({0xFE, XcpErr::AccessDenied});
    EXPECT_TRUE(f.isNegative);
    EXPECT_FALSE(f.isPositive);
    EXPECT_EQ(f.dir, XcpFrame::Direction::Response);
    EXPECT_EQ(f.errorCode, XcpErr::AccessDenied);
}

TEST(XcpParser, NegativeResponseNoErrCode) {
    XcpFrame f = XcpParser::parse({0xFE});
    EXPECT_TRUE(f.isNegative);
    EXPECT_EQ(f.errorCode, 0u);
}

// ── Event / Service ───────────────────────────────────────────────────────────

TEST(XcpParser, EventPacket) {
    XcpFrame f = XcpParser::parse({0xFD, 0x01, 0x00});
    EXPECT_TRUE(f.isEvent);
    EXPECT_EQ(f.dir, XcpFrame::Direction::Event);
}

TEST(XcpParser, ServicePacket) {
    XcpFrame f = XcpParser::parse({0xFC, 0x00});
    EXPECT_TRUE(f.isService);
    EXPECT_EQ(f.dir, XcpFrame::Direction::Service);
}

// ── Empty input ───────────────────────────────────────────────────────────────

TEST(XcpParser, EmptyInput) {
    XcpFrame f = XcpParser::parse({});
    EXPECT_EQ(f.pid, 0u);
    EXPECT_EQ(f.dir, XcpFrame::Direction::Command);
}

// ── isCommand ─────────────────────────────────────────────────────────────────

TEST(XcpParser, IsCommandTrue) {
    // All 0xC0-0xFF are command PIDs (same bytes used as response markers too, direction disambiguates)
    EXPECT_TRUE(XcpParser::isCommand(XcpCmd::Connect));        // 0xFF = CONNECT
    EXPECT_TRUE(XcpParser::isCommand(XcpCmd::Disconnect));     // 0xFE = DISCONNECT
    EXPECT_TRUE(XcpParser::isCommand(XcpCmd::GetStatus));      // 0xFD = GET_STATUS
    EXPECT_TRUE(XcpParser::isCommand(0xC0));                   // TRANSPORT_LAYER
    EXPECT_TRUE(XcpParser::isCommand(0xFB));                   // GET_COMM_MODE_INFO
    EXPECT_TRUE(XcpParser::isCommand(XcpCmd::Download));       // 0xF0
    EXPECT_TRUE(XcpParser::isCommand(XcpCmd::GetDaqListInfo)); // 0xD9
}

TEST(XcpParser, IsCommandFalse) {
    EXPECT_FALSE(XcpParser::isCommand(0x00)); // below command range
    EXPECT_FALSE(XcpParser::isCommand(0xBF)); // one below command range
    EXPECT_FALSE(XcpParser::isCommand(0x7F)); // mid range, not XCP command
}

// ── commandName ───────────────────────────────────────────────────────────────

TEST(XcpParser, CommandNameKnown) {
    EXPECT_NE(XcpParser::commandName(XcpCmd::Connect).find("CONNECT"),  std::string::npos);
    EXPECT_EQ(XcpParser::commandName(XcpCmd::Download),  "DOWNLOAD");
    EXPECT_EQ(XcpParser::commandName(XcpCmd::SetMta),    "SET_MTA");
    EXPECT_NE(XcpParser::commandName(0xFF).find("CONNECT"), std::string::npos);
    EXPECT_NE(XcpParser::commandName(0xFE).find("DISCONNECT"), std::string::npos);
}

TEST(XcpParser, CommandNameUnknown) {
    std::string name = XcpParser::commandName(0x01);
    EXPECT_NE(name.find("CMD_0x01"), std::string::npos);
}

// ── errorDescription ──────────────────────────────────────────────────────────

TEST(XcpParser, ErrorDescriptions) {
    EXPECT_NE(XcpParser::errorDescription(XcpErr::CmdBusy).find("busy"), std::string::npos);
    // AccessDenied (0x24) = "not accessible"; AccessLocked (0x25) = "Access denied"
    EXPECT_NE(XcpParser::errorDescription(XcpErr::AccessDenied).find("accessible"), std::string::npos);
    EXPECT_NE(XcpParser::errorDescription(XcpErr::AccessLocked).find("denied"), std::string::npos);
    EXPECT_NE(XcpParser::errorDescription(XcpErr::CmdUnknown).find("Unknown"), std::string::npos);
    EXPECT_NE(XcpParser::errorDescription(0x99).find("Unknown"), std::string::npos);
}
