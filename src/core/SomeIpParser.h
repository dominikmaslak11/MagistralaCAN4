#pragma once
#include "SomeIpFrame.h"
#include <cstdint>
#include <string>
#include <vector>

// Qt-free SOME/IP parser — safe to use in headless tests.
class SomeIpParser {
public:
    static SomeIpMessage parse(const std::vector<uint8_t> &data);
    static SomeIpMessage parse(const uint8_t *data, size_t len);

    static std::string messageTypeName(uint8_t type);
    static std::string returnCodeName(uint8_t code);
    static std::string sdEntryTypeName(uint8_t type);

    static bool isServiceDiscovery(uint16_t serviceId, uint16_t methodId);
    static bool isMagicCookie(uint16_t serviceId);

private:
    static void parseSdPayload(SomeIpMessage &msg);
};
