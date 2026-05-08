#pragma once
#include <cstdint>
#include <QString>
#include <QVector>
#include "CanFrame.h"

/**
 * @brief Ramka UDS (ISO 14229) zdekodowana z surowej ramki CAN.
 *
 * Struktura danych UDS na CAN:
 *   Bajt 0: SID (Service Identifier)
 *   Bajt 1: Sub-function (dla niektórych serwisów) lub pierwszy bajt DID
 *   Bajt 2-3: DID (Data Identifier, 16-bit big-endian, opcjonalny)
 *   Bajt 4+: dane serwisu
 *
 * Odpowiedzi:
 *   Pozytywna: SID + 0x40
 *   Negatywna: 0x7F + SID + NRC
 */
struct UdsFrame {
    enum Type { Unknown, Request, PositiveResponse, NegativeResponse, FlowControl };

    Type    type     = Unknown;
    uint8_t sid      = 0;     // Service Identifier
    uint8_t subFunc  = 0;     // Sub-function (jeśli dotyczy)
    uint16_t did     = 0;     // Data Identifier (jeśli dotyczy)
    uint8_t nrc      = 0;     // Negative Response Code
    uint8_t dlc      = 0;
    uint32_t canId   = 0;     // CAN ID na którym przyszła ramka
    uint64_t timestamp = 0;
    std::array<uint8_t, 64> data{};

    /// Parsuje CanFrame → UdsFrame.
    [[nodiscard]] static UdsFrame fromCanFrame(const CanFrame &frame);

    /// Heurystyka: czy ramka CAN wygląda na UDS.
    [[nodiscard]] static bool looksLikeUds(const CanFrame &frame);

    /// Czytelny opis.
    [[nodiscard]] QString toString() const;
};
