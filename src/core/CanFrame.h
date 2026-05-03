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
    uint8_t  dlc = 0;                    // 0-8 for classic, 0-64 for FD
    std::array<uint8_t, 64> data{};      // max 64 bytes
    uint64_t timestamp = 0;

    [[nodiscard]] QString toString() const {
        return QString("ID: %1 | DLC: %2").arg(id, 3, 16, QChar('0')).arg(dlc);
    }
};

Q_DECLARE_METATYPE(CanFrame)
