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
    uint8_t  dlc = 0;
    std::array<uint8_t, 8> data{};
    uint64_t timestamp = 0;

    [[nodiscard]] QString toString() const {
        return QString("ID: %1 | DLC: %2").arg(id, 3, 16, QChar('0')).arg(dlc);
    }
};

Q_DECLARE_METATYPE(CanFrame)
