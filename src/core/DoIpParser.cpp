#include "DoIpParser.h"
#include <cstring>
#include <iomanip>
#include <sstream>

// ── Big-endian helpers ────────────────────────────────────────────────────────

static uint16_t u16be(const uint8_t *p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

static uint32_t u32be(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16)
         | (static_cast<uint32_t>(p[2]) <<  8) |  static_cast<uint32_t>(p[3]);
}

static std::string hexByte(uint8_t v) {
    std::ostringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (int)v;
    return ss.str();
}

// ── Public API ────────────────────────────────────────────────────────────────

DoIpMessage DoIpParser::parse(const std::vector<uint8_t> &data) {
    return parse(data.data(), data.size());
}

DoIpMessage DoIpParser::parse(const uint8_t *data, size_t len) {
    DoIpMessage msg;

    if (len < DOIP_HEADER_SIZE) {
        msg.error = "Too short: " + std::to_string(len) + " bytes (min 8)";
        return msg;
    }

    msg.header.protocolVersion     = data[0];
    msg.header.inverseProtoVersion = data[1];
    msg.header.payloadType         = u16be(data + 2);
    msg.header.payloadLength       = u32be(data + 4);
    msg.header.validPattern        = ((msg.header.protocolVersion ^ msg.header.inverseProtoVersion) == 0xFF);

    if (!msg.header.validPattern) {
        msg.error = "Invalid DoIP header pattern (byte0 XOR byte1 != 0xFF)";
        return msg;
    }

    uint64_t totalExpected = static_cast<uint64_t>(DOIP_HEADER_SIZE) + msg.header.payloadLength;
    if (len < totalExpected) {
        msg.error = "Truncated: expected " + std::to_string(totalExpected)
                  + " bytes, got " + std::to_string(len);
        return msg;
    }

    // Copy raw payload
    msg.rawPayload.assign(data + DOIP_HEADER_SIZE,
                          data + DOIP_HEADER_SIZE + msg.header.payloadLength);

    msg.type  = classifyType(msg.header.payloadType);
    msg.valid = true;

    // Decode payload based on type
    switch (msg.type) {
        case DoIpPayloadType::VehicleAnnouncement:
            decodeVehicleAnnouncement(msg);
            break;
        case DoIpPayloadType::RoutingActivationReq:
            decodeRoutingActivationReq(msg);
            break;
        case DoIpPayloadType::RoutingActivationResp:
            decodeRoutingActivationResp(msg);
            break;
        case DoIpPayloadType::DiagnosticMessage:
            decodeDiagnosticMessage(msg);
            break;
        case DoIpPayloadType::DiagnosticMessageAck:
            decodeDiagnosticAck(msg, false);
            break;
        case DoIpPayloadType::DiagnosticMessageNack:
            decodeDiagnosticAck(msg, true);
            break;
        default:
            break;
    }

    return msg;
}

// ── Payload decoders ──────────────────────────────────────────────────────────

void DoIpParser::decodeVehicleAnnouncement(DoIpMessage &msg) {
    // VIN(17) + logicalAddr(2) + EID(6) + GID(6) + furtherAction(1) + [syncStatus(1)]
    const auto &p = msg.rawPayload;
    if (p.size() < 32) return; // 17+2+6+6+1 = 32 minimum

    msg.vehicleAnnouncement.vin.assign(p.begin(), p.begin() + 17);
    msg.vehicleAnnouncement.logicalAddr = u16be(p.data() + 17);
    std::memcpy(msg.vehicleAnnouncement.eid, p.data() + 19, 6);
    std::memcpy(msg.vehicleAnnouncement.gid, p.data() + 25, 6);
    msg.vehicleAnnouncement.furtherAction = p[31];
    if (p.size() >= 33) {
        msg.vehicleAnnouncement.syncStatus    = p[32];
        msg.vehicleAnnouncement.hasSyncStatus = true;
    }
}

void DoIpParser::decodeRoutingActivationReq(DoIpMessage &msg) {
    // sourceAddr(2) + activationType(1) [+ reserved(4)]
    const auto &p = msg.rawPayload;
    if (p.size() < 3) return;
    msg.routingReq.sourceAddr     = u16be(p.data());
    msg.routingReq.activationType = p[2];
}

void DoIpParser::decodeRoutingActivationResp(DoIpMessage &msg) {
    // clientLogicalAddr(2) + entityLogicalAddr(2) + responseCode(1) [+ reserved(4+4)]
    const auto &p = msg.rawPayload;
    if (p.size() < 5) return;
    msg.routingResp.clientLogicalAddr = u16be(p.data());
    msg.routingResp.entityLogicalAddr = u16be(p.data() + 2);
    msg.routingResp.responseCode      = p[4];
}

void DoIpParser::decodeDiagnosticMessage(DoIpMessage &msg) {
    // sourceAddr(2) + targetAddr(2) + udsPayload(N)
    const auto &p = msg.rawPayload;
    if (p.size() < 4) return;
    msg.diagMsg.sourceAddr = u16be(p.data());
    msg.diagMsg.targetAddr = u16be(p.data() + 2);
    if (p.size() > 4)
        msg.diagMsg.udsPayload.assign(p.begin() + 4, p.end());
}

void DoIpParser::decodeDiagnosticAck(DoIpMessage &msg, bool /*isNack*/) {
    // sourceAddr(2) + targetAddr(2) + ackCode(1) [+ prevDiagMsg...]
    const auto &p = msg.rawPayload;
    if (p.size() < 5) return;
    msg.diagAck.sourceAddr = u16be(p.data());
    msg.diagAck.targetAddr = u16be(p.data() + 2);
    msg.diagAck.ackCode    = p[4];
}

// ── Classification & name helpers ─────────────────────────────────────────────

DoIpPayloadType DoIpParser::classifyType(uint16_t raw) {
    switch (raw) {
        case 0x0000: return DoIpPayloadType::GenericNack;
        case 0x0001: return DoIpPayloadType::VehicleIdentReq;
        case 0x0002: return DoIpPayloadType::VehicleIdentReqVin;
        case 0x0003: return DoIpPayloadType::VehicleIdentReqEid;
        case 0x0004: return DoIpPayloadType::VehicleAnnouncement;
        case 0x0005: return DoIpPayloadType::RoutingActivationReq;
        case 0x0006: return DoIpPayloadType::RoutingActivationResp;
        case 0x0007: return DoIpPayloadType::AliveCheckReq;
        case 0x0008: return DoIpPayloadType::AliveCheckResp;
        case 0x4001: return DoIpPayloadType::DoipEntityStatusReq;
        case 0x4002: return DoIpPayloadType::DoipEntityStatusResp;
        case 0x4003: return DoIpPayloadType::DiagPowerModeInfoReq;
        case 0x4004: return DoIpPayloadType::DiagPowerModeInfoResp;
        case 0x8001: return DoIpPayloadType::DiagnosticMessage;
        case 0x8002: return DoIpPayloadType::DiagnosticMessageAck;
        case 0x8003: return DoIpPayloadType::DiagnosticMessageNack;
        default:     return DoIpPayloadType::Unknown;
    }
}

std::string DoIpParser::payloadTypeName(uint16_t type) {
    switch (type) {
        case 0x0000: return "Generic NACK";
        case 0x0001: return "Vehicle Ident Request";
        case 0x0002: return "Vehicle Ident Request (VIN)";
        case 0x0003: return "Vehicle Ident Request (EID)";
        case 0x0004: return "Vehicle Announcement / Ident Response";
        case 0x0005: return "Routing Activation Request";
        case 0x0006: return "Routing Activation Response";
        case 0x0007: return "Alive Check Request";
        case 0x0008: return "Alive Check Response";
        case 0x4001: return "DoIP Entity Status Request";
        case 0x4002: return "DoIP Entity Status Response";
        case 0x4003: return "Diag Power Mode Info Request";
        case 0x4004: return "Diag Power Mode Info Response";
        case 0x8001: return "Diagnostic Message (UDS)";
        case 0x8002: return "Diagnostic Message ACK";
        case 0x8003: return "Diagnostic Message NACK";
        default:     return "Unknown (0x" + hexByte(type >> 8) + hexByte(type & 0xFF) + ")";
    }
}

std::string DoIpParser::nackCodeName(uint8_t code) {
    switch (code) {
        case 0x00: return "Incorrect pattern format";
        case 0x01: return "Unknown payload type";
        case 0x02: return "Message too large";
        case 0x03: return "Out of memory";
        case 0x04: return "Invalid payload length";
        default:   return "Reserved (0x" + hexByte(code) + ")";
    }
}

std::string DoIpParser::routingResponseCodeName(uint8_t code) {
    switch (code) {
        case 0x00: return "Denied: unknown source address";
        case 0x01: return "Denied: all sockets active";
        case 0x02: return "Denied: SA already active (socket)";
        case 0x03: return "Denied: SA already active (entity)";
        case 0x04: return "Denied: authentication required";
        case 0x05: return "Denied: confirmation required";
        case 0x06: return "Denied: unsupported activation type";
        case 0x10: return "Success: confirmation required";
        case 0x11: return "Success";
        default:   return "Reserved (0x" + hexByte(code) + ")";
    }
}

std::string DoIpParser::diagAckCodeName(uint8_t code) {
    switch (code) {
        case 0x00: return "ACK";
        case 0x02: return "NACK: invalid source address";
        case 0x03: return "NACK: unknown target address";
        case 0x04: return "NACK: diagnostic message too large";
        case 0x05: return "NACK: out of memory";
        case 0x06: return "NACK: target unreachable";
        case 0x07: return "NACK: unknown network";
        case 0x08: return "NACK: transport protocol error";
        default:   return "0x" + hexByte(code);
    }
}
