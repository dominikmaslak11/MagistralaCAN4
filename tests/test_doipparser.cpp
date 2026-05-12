#include <gtest/gtest.h>
#include "core/DoIpParser.h"

// ── Helper — build a raw DoIP message ─────────────────────────────────────────

static std::vector<uint8_t> makeDoIp(uint8_t protoV, uint16_t payloadType,
                                      const std::vector<uint8_t> &payload = {})
{
    uint32_t len = static_cast<uint32_t>(payload.size());
    std::vector<uint8_t> buf(8 + payload.size());
    buf[0] = protoV;
    buf[1] = static_cast<uint8_t>(~protoV); // inverse
    buf[2] = payloadType >> 8;
    buf[3] = payloadType & 0xFF;
    buf[4] = (len >> 24) & 0xFF; buf[5] = (len >> 16) & 0xFF;
    buf[6] = (len >>  8) & 0xFF; buf[7] =  len & 0xFF;
    std::copy(payload.begin(), payload.end(), buf.begin() + 8);
    return buf;
}

// ── Header parsing ────────────────────────────────────────────────────────────

TEST(DoIpParser, TooShortReturnsInvalid) {
    std::vector<uint8_t> data(5, 0);
    auto msg = DoIpParser::parse(data);
    EXPECT_FALSE(msg.valid);
    EXPECT_FALSE(msg.error.empty());
}

TEST(DoIpParser, InvalidPatternReturnsInvalid) {
    // byte0=0x02, byte1=0x00 → 0x02 XOR 0x00 != 0xFF
    std::vector<uint8_t> data(8, 0x00);
    data[0] = 0x02; data[1] = 0x00;
    data[2] = 0x00; data[3] = 0x01; // VehicleIdentReq
    auto msg = DoIpParser::parse(data);
    EXPECT_FALSE(msg.valid);
}

TEST(DoIpParser, ValidAliveCheckReq) {
    auto data = makeDoIp(0x02, 0x0007); // AliveCheckReq, no payload
    auto msg = DoIpParser::parse(data);
    EXPECT_TRUE(msg.valid);
    EXPECT_TRUE(msg.header.validPattern);
    EXPECT_EQ(msg.header.protocolVersion, 0x02);
    EXPECT_EQ(msg.header.payloadLength, 0u);
    EXPECT_EQ(msg.type, DoIpPayloadType::AliveCheckReq);
}

TEST(DoIpParser, TruncatedPayloadReturnsInvalid) {
    // Header claims payloadLength=10 but buffer has only 8 bytes
    auto data = makeDoIp(0x02, 0x8001);
    data[7] = 10; // payloadLength = 10 but no actual payload
    auto msg = DoIpParser::parse(data);
    EXPECT_FALSE(msg.valid);
}

TEST(DoIpParser, ParseFromRawPointer) {
    auto data = makeDoIp(0x02, 0x0008); // AliveCheckResp
    auto msg  = DoIpParser::parse(data.data(), data.size());
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.type, DoIpPayloadType::AliveCheckResp);
}

// ── Vehicle Announcement ──────────────────────────────────────────────────────

TEST(DoIpParser, VehicleAnnouncement) {
    std::vector<uint8_t> pl;
    // VIN: "1HGBH41JXMN109186" (17 bytes)
    const char *vin = "1HGBH41JXMN109186";
    for (int i = 0; i < 17; ++i) pl.push_back(static_cast<uint8_t>(vin[i]));
    pl.push_back(0x0E); pl.push_back(0x80); // logicalAddr = 0x0E80
    // EID (MAC)
    pl.insert(pl.end(), {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF});
    // GID
    pl.insert(pl.end(), {0x00, 0x11, 0x22, 0x33, 0x44, 0x55});
    pl.push_back(0x00); // furtherAction

    auto data = makeDoIp(0x02, 0x0004, pl);
    auto msg  = DoIpParser::parse(data);
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.type, DoIpPayloadType::VehicleAnnouncement);
    EXPECT_EQ(msg.vehicleAnnouncement.vin, "1HGBH41JXMN109186");
    EXPECT_EQ(msg.vehicleAnnouncement.logicalAddr, 0x0E80);
    EXPECT_EQ(msg.vehicleAnnouncement.eid[0], 0xAA);
    EXPECT_EQ(msg.vehicleAnnouncement.eid[5], 0xFF);
    EXPECT_FALSE(msg.vehicleAnnouncement.hasSyncStatus);
}

TEST(DoIpParser, VehicleAnnouncementWithSyncStatus) {
    std::vector<uint8_t> pl(33, 0x00);
    pl[32] = 0x10; // syncStatus
    auto data = makeDoIp(0x02, 0x0004, pl);
    auto msg  = DoIpParser::parse(data);
    EXPECT_TRUE(msg.valid);
    EXPECT_TRUE(msg.vehicleAnnouncement.hasSyncStatus);
    EXPECT_EQ(msg.vehicleAnnouncement.syncStatus, 0x10);
}

// ── Routing Activation ────────────────────────────────────────────────────────

TEST(DoIpParser, RoutingActivationRequest) {
    std::vector<uint8_t> pl = {0x0E, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00};
    // sourceAddr=0x0E80, activationType=0x00 (default), 4 reserved bytes
    auto data = makeDoIp(0x02, 0x0005, pl);
    auto msg  = DoIpParser::parse(data);
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.type, DoIpPayloadType::RoutingActivationReq);
    EXPECT_EQ(msg.routingReq.sourceAddr, 0x0E80);
    EXPECT_EQ(msg.routingReq.activationType, 0x00);
}

TEST(DoIpParser, RoutingActivationResponse_Success) {
    // clientAddr(2) + entityAddr(2) + code(1) + reserved(8)
    std::vector<uint8_t> pl = {0x0E, 0x80, 0x00, 0x10, 0x11, 0,0,0,0, 0,0,0,0};
    auto data = makeDoIp(0x02, 0x0006, pl);
    auto msg  = DoIpParser::parse(data);
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.type, DoIpPayloadType::RoutingActivationResp);
    EXPECT_EQ(msg.routingResp.clientLogicalAddr, 0x0E80);
    EXPECT_EQ(msg.routingResp.entityLogicalAddr, 0x0010);
    EXPECT_EQ(msg.routingResp.responseCode, 0x11); // Success
}

// ── Diagnostic Message ────────────────────────────────────────────────────────

TEST(DoIpParser, DiagnosticMessage_UDS) {
    // sourceAddr + targetAddr + UDS SID 0x22 (ReadDataByIdentifier) + DID 0xF186
    std::vector<uint8_t> pl = {0x0E, 0x80, 0x00, 0x10, 0x22, 0xF1, 0x86};
    auto data = makeDoIp(0x02, 0x8001, pl);
    auto msg  = DoIpParser::parse(data);
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.type, DoIpPayloadType::DiagnosticMessage);
    EXPECT_EQ(msg.diagMsg.sourceAddr, 0x0E80);
    EXPECT_EQ(msg.diagMsg.targetAddr, 0x0010);
    ASSERT_EQ(msg.diagMsg.udsPayload.size(), 3u);
    EXPECT_EQ(msg.diagMsg.udsPayload[0], 0x22); // SID ReadDataByIdentifier
    EXPECT_EQ(msg.diagMsg.udsPayload[1], 0xF1);
    EXPECT_EQ(msg.diagMsg.udsPayload[2], 0x86);
}

TEST(DoIpParser, DiagnosticMessageAck) {
    std::vector<uint8_t> pl = {0x0E, 0x80, 0x00, 0x10, 0x00};
    auto data = makeDoIp(0x02, 0x8002, pl);
    auto msg  = DoIpParser::parse(data);
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.type, DoIpPayloadType::DiagnosticMessageAck);
    EXPECT_EQ(msg.diagAck.ackCode, 0x00);
}

TEST(DoIpParser, DiagnosticMessageNack) {
    std::vector<uint8_t> pl = {0x0E, 0x80, 0x00, 0x10, 0x06}; // target unreachable
    auto data = makeDoIp(0x02, 0x8003, pl);
    auto msg  = DoIpParser::parse(data);
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.type, DoIpPayloadType::DiagnosticMessageNack);
    EXPECT_EQ(msg.diagAck.ackCode, 0x06);
}

// ── Classification & name helpers ─────────────────────────────────────────────

TEST(DoIpParser, PayloadTypeNames) {
    EXPECT_EQ(DoIpParser::payloadTypeName(0x0000), "Generic NACK");
    EXPECT_EQ(DoIpParser::payloadTypeName(0x0004), "Vehicle Announcement / Ident Response");
    EXPECT_EQ(DoIpParser::payloadTypeName(0x0005), "Routing Activation Request");
    EXPECT_EQ(DoIpParser::payloadTypeName(0x0006), "Routing Activation Response");
    EXPECT_EQ(DoIpParser::payloadTypeName(0x8001), "Diagnostic Message (UDS)");
    EXPECT_EQ(DoIpParser::payloadTypeName(0x8002), "Diagnostic Message ACK");
    EXPECT_EQ(DoIpParser::payloadTypeName(0x8003), "Diagnostic Message NACK");
    EXPECT_FALSE(DoIpParser::payloadTypeName(0xDEAD).empty());
}

TEST(DoIpParser, RoutingResponseCodeNames) {
    EXPECT_EQ(DoIpParser::routingResponseCodeName(0x11), "Success");
    EXPECT_EQ(DoIpParser::routingResponseCodeName(0x10), "Success: confirmation required");
    EXPECT_EQ(DoIpParser::routingResponseCodeName(0x00), "Denied: unknown source address");
}

TEST(DoIpParser, DiagAckCodeNames) {
    EXPECT_EQ(DoIpParser::diagAckCodeName(0x00), "ACK");
    EXPECT_EQ(DoIpParser::diagAckCodeName(0x06), "NACK: target unreachable");
}

TEST(DoIpParser, ClassifyUnknownType) {
    auto data = makeDoIp(0x02, 0xABCD);
    auto msg  = DoIpParser::parse(data);
    EXPECT_TRUE(msg.valid); // header valid, payload just unknown
    EXPECT_EQ(msg.type, DoIpPayloadType::Unknown);
}

TEST(DoIpParser, EmptyInputReturnsInvalid) {
    std::vector<uint8_t> empty;
    auto msg = DoIpParser::parse(empty);
    EXPECT_FALSE(msg.valid);
}
