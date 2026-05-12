#pragma once
#include "DoIpFrame.h"
#include <cstdint>
#include <string>
#include <vector>

// Qt-free DoIP parser (ISO 13400-2:2019).
class DoIpParser {
public:
    static DoIpMessage parse(const std::vector<uint8_t> &data);
    static DoIpMessage parse(const uint8_t *data, size_t len);

    static std::string payloadTypeName(uint16_t type);
    static std::string nackCodeName(uint8_t code);
    static std::string routingResponseCodeName(uint8_t code);
    static std::string diagAckCodeName(uint8_t code);

    static DoIpPayloadType classifyType(uint16_t raw);

private:
    static void decodeVehicleAnnouncement(DoIpMessage &msg);
    static void decodeRoutingActivationReq(DoIpMessage &msg);
    static void decodeRoutingActivationResp(DoIpMessage &msg);
    static void decodeDiagnosticMessage(DoIpMessage &msg);
    static void decodeDiagnosticAck(DoIpMessage &msg, bool isNack);
};
