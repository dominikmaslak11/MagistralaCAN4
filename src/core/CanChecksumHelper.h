#pragma once
#include "CanFrame.h"
#include <cstdint>

/// Computes and validates common automotive CRC/checksum algorithms over CAN frame payloads.
///
/// Supported algorithms:
///   XOR    – XOR of all bytes in the range
///   SUM8   – 8-bit sum (truncated to uint8_t) of all bytes in the range
///   CRC8_SAE_J1850  – CRC-8/SAE-J1850 (poly=0x1D, init=0xFF, no reflections)
///   CRC8_AUTOSAR    – CRC-8/AUTOSAR (poly=0x2F, init=0xFF, no reflections, finalXor=0xFF)
class CanChecksumHelper {
public:
    enum class Algorithm {
        XOR,
        SUM8,
        CRC8_SAE_J1850,
        CRC8_AUTOSAR,
    };

    /// Compute checksum over frame.data[startByte..endByte] (endByte inclusive).
    /// Returns 0 if indices are out of range.
    static uint8_t compute(const CanFrame &frame, int startByte, int endByte,
                           Algorithm algo);

    /// Returns true if frame.data[checksumByte] == compute(frame, startByte, endByte, algo).
    static bool validate(const CanFrame &frame, int checksumByte,
                         int startByte, int endByte, Algorithm algo);

    /// Low-level: compute over an arbitrary byte buffer.
    static uint8_t computeBuffer(const uint8_t *buf, int len, Algorithm algo);

private:
    static uint8_t crc8SaeJ1850(const uint8_t *buf, int len);
    static uint8_t crc8Autosar(const uint8_t *buf, int len);
};
