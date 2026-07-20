#include "CanChecksumHelper.h"
#include <algorithm>

// ── CRC-8/SAE-J1850 (poly=0x1D, init=0xFF) ───────────────────────────────────

uint8_t CanChecksumHelper::crc8SaeJ1850(const uint8_t *buf, int len) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; ++i) {
        crc ^= buf[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x1D) : (crc << 1);
    }
    return crc;
}

// ── CRC-8/AUTOSAR (poly=0x2F, init=0xFF, finalXor=0xFF) ──────────────────────

uint8_t CanChecksumHelper::crc8Autosar(const uint8_t *buf, int len) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; ++i) {
        crc ^= buf[i];
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x2F) : (crc << 1);
    }
    return crc ^ 0xFF;
}

// ── Public API ────────────────────────────────────────────────────────────────

uint8_t CanChecksumHelper::computeBuffer(const uint8_t *buf, int len, Algorithm algo) {
    if (!buf || len <= 0) return 0;
    switch (algo) {
    case Algorithm::XOR: {
        uint8_t acc = 0;
        for (int i = 0; i < len; ++i) acc ^= buf[i];
        return acc;
    }
    case Algorithm::SUM8: {
        uint8_t acc = 0;
        for (int i = 0; i < len; ++i) acc = static_cast<uint8_t>(acc + buf[i]);
        return acc;
    }
    case Algorithm::CRC8_SAE_J1850:
        return crc8SaeJ1850(buf, len);
    case Algorithm::CRC8_AUTOSAR:
        return crc8Autosar(buf, len);
    }
    return 0;
}

uint8_t CanChecksumHelper::compute(const CanFrame &frame, int startByte, int endByte,
                                    Algorithm algo) {
    if (startByte < 0 || endByte < startByte || endByte >= 64) return 0;
    if (startByte >= frame.dlc || endByte >= frame.dlc) return 0;

    int len = endByte - startByte + 1;
    return computeBuffer(frame.data.data() + startByte, len, algo);
}

bool CanChecksumHelper::validate(const CanFrame &frame, int checksumByte,
                                  int startByte, int endByte, Algorithm algo) {
    if (checksumByte < 0 || checksumByte >= frame.dlc) return false;
    return frame.data[checksumByte] == compute(frame, startByte, endByte, algo);
}
