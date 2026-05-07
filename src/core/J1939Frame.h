#pragma once
#include <cstdint>
#include <QString>
#include "CanFrame.h"

/**
 * @brief Struktura reprezentująca ramkę J1939 – zdekodowaną z 29-bitowego CAN ID.
 *
 * SAE J1939 używa 29-bitowych identyfikatorów CAN w następującym układzie:
 *   P(3) | R(1) | DP(1) | PF(8) | PS(8) | SA(8)
 *   26-28   25     24     16-23   8-15    0-7
 *
 * PGN (Parameter Group Number) – 18 bitów:
 *   PGN = (R<<17) | (DP<<16) | (PF<<8) | (PS gdy PF<240, inaczej 0x00)
 *
 * Tryb PDU1 (PF < 240):  PS = adres docelowy (DA),  PGN zawiera DA
 * Tryb PDU2 (PF >= 240): PS = Group Extension (GE), PGN nie zawiera PS
 */
struct J1939Frame {
    // --- Pola dekodowane z CAN ID ---
    uint8_t  priority      = 0;   // 0–7 (0 = najwyższy)
    bool     dataPage      = false;
    uint8_t  pduFormat     = 0;   // PF (8 bitów)
    uint8_t  pduSpecific   = 0;   // PS (8 bitów)
    uint8_t  sourceAddress = 0;   // SA (8 bitów)
    uint32_t pgn           = 0;   // 18-bit PGN

    // --- Pola pochodne ---
    bool     isPdu1        = false; // PF < 240 → PDU1 (peer-to-peer)
    uint8_t  destAddress   = 0;     // ważne tylko dla PDU1 (PS = DA)
    uint8_t  groupExt      = 0;     // ważne tylko dla PDU2 (PS = GE)

    // --- Dane ramki ---
    uint64_t timestamp     = 0;
    uint8_t  dlc           = 0;
    std::array<uint8_t, 64> data{};

    /**
     * @brief Tworzy J1939Frame z surowej ramki CAN.
     * @return true jeśli ramka ma poprawny 29-bitowy format J1939.
     */
    [[nodiscard]] static bool fromCanFrame(const CanFrame &can, J1939Frame &out);

    /**
     * @brief Sprawdza, czy ramka CAN wygląda na J1939
     * (29-bit ID + rozsądne PGN).
     */
    [[nodiscard]] static bool isJ1939(const CanFrame &can);

    /**
     * @brief Rekonstruuje oryginalny 29-bit CAN ID.
     */
    [[nodiscard]] uint32_t canId() const;

    /**
     * @brief Opis PGN (nazwa z bazy – dostarczana przez J1939Parser).
     */
    [[nodiscard]] QString pgnHex() const {
        return QString("0x%1").arg(pgn, 5, 16, QChar('0')).toUpper();
    }

    /**
     * @brief Czytelny opis ramki (PGN, SA, priorytet).
     */
    [[nodiscard]] QString toString() const;
};
