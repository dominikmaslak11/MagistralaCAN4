#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ISO 13400-2:2019 — Diagnostics over IP (DoIP)

static constexpr uint8_t  DOIP_PROTO_VERSION    = 0x02; // ISO 13400-2:2019
static constexpr uint8_t  DOIP_PROTO_VERSION_INV= 0xFD; // ~0x02
static constexpr uint32_t DOIP_HEADER_SIZE      = 8;

// DoIP payload types (2 bytes, big-endian)
enum class DoIpPayloadType : uint16_t {
    GenericNack              = 0x0000,
    VehicleIdentReq          = 0x0001,
    VehicleIdentReqVin       = 0x0002,
    VehicleIdentReqEid       = 0x0003,
    VehicleAnnouncement      = 0x0004,  // also VehicleIdentificationResponse
    RoutingActivationReq     = 0x0005,
    RoutingActivationResp    = 0x0006,
    AliveCheckReq            = 0x0007,
    AliveCheckResp           = 0x0008,
    DoipEntityStatusReq      = 0x4001,
    DoipEntityStatusResp     = 0x4002,
    DiagPowerModeInfoReq     = 0x4003,
    DiagPowerModeInfoResp    = 0x4004,
    DiagnosticMessage        = 0x8001,
    DiagnosticMessageAck     = 0x8002,
    DiagnosticMessageNack    = 0x8003,
    Unknown                  = 0xFFFF,
};

// Generic NACK codes
enum class DoIpNackCode : uint8_t {
    IncorrectPatternFormat  = 0x00,
    UnknownPayloadType      = 0x01,
    MessageTooLarge         = 0x02,
    OutOfMemory             = 0x03,
    InvalidPayloadLength    = 0x04,
};

// Routing activation response codes
enum class DoIpRoutingActivationCode : uint8_t {
    DeniedUnknownSourceAddr  = 0x00,
    DeniedAllSocketsActive   = 0x01,
    DeniedSaAlreadyActSock   = 0x02,
    DeniedSaAlreadyActEntity = 0x03,
    DeniedAuthentRequired    = 0x04,
    DeniedConfirmRequired    = 0x05,
    DeniedUnsupportedType    = 0x06,
    SuccessConfirmRequired   = 0x10,
    Success                  = 0x11,
};

struct DoIpHeader {
    uint8_t  protocolVersion     = 0;
    uint8_t  inverseProtoVersion = 0;
    uint16_t payloadType         = 0;
    uint32_t payloadLength       = 0;
    bool     validPattern        = false; // version XOR inverse == 0xFF
};

// Vehicle announcement (VehicleIdentificationResponse)
struct DoIpVehicleAnnouncement {
    std::string vin;         // 17 ASCII chars
    uint16_t    logicalAddr  = 0;
    uint8_t     eid[6]       = {};  // EID (MAC)
    uint8_t     gid[6]       = {};  // GID (group)
    uint8_t     furtherAction= 0;
    uint8_t     syncStatus   = 0;
    bool        hasSyncStatus= false;
};

// Routing activation request
struct DoIpRoutingActivationRequest {
    uint16_t sourceAddr    = 0;
    uint8_t  activationType= 0;
};

// Routing activation response
struct DoIpRoutingActivationResponse {
    uint16_t clientLogicalAddr = 0;
    uint16_t entityLogicalAddr = 0;
    uint8_t  responseCode      = 0;
};

// Diagnostic message (UDS over DoIP)
struct DoIpDiagnosticMessage {
    uint16_t             sourceAddr  = 0;
    uint16_t             targetAddr  = 0;
    std::vector<uint8_t> udsPayload;
};

// Diagnostic message ACK/NACK
struct DoIpDiagnosticAck {
    uint16_t sourceAddr = 0;
    uint16_t targetAddr = 0;
    uint8_t  ackCode    = 0;  // 0x00 = ACK; NACK codes 0x02..0x0F
};

struct DoIpMessage {
    DoIpHeader                  header;
    DoIpPayloadType             type = DoIpPayloadType::Unknown;
    std::vector<uint8_t>        rawPayload;
    // Decoded variants (only one is valid depending on type)
    DoIpVehicleAnnouncement     vehicleAnnouncement;
    DoIpRoutingActivationRequest routingReq;
    DoIpRoutingActivationResponse routingResp;
    DoIpDiagnosticMessage       diagMsg;
    DoIpDiagnosticAck           diagAck;
    std::string                 error;
    bool                        valid = false;
};
