#include "SomeIpParser.h"
#include <iomanip>
#include <sstream>
#include <string>

// ── Big-endian readers ────────────────────────────────────────────────────────

static uint16_t u16be(const uint8_t *p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

static uint32_t u32be(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24)
         | (static_cast<uint32_t>(p[1]) << 16)
         | (static_cast<uint32_t>(p[2]) <<  8)
         |  static_cast<uint32_t>(p[3]);
}

static uint32_t u24be(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 16)
         | (static_cast<uint32_t>(p[1]) <<  8)
         |  static_cast<uint32_t>(p[2]);
}

static std::string hexByte(uint8_t v) {
    std::ostringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (int)v;
    return ss.str();
}

// ── Public API ────────────────────────────────────────────────────────────────

bool SomeIpParser::isServiceDiscovery(uint16_t serviceId, uint16_t methodId) {
    return serviceId == SOMEIP_SD_SERVICE_ID && methodId == SOMEIP_SD_METHOD_ID;
}

bool SomeIpParser::isMagicCookie(uint16_t serviceId) {
    return serviceId == SOMEIP_MAGIC_SERVICE_ID;
}

SomeIpMessage SomeIpParser::parse(const std::vector<uint8_t> &data) {
    return parse(data.data(), data.size());
}

SomeIpMessage SomeIpParser::parse(const uint8_t *data, size_t len) {
    SomeIpMessage msg;

    if (len < SOMEIP_HEADER_SIZE) {
        msg.error = "Too short: " + std::to_string(len) + " bytes (min 16)";
        return msg;
    }

    msg.header.serviceId        = u16be(data + 0);
    msg.header.methodId         = u16be(data + 2);
    msg.header.length           = u32be(data + 4);
    msg.header.clientId         = u16be(data + 8);
    msg.header.sessionId        = u16be(data + 10);
    msg.header.protocolVersion  = data[12];
    msg.header.interfaceVersion = data[13];
    msg.header.messageType      = data[14];
    msg.header.returnCode       = data[15];

    // length field covers clientId..end, so total wire size = 8 + length
    if (msg.header.length < SOMEIP_MIN_LENGTH_FIELD) {
        msg.error = "Length field " + std::to_string(msg.header.length) + " < 8 (invalid)";
        return msg;
    }

    uint32_t totalExpected = 8u + msg.header.length;
    if (static_cast<uint32_t>(len) < totalExpected) {
        msg.error = "Truncated: expected " + std::to_string(totalExpected)
                  + " bytes, got " + std::to_string(len);
        return msg;
    }

    size_t payloadLen = msg.header.length - SOMEIP_MIN_LENGTH_FIELD;
    msg.payload.assign(data + 16, data + 16 + payloadLen);

    msg.isServiceDiscovery = isServiceDiscovery(msg.header.serviceId, msg.header.methodId);
    if (msg.isServiceDiscovery)
        parseSdPayload(msg);

    msg.valid = true;
    return msg;
}

// ── SOME/IP-SD payload ────────────────────────────────────────────────────────

void SomeIpParser::parseSdPayload(SomeIpMessage &msg) {
    // SD payload layout:
    //   4 bytes  flags + reserved
    //   4 bytes  entries array length (bytes)
    //   N bytes  entries (16 bytes each)
    //   4 bytes  options array length
    //   M bytes  options (not decoded here)
    const auto &p = msg.payload;
    if (p.size() < 12) return;

    uint32_t entriesLen = u32be(p.data() + 4);
    size_t   entryStart = 8;

    if (entryStart + entriesLen > p.size()) return;

    for (size_t off = entryStart; off + 16 <= entryStart + entriesLen; off += 16) {
        SomeIpSdEntry e;
        e.type          = p[off + 0];
        e.indexFirstOpt = p[off + 1];
        e.indexSecondOpt= p[off + 2];
        e.numFirstOpts  = (p[off + 3] >> 4) & 0x0F;
        e.numSecondOpts =  p[off + 3] & 0x0F;
        e.serviceId     = u16be(p.data() + off + 4);
        e.instanceId    = u16be(p.data() + off + 6);
        e.majorVersion  = p[off + 8];
        e.ttl           = u24be(p.data() + off + 9);
        e.minorVersion  = u32be(p.data() + off + 12);
        msg.sdEntries.push_back(e);
    }
}

// ── Name helpers ──────────────────────────────────────────────────────────────

std::string SomeIpParser::messageTypeName(uint8_t type) {
    switch (type) {
        case 0x00: return "REQUEST";
        case 0x01: return "REQUEST_NO_RETURN";
        case 0x02: return "NOTIFICATION";
        case 0x40: return "REQUEST_ACK";
        case 0x80: return "RESPONSE";
        case 0x81: return "ERROR";
        default:   return "UNKNOWN(0x" + hexByte(type) + ")";
    }
}

std::string SomeIpParser::returnCodeName(uint8_t code) {
    switch (code) {
        case 0x00: return "E_OK";
        case 0x01: return "E_NOT_OK";
        case 0x02: return "E_UNKNOWN_SERVICE";
        case 0x03: return "E_UNKNOWN_METHOD";
        case 0x04: return "E_NOT_READY";
        case 0x05: return "E_NOT_REACHABLE";
        case 0x06: return "E_TIMEOUT";
        case 0x07: return "E_WRONG_PROTOCOL_VERSION";
        case 0x08: return "E_WRONG_INTERFACE_VERSION";
        case 0x09: return "E_MALFORMED_MESSAGE";
        case 0x0A: return "E_WRONG_MESSAGE_TYPE";
        default:   return "0x" + hexByte(code);
    }
}

std::string SomeIpParser::sdEntryTypeName(uint8_t type) {
    switch (type) {
        case 0x00: return "FindService";
        case 0x01: return "OfferService";
        case 0x06: return "SubscribeEventgroup";
        case 0x07: return "SubscribeEventgroupAck";
        default:   return "Unknown(0x" + hexByte(type) + ")";
    }
}
