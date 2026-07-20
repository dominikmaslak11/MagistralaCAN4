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

    Type     type       = Unknown;
    uint8_t  sid        = 0;      // Service Identifier
    uint8_t  subFunc    = 0;      // Sub-function (jeśli dotyczy)
    uint16_t did        = 0;      // Data Identifier (jeśli dotyczy)
    uint8_t  nrc        = 0;      // Negative Response Code
    uint8_t  dlc        = 0;      // liczba bajtów w polu data
    uint32_t canId      = 0;      // CAN ID źródłowy
    uint64_t timestamp  = 0;      // µs
    bool     multiFrame = false;  // złożone z wielu ramek CAN (ISO-TP)
    std::array<uint8_t, 4096> data{};

    /// Parsuje gotowy payload ISO-TP (SID już na pozycji [0]).
    [[nodiscard]] static UdsFrame fromPayload(const uint8_t *payload, int len,
                                              uint32_t canId, uint64_t timestampUs,
                                              bool multi = false);

    /// Parsuje pojedynczą surową ramkę CAN (bez ISO-TP). Zostaje dla kompatybilności.
    [[nodiscard]] static UdsFrame fromCanFrame(const CanFrame &frame);

    /// Czy ramka CAN należy do zakresu diagnostycznego UDS/OBD (11-bit 0x700-0x7FF
    /// lub 29-bit 0x18DA____).
    [[nodiscard]] static bool looksLikeUds(const CanFrame &frame);

    [[nodiscard]] QString toString() const;
};
