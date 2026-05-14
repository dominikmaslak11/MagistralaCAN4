#include <gtest/gtest.h>
#include "core/CanOpenFrame.h"

// ── Helper ───────────────────────────────────────────────────

static CanFrame raw(uint32_t id, std::initializer_list<uint8_t> bytes) {
    CanFrame f{};
    f.id  = id;
    f.dlc = static_cast<uint8_t>(bytes.size());
    int i = 0;
    for (uint8_t b : bytes) f.data[i++] = b;
    return f;
}

// ── isCanOpen ────────────────────────────────────────────────

TEST(CanOpenParserTest, IsCanOpen_NMT) {
    EXPECT_TRUE(CanOpenFrame::isCanOpen(raw(0x000, {0x01, 0x01})));
}

TEST(CanOpenParserTest, IsCanOpen_SYNC) {
    EXPECT_TRUE(CanOpenFrame::isCanOpen(raw(0x080, {})));
}

TEST(CanOpenParserTest, IsCanOpen_EMCY) {
    EXPECT_TRUE(CanOpenFrame::isCanOpen(raw(0x085, {0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00})));
}

TEST(CanOpenParserTest, IsCanOpen_SDO_TX) {
    EXPECT_TRUE(CanOpenFrame::isCanOpen(raw(0x585, {0x60, 0x00, 0x18, 0x01, 0x00})));
}

TEST(CanOpenParserTest, IsCanOpen_SDO_RX) {
    EXPECT_TRUE(CanOpenFrame::isCanOpen(raw(0x605, {0x40, 0x00, 0x18, 0x01, 0x00, 0x00, 0x00, 0x00})));
}

TEST(CanOpenParserTest, IsCanOpen_Heartbeat) {
    EXPECT_TRUE(CanOpenFrame::isCanOpen(raw(0x705, {0x05})));
}

TEST(CanOpenParserTest, IsCanOpen_ExtendedId_False) {
    CanFrame f = raw(0x000, {0x01});
    f.extended = true;
    EXPECT_FALSE(CanOpenFrame::isCanOpen(f));
}

TEST(CanOpenParserTest, IsCanOpen_OutOfRange_False) {
    EXPECT_FALSE(CanOpenFrame::isCanOpen(raw(0x7F8, {0x01})));
}

// ── fromCanFrame — type detection ────────────────────────────

TEST(CanOpenParserTest, FromCanFrame_NMT) {
    auto co = CanOpenFrame::fromCanFrame(raw(0x000, {0x01, 0x05}));
    EXPECT_EQ(co.type, CanOpenFrame::NMT);
    EXPECT_EQ(co.nodeId, 0);
}

TEST(CanOpenParserTest, FromCanFrame_SYNC) {
    auto co = CanOpenFrame::fromCanFrame(raw(0x080, {}));
    EXPECT_EQ(co.type, CanOpenFrame::SYNC);
}

TEST(CanOpenParserTest, FromCanFrame_EMCY_Node5) {
    auto co = CanOpenFrame::fromCanFrame(raw(0x085, {0x00, 0x10, 0, 0, 0, 0, 0}));
    EXPECT_EQ(co.type, CanOpenFrame::EMCY);
    EXPECT_EQ(co.nodeId, 5);
}

TEST(CanOpenParserTest, FromCanFrame_PDO1_TX_Node1) {
    auto co = CanOpenFrame::fromCanFrame(raw(0x181, {0x01, 0x02}));
    EXPECT_EQ(co.type, CanOpenFrame::PDO1_TX);
    EXPECT_EQ(co.nodeId, 1);
}

TEST(CanOpenParserTest, FromCanFrame_PDO1_RX) {
    auto co = CanOpenFrame::fromCanFrame(raw(0x201, {0xFF}));
    EXPECT_EQ(co.type, CanOpenFrame::PDO1_RX);
}

TEST(CanOpenParserTest, FromCanFrame_PDO2_TX) {
    auto co = CanOpenFrame::fromCanFrame(raw(0x281, {}));
    EXPECT_EQ(co.type, CanOpenFrame::PDO2_TX);
}

TEST(CanOpenParserTest, FromCanFrame_PDO3_TX) {
    auto co = CanOpenFrame::fromCanFrame(raw(0x381, {}));
    EXPECT_EQ(co.type, CanOpenFrame::PDO3_TX);
}

TEST(CanOpenParserTest, FromCanFrame_PDO4_RX) {
    auto co = CanOpenFrame::fromCanFrame(raw(0x501, {}));
    EXPECT_EQ(co.type, CanOpenFrame::PDO4_RX);
}

TEST(CanOpenParserTest, FromCanFrame_SDO_TX_Node5) {
    auto co = CanOpenFrame::fromCanFrame(raw(0x585, {0x60, 0x00, 0x18, 0x01, 0x00, 0x00, 0x00, 0x00}));
    EXPECT_EQ(co.type, CanOpenFrame::SDO_TX);
    EXPECT_EQ(co.nodeId, 5);
}

TEST(CanOpenParserTest, FromCanFrame_SDO_RX_Node1) {
    auto co = CanOpenFrame::fromCanFrame(raw(0x601, {0x40, 0x00, 0x18, 0x01}));
    EXPECT_EQ(co.type, CanOpenFrame::SDO_RX);
    EXPECT_EQ(co.nodeId, 1);
}

TEST(CanOpenParserTest, FromCanFrame_Heartbeat_Node5) {
    auto co = CanOpenFrame::fromCanFrame(raw(0x705, {0x05}));
    EXPECT_EQ(co.type, CanOpenFrame::HEARTBEAT);
    EXPECT_EQ(co.nodeId, 5);
}

TEST(CanOpenParserTest, FromCanFrame_Heartbeat_Node127) {
    auto co = CanOpenFrame::fromCanFrame(raw(0x77F, {0x7F}));
    EXPECT_EQ(co.type, CanOpenFrame::HEARTBEAT);
    EXPECT_EQ(co.nodeId, 127);
}

TEST(CanOpenParserTest, FromCanFrame_PreservesTimestamp) {
    CanFrame f = raw(0x080, {});
    f.timestamp = 123456789;
    auto co = CanOpenFrame::fromCanFrame(f);
    EXPECT_EQ(co.timestamp, 123456789ULL);
}

// ── CanOpenParser methods ─────────────────────────────────────

TEST(CanOpenParserTest, TypeName_NMT) {
    CanOpenParser p;
    EXPECT_EQ(p.typeName(CanOpenFrame::NMT), "NMT");
}

TEST(CanOpenParserTest, TypeName_SYNC) {
    CanOpenParser p;
    EXPECT_EQ(p.typeName(CanOpenFrame::SYNC), "SYNC");
}

TEST(CanOpenParserTest, TypeName_Heartbeat) {
    CanOpenParser p;
    EXPECT_EQ(p.typeName(CanOpenFrame::HEARTBEAT), "Heartbeat");
}

TEST(CanOpenParserTest, TypeName_Unknown) {
    CanOpenParser p;
    EXPECT_EQ(p.typeName(CanOpenFrame::Unknown), "???");
}

TEST(CanOpenParserTest, NmtCommand_StartNode) {
    CanOpenParser p;
    EXPECT_EQ(p.nmtCommand(0x01), "Start Remote Node");
}

TEST(CanOpenParserTest, NmtCommand_StopNode) {
    CanOpenParser p;
    EXPECT_EQ(p.nmtCommand(0x02), "Stop Remote Node");
}

TEST(CanOpenParserTest, NmtCommand_ResetNode) {
    CanOpenParser p;
    EXPECT_EQ(p.nmtCommand(0x81), "Reset Node");
}

TEST(CanOpenParserTest, NmtCommand_Unknown_ReturnsHex) {
    CanOpenParser p;
    QString s = p.nmtCommand(0xAA);
    EXPECT_TRUE(s.contains("AA", Qt::CaseInsensitive) || s.contains("NMT"));
}

TEST(CanOpenParserTest, EmcyCode_GenericError) {
    CanOpenParser p;
    EXPECT_EQ(p.emcyCode(0x1000), "Generic Error");
}

TEST(CanOpenParserTest, EmcyCode_Communication) {
    CanOpenParser p;
    EXPECT_EQ(p.emcyCode(0x8100), "Communication");
}

TEST(CanOpenParserTest, EmcyCode_Unknown_ReturnsHex) {
    CanOpenParser p;
    QString s = p.emcyCode(0xFFFF);
    EXPECT_TRUE(s.contains("FFFF", Qt::CaseInsensitive) ||
                s.contains("ffff", Qt::CaseInsensitive));
}

TEST(CanOpenParserTest, SdoCommand_InitiateDownload) {
    CanOpenParser p;
    // CCS=1 (Initiate Download) → cmd byte = 1<<5 = 0x20..0x3F
    EXPECT_EQ(p.sdoCommand(0x20), "Initiate Download");
}

TEST(CanOpenParserTest, SdoCommand_InitiateUpload) {
    CanOpenParser p;
    // CCS=3 (Initiate Upload) → cmd byte = 3<<5 = 0x60..0x7F
    EXPECT_EQ(p.sdoCommand(0x60), "Initiate Upload");
}

TEST(CanOpenParserTest, SdoCommand_AbortTransfer) {
    CanOpenParser p;
    // CCS=4 → cmd byte = 4<<5 = 0x80
    EXPECT_EQ(p.sdoCommand(0x80), "Abort Transfer");
}

TEST(CanOpenParserTest, HeartbeatState_Operational) {
    CanOpenParser p;
    EXPECT_EQ(p.heartbeatState(0x05), "Operational");
}

TEST(CanOpenParserTest, HeartbeatState_Bootup) {
    CanOpenParser p;
    EXPECT_EQ(p.heartbeatState(0x00), "Boot-up");
}

TEST(CanOpenParserTest, HeartbeatState_PreOperational) {
    CanOpenParser p;
    EXPECT_EQ(p.heartbeatState(0x7F), "Pre-operational");
}

TEST(CanOpenParserTest, HeartbeatState_Unknown_ReturnsHex) {
    CanOpenParser p;
    QString s = p.heartbeatState(0x10);
    EXPECT_TRUE(s.contains("10", Qt::CaseInsensitive) ||
                s.contains("State"));
}

// ── toString ─────────────────────────────────────────────────

TEST(CanOpenParserTest, ToString_NMT) {
    auto co = CanOpenFrame::fromCanFrame(raw(0x000, {0x01, 0x05}));
    EXPECT_TRUE(co.toString().contains("NMT"));
}

TEST(CanOpenParserTest, ToString_SYNC) {
    auto co = CanOpenFrame::fromCanFrame(raw(0x080, {}));
    EXPECT_TRUE(co.toString().contains("SYNC"));
}

TEST(CanOpenParserTest, ToString_HeartbeatContainsNodeId) {
    auto co = CanOpenFrame::fromCanFrame(raw(0x705, {0x05}));
    EXPECT_TRUE(co.toString().contains("5"));
}
