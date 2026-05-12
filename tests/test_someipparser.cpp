#include <gtest/gtest.h>
#include "core/SomeIpParser.h"

// ── Helper — build a raw SOME/IP message ──────────────────────────────────────

static std::vector<uint8_t> makeMsg(
    uint16_t svcId, uint16_t mthId, uint32_t length,
    uint16_t clientId, uint16_t sessionId,
    uint8_t pv, uint8_t iv, uint8_t mt, uint8_t rc,
    const std::vector<uint8_t> &payload = {})
{
    std::vector<uint8_t> buf(16 + payload.size());
    buf[0] = svcId >> 8;   buf[1] = svcId & 0xFF;
    buf[2] = mthId >> 8;   buf[3] = mthId & 0xFF;
    buf[4] = length >> 24; buf[5] = (length >> 16) & 0xFF;
    buf[6] = (length >> 8) & 0xFF; buf[7] = length & 0xFF;
    buf[8] = clientId >> 8; buf[9] = clientId & 0xFF;
    buf[10] = sessionId >> 8; buf[11] = sessionId & 0xFF;
    buf[12] = pv; buf[13] = iv; buf[14] = mt; buf[15] = rc;
    std::copy(payload.begin(), payload.end(), buf.begin() + 16);
    return buf;
}

// ── Basic parsing ─────────────────────────────────────────────────────────────

TEST(SomeIpParser, TooShortReturnsInvalid) {
    std::vector<uint8_t> data(10, 0x00);
    auto msg = SomeIpParser::parse(data);
    EXPECT_FALSE(msg.valid);
    EXPECT_FALSE(msg.error.empty());
}

TEST(SomeIpParser, BasicRequestParse) {
    auto data = makeMsg(0x1234, 0x5678, 8, 0x0001, 0x0001, 1, 1, 0x00, 0x00);
    auto msg = SomeIpParser::parse(data);
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.header.serviceId, 0x1234);
    EXPECT_EQ(msg.header.methodId,  0x5678);
    EXPECT_EQ(msg.header.clientId,  0x0001);
    EXPECT_EQ(msg.header.sessionId, 0x0001);
    EXPECT_EQ(msg.header.messageType, 0x00);
    EXPECT_EQ(msg.header.returnCode,  0x00);
    EXPECT_TRUE(msg.payload.empty());
    EXPECT_FALSE(msg.isServiceDiscovery);
}

TEST(SomeIpParser, WithPayload) {
    std::vector<uint8_t> pl = {0xDE, 0xAD, 0xBE, 0xEF};
    auto data = makeMsg(0x0100, 0x0001, 12, 0x0002, 0x0003, 1, 2, 0x80, 0x00, pl);
    auto msg = SomeIpParser::parse(data);
    EXPECT_TRUE(msg.valid);
    ASSERT_EQ(msg.payload.size(), 4u);
    EXPECT_EQ(msg.payload[0], 0xDE);
    EXPECT_EQ(msg.payload[3], 0xEF);
    EXPECT_EQ(msg.header.length, 12u);
}

TEST(SomeIpParser, TruncatedPayloadReturnsInvalid) {
    // length=20 claims 28 total bytes, but buffer has only 16
    auto data = makeMsg(0x0100, 0x0001, 20, 0x0001, 0x0001, 1, 1, 0x00, 0x00);
    auto msg = SomeIpParser::parse(data);
    EXPECT_FALSE(msg.valid);
    EXPECT_FALSE(msg.error.empty());
}

TEST(SomeIpParser, InvalidLengthFieldReturnsInvalid) {
    // length < 8 is illegal
    auto data = makeMsg(0x0100, 0x0001, 4, 0x0001, 0x0001, 1, 1, 0x00, 0x00);
    auto msg = SomeIpParser::parse(data);
    EXPECT_FALSE(msg.valid);
}

TEST(SomeIpParser, ParseFromRawPointer) {
    uint8_t raw[] = {
        0x00, 0x01,             // serviceId
        0x00, 0x02,             // methodId
        0x00, 0x00, 0x00, 0x08, // length = 8
        0x00, 0x01,             // clientId
        0x00, 0x01,             // sessionId
        0x01, 0x01, 0x00, 0x00  // pv, iv, mt, rc
    };
    auto msg = SomeIpParser::parse(raw, sizeof(raw));
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.header.serviceId, 0x0001);
    EXPECT_EQ(msg.header.methodId,  0x0002);
}

// ── Service Discovery ─────────────────────────────────────────────────────────

TEST(SomeIpParser, ServiceDiscoveryDetectedEmptyEntries) {
    // Minimal SD: 4 flags + 4 entries-len=0 + 4 options-len=0
    std::vector<uint8_t> sdPayload = {
        0x00, 0x00, 0x00, 0x00,  // flags
        0x00, 0x00, 0x00, 0x00,  // entries array len = 0
        0x00, 0x00, 0x00, 0x00,  // options array len = 0
    };
    auto data = makeMsg(0xFFFF, 0x8100, 8 + 12, 0x0000, 0x0001, 1, 1, 0x02, 0x00, sdPayload);
    auto msg = SomeIpParser::parse(data);
    EXPECT_TRUE(msg.valid);
    EXPECT_TRUE(msg.isServiceDiscovery);
    EXPECT_TRUE(msg.sdEntries.empty());
}

TEST(SomeIpParser, ServiceDiscoveryWithOfferEntry) {
    std::vector<uint8_t> entry(16, 0x00);
    entry[0] = 0x01;                        // OfferService
    entry[4] = 0x12; entry[5] = 0x34;       // serviceId = 0x1234
    entry[6] = 0x00; entry[7] = 0x01;       // instanceId = 0x0001
    entry[8] = 0x02;                         // majorVersion = 2
    entry[9] = 0x00; entry[10] = 0x00; entry[11] = 0x0A; // TTL = 10
    entry[12] = 0x00; entry[13] = 0x00; entry[14] = 0x00; entry[15] = 0x03; // minorVersion = 3

    std::vector<uint8_t> sdPayload;
    sdPayload.insert(sdPayload.end(), {0x00, 0x00, 0x00, 0x00});         // flags
    sdPayload.insert(sdPayload.end(), {0x00, 0x00, 0x00, 0x10});         // entries len = 16
    sdPayload.insert(sdPayload.end(), entry.begin(), entry.end());
    sdPayload.insert(sdPayload.end(), {0x00, 0x00, 0x00, 0x00});         // options len = 0

    auto data = makeMsg(0xFFFF, 0x8100, 8 + (uint32_t)sdPayload.size(),
                        0x0000, 0x0001, 1, 1, 0x02, 0x00, sdPayload);
    auto msg = SomeIpParser::parse(data);
    EXPECT_TRUE(msg.valid);
    EXPECT_TRUE(msg.isServiceDiscovery);
    ASSERT_EQ(msg.sdEntries.size(), 1u);
    EXPECT_EQ(msg.sdEntries[0].type,         0x01);
    EXPECT_EQ(msg.sdEntries[0].serviceId,    0x1234);
    EXPECT_EQ(msg.sdEntries[0].instanceId,   0x0001);
    EXPECT_EQ(msg.sdEntries[0].majorVersion, 2);
    EXPECT_EQ(msg.sdEntries[0].ttl,         10u);
    EXPECT_EQ(msg.sdEntries[0].minorVersion,  3u);
}

TEST(SomeIpParser, ServiceDiscoveryTwoEntries) {
    auto makeEntry = [](uint8_t type, uint16_t svc) {
        std::vector<uint8_t> e(16, 0x00);
        e[0] = type;
        e[4] = svc >> 8; e[5] = svc & 0xFF;
        e[9] = 0x00; e[10] = 0x00; e[11] = 0x05; // TTL=5
        return e;
    };

    auto e1 = makeEntry(0x00, 0xAAAA); // FindService
    auto e2 = makeEntry(0x01, 0xBBBB); // OfferService

    std::vector<uint8_t> sdPayload;
    sdPayload.insert(sdPayload.end(), {0x00, 0x00, 0x00, 0x00});
    sdPayload.insert(sdPayload.end(), {0x00, 0x00, 0x00, 0x20}); // 32 bytes
    sdPayload.insert(sdPayload.end(), e1.begin(), e1.end());
    sdPayload.insert(sdPayload.end(), e2.begin(), e2.end());
    sdPayload.insert(sdPayload.end(), {0x00, 0x00, 0x00, 0x00});

    auto data = makeMsg(0xFFFF, 0x8100, 8 + (uint32_t)sdPayload.size(),
                        0x0000, 0x0001, 1, 1, 0x02, 0x00, sdPayload);
    auto msg = SomeIpParser::parse(data);
    EXPECT_TRUE(msg.valid);
    ASSERT_EQ(msg.sdEntries.size(), 2u);
    EXPECT_EQ(msg.sdEntries[0].serviceId, 0xAAAA);
    EXPECT_EQ(msg.sdEntries[1].serviceId, 0xBBBB);
    EXPECT_EQ(msg.sdEntries[0].type, 0x00);
    EXPECT_EQ(msg.sdEntries[1].type, 0x01);
}

// ── Classification helpers ────────────────────────────────────────────────────

TEST(SomeIpParser, IsServiceDiscovery) {
    EXPECT_TRUE( SomeIpParser::isServiceDiscovery(0xFFFF, 0x8100));
    EXPECT_FALSE(SomeIpParser::isServiceDiscovery(0xFFFF, 0x0001));
    EXPECT_FALSE(SomeIpParser::isServiceDiscovery(0x1234, 0x8100));
    EXPECT_FALSE(SomeIpParser::isServiceDiscovery(0x0000, 0x0000));
}

TEST(SomeIpParser, IsMagicCookie) {
    EXPECT_TRUE( SomeIpParser::isMagicCookie(0xDEAD));
    EXPECT_FALSE(SomeIpParser::isMagicCookie(0xFFFF));
    EXPECT_FALSE(SomeIpParser::isMagicCookie(0x1234));
}

// ── Name helpers ──────────────────────────────────────────────────────────────

TEST(SomeIpParser, MessageTypeNames) {
    EXPECT_EQ(SomeIpParser::messageTypeName(0x00), "REQUEST");
    EXPECT_EQ(SomeIpParser::messageTypeName(0x01), "REQUEST_NO_RETURN");
    EXPECT_EQ(SomeIpParser::messageTypeName(0x02), "NOTIFICATION");
    EXPECT_EQ(SomeIpParser::messageTypeName(0x40), "REQUEST_ACK");
    EXPECT_EQ(SomeIpParser::messageTypeName(0x80), "RESPONSE");
    EXPECT_EQ(SomeIpParser::messageTypeName(0x81), "ERROR");
    // Unknown should not be empty
    EXPECT_FALSE(SomeIpParser::messageTypeName(0xFF).empty());
}

TEST(SomeIpParser, ReturnCodeNames) {
    EXPECT_EQ(SomeIpParser::returnCodeName(0x00), "E_OK");
    EXPECT_EQ(SomeIpParser::returnCodeName(0x01), "E_NOT_OK");
    EXPECT_EQ(SomeIpParser::returnCodeName(0x02), "E_UNKNOWN_SERVICE");
    EXPECT_EQ(SomeIpParser::returnCodeName(0x03), "E_UNKNOWN_METHOD");
    EXPECT_EQ(SomeIpParser::returnCodeName(0x09), "E_MALFORMED_MESSAGE");
    EXPECT_EQ(SomeIpParser::returnCodeName(0x0A), "E_WRONG_MESSAGE_TYPE");
    EXPECT_FALSE(SomeIpParser::returnCodeName(0xFF).empty());
}

TEST(SomeIpParser, SdEntryTypeNames) {
    EXPECT_EQ(SomeIpParser::sdEntryTypeName(0x00), "FindService");
    EXPECT_EQ(SomeIpParser::sdEntryTypeName(0x01), "OfferService");
    EXPECT_EQ(SomeIpParser::sdEntryTypeName(0x06), "SubscribeEventgroup");
    EXPECT_EQ(SomeIpParser::sdEntryTypeName(0x07), "SubscribeEventgroupAck");
    EXPECT_FALSE(SomeIpParser::sdEntryTypeName(0xFF).empty());
}

// ── Exact byte values ─────────────────────────────────────────────────────────

TEST(SomeIpParser, ProtocolAndInterfaceVersionPreserved) {
    auto data = makeMsg(0x0100, 0x0200, 8, 0x0005, 0x0007, 0x03, 0x07, 0x80, 0x00);
    auto msg = SomeIpParser::parse(data);
    EXPECT_TRUE(msg.valid);
    EXPECT_EQ(msg.header.protocolVersion,  0x03);
    EXPECT_EQ(msg.header.interfaceVersion, 0x07);
    EXPECT_EQ(msg.header.clientId,         0x0005);
    EXPECT_EQ(msg.header.sessionId,        0x0007);
}

TEST(SomeIpParser, EmptyInputReturnsInvalid) {
    std::vector<uint8_t> empty;
    auto msg = SomeIpParser::parse(empty);
    EXPECT_FALSE(msg.valid);
}
