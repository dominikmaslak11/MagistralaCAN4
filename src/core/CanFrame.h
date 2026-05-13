#pragma once
#include <cstdint>
#include <array>
#include <QMetaType>
#include <QString>

struct CanFrame {
    uint32_t id = 0;
    bool     extended = false;
    bool     rtr = false;
    bool     error = false;
    bool     fd = false;                  // CAN FD frame (payload up to 64 bytes)
    bool     brs = false;                 // Bit Rate Switch (CAN FD)
    bool     esi = false;                 // Error State Indicator (CAN FD)
    bool     xl = false;                  // CAN XL frame (>64 bytes, truncated)
    uint8_t  sdt = 0;                     // SDU type (CAN XL only)
    uint32_t af = 0;                      // acceptance field (CAN XL only)
    uint8_t  dlc = 0;                     // 0-8 classic, 0-64 FD, 0-2047 XL
    std::array<uint8_t, 64> data{};       // inline payload (CAN 2.0 + FD; XL truncated)
    uint64_t changedMask = 0;             // bitmask: bit i = 1 jeśli bajt i zmienił się (highlight)
    uint64_t timestamp = 0;

    /// Dostęp do bajtu payload — z bounds-check dla CAN XL.
    [[nodiscard]] uint8_t byteAt(int idx) const {
        return (idx >= 0 && idx < 64) ? data[idx] : uint8_t(0);
    }

    [[nodiscard]] QString toString() const {
        return QString("ID: %1 | DLC: %2 %3")
            .arg(id, 3, 16, QChar('0')).arg(dlc)
            .arg(xl ? "XL" : fd ? "FD" : "CAN");
    }
};

Q_DECLARE_METATYPE(CanFrame)
