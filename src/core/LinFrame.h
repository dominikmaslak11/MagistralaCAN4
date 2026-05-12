#pragma once
#include <cstdint>
#include <array>
#include <string>

/// LIN bus frame (ISO 17987 / SAE J2602)
struct LinFrame {
    uint8_t  frameId   = 0;    // 6-bit frame ID (0x00–0x3F)
    uint8_t  pid       = 0;    // Protected ID = frameId | parity bits
    uint8_t  dlc       = 0;    // Payload length (1–8)
    std::array<uint8_t, 8> data{};
    uint8_t  checksum  = 0;
    uint64_t timestamp = 0;    // µs

    enum class ChecksumType { Classic, Enhanced };
    ChecksumType csType = ChecksumType::Enhanced;

    bool checksumValid = false;

    /// Compute LIN parity bits for a 6-bit frame ID.
    /// P0 = ID0 ⊕ ID1 ⊕ ID2 ⊕ ID4
    /// P1 = ¬(ID1 ⊕ ID3 ⊕ ID4 ⊕ ID5)
    static uint8_t computePid(uint8_t frameId);

    /// Verify the Protected ID parity.
    static bool pidValid(uint8_t pid);

    /// Compute checksum.
    /// Classic:  sum of data bytes mod 256, inverted
    /// Enhanced: sum of PID + data bytes mod 256, inverted
    static uint8_t computeChecksum(uint8_t pid, const uint8_t *data, int len, ChecksumType type);
};
