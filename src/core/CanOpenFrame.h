#pragma once
#include <cstdint>
#include <QString>
#include "CanFrame.h"

/**
 * @brief Ramka CANopen (CiA 301) zdekodowana z CAN ID i danych.
 *
 * CAN ID = Function Code (4 bity) + Node ID (7 bitów)
 */
struct CanOpenFrame {
    enum Type { Unknown, NMT, SYNC, EMCY, PDO1_TX, PDO1_RX, PDO2_TX, PDO2_RX, PDO3_TX, PDO3_RX, PDO4_TX, PDO4_RX, SDO_TX, SDO_RX, HEARTBEAT };
    Type    type = Unknown;
    uint8_t nodeId = 0;     // 1–127
    uint8_t funcCode = 0;   // 4-bit function code
    uint8_t dlc = 0;
    uint32_t canId = 0;
    uint64_t timestamp = 0;
    std::array<uint8_t, 8> data{};

    [[nodiscard]] static CanOpenFrame fromCanFrame(const CanFrame &frame);
    [[nodiscard]] static bool isCanOpen(const CanFrame &frame);
    [[nodiscard]] QString toString() const;
};

class CanOpenParser {
public:
    CanOpenParser();
    [[nodiscard]] QString typeName(CanOpenFrame::Type t) const;
    [[nodiscard]] QString nmtCommand(uint8_t cmd) const;
    [[nodiscard]] QString emcyCode(uint16_t code) const;
    [[nodiscard]] QString sdoCommand(uint8_t cmd) const;
    [[nodiscard]] QString heartbeatState(uint8_t state) const;

private:
    struct TypeInfo { QString name; QString desc; };
    QHash<CanOpenFrame::Type, TypeInfo> m_typeDb;
};
