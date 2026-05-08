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
    bool     fd = false;                  // CAN FD frame
    bool     xl = false;                  // CAN XL frame (max 2048 bytes)
    uint8_t  sdt = 0;                     // SDU type (CAN XL only)
    uint32_t af = 0;                      // acceptance field (CAN XL only)
    uint8_t  dlc = 0;                     // 0-8 classic, 0-64 FD, 0-2047 XL
    std::array<uint8_t, 2048> data{};     // max 2048 bytes (CAN XL)
    uint64_t timestamp = 0;

    [[nodiscard]] QString toString() const {
        return QString("ID: %1 | DLC: %2 %3")
            .arg(id, 3, 16, QChar('0')).arg(dlc)
            .arg(xl ? "XL" : fd ? "FD" : "CAN");
    }
};

Q_DECLARE_METATYPE(CanFrame)
