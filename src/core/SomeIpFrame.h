#pragma once
#include <cstdint>
#include <string>
#include <vector>

// SOME/IP header constants
static constexpr uint16_t SOMEIP_SD_SERVICE_ID    = 0xFFFF;
static constexpr uint16_t SOMEIP_SD_METHOD_ID     = 0x8100;
static constexpr uint16_t SOMEIP_MAGIC_SERVICE_ID = 0xDEAD;
static constexpr uint32_t SOMEIP_HEADER_SIZE      = 16;
static constexpr uint32_t SOMEIP_MIN_LENGTH_FIELD = 8; // clientId..returnCode

// SOME/IP message types (messageType field)
enum class SomeIpMessageType : uint8_t {
    Request         = 0x00,
    RequestNoReturn = 0x01,
    Notification    = 0x02,
    RequestAck      = 0x40,
    Response        = 0x80,
    Error           = 0x81,
    Unknown         = 0xFF,
};

// SOME/IP return codes
enum class SomeIpReturnCode : uint8_t {
    EOk                 = 0x00,
    ENotOk              = 0x01,
    EUnknownService     = 0x02,
    EUnknownMethod      = 0x03,
    ENotReady           = 0x04,
    ENotReachable       = 0x05,
    ETimeout            = 0x06,
    EWrongProtocolVer   = 0x07,
    EWrongInterfaceVer  = 0x08,
    EMalformedMsg       = 0x09,
    EWrongMessageType   = 0x0A,
};

struct SomeIpHeader {
    uint16_t serviceId        = 0;
    uint16_t methodId         = 0;
    uint32_t length           = 0;   // payload + 8 bytes of IDs/versions
    uint16_t clientId         = 0;
    uint16_t sessionId        = 0;
    uint8_t  protocolVersion  = 1;
    uint8_t  interfaceVersion = 0;
    uint8_t  messageType      = 0;
    uint8_t  returnCode       = 0;
};

// SOME/IP-SD entry (16 bytes on wire)
struct SomeIpSdEntry {
    uint8_t  type          = 0;  // 0x00 Find, 0x01 Offer, 0x06 Subscribe, 0x07 SubscribeAck
    uint8_t  indexFirstOpt = 0;
    uint8_t  indexSecondOpt= 0;
    uint8_t  numFirstOpts  = 0;
    uint8_t  numSecondOpts = 0;
    uint16_t serviceId     = 0;
    uint16_t instanceId    = 0;
    uint8_t  majorVersion  = 0;
    uint32_t ttl           = 0;  // 24-bit on wire, seconds
    uint32_t minorVersion  = 0;  // or eventgroupId for Subscribe entries
};

struct SomeIpMessage {
    SomeIpHeader               header;
    std::vector<uint8_t>       payload;
    bool                       isServiceDiscovery = false;
    std::vector<SomeIpSdEntry> sdEntries;
    std::string                error;
    bool                       valid = false;
};
