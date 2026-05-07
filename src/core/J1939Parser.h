#pragma once
#include <QHash>
#include <QString>
#include <QVector>
#include <QPair>
#include <cstdint>
#include "J1939Frame.h"

/**
 * @brief Definicja pojedynczego SPN (Suspect Parameter Number) w ramach PGN.
 */
struct SpnDef {
    uint32_t spn;        // numer SPN
    QString  name;        // nazwa parametru
    int      byteOffset;  // bajt startu (0‑based w danych ramki)
    int      bitOffset;   // bit startu (0‑7)
    int      bitLength;   // długość pola w bitach
    double   resolution;  // rozdzielczość (mnożnik)
    double   offset;      // offset (dodawany po mnożeniu)
    QString  unit;        // jednostka (np. "rpm", "degC")
};

/**
 * @brief Definicja PGN – grupa parametrów J1939.
 */
struct PgnDef {
    uint32_t pgn;
    QString  name;
    QString  acronym;
    int      defaultDlc;       // typowe DLC
    QVector<SpnDef> spns;      // lista sygnałów w tej grupie
};

/**
 * @brief Parser J1939 – baza PGN/SPN, dekodowanie wartości.
 *
 * Zawiera statyczną bazę popularnych PGN (EEC1, EEC2, Temperatury,
 * Prędkość, itp.) oraz metody do ekstrakcji wartości SPN z danych ramki.
 */
class J1939Parser {
public:
    J1939Parser();

    /// Zwraca definicję PGN (lub nullptr).
    const PgnDef* pgnDef(uint32_t pgn) const;

    /// Czytelna nazwa PGN.
    QString pgnName(uint32_t pgn) const;

    /// Nazwa źródła (SA) – podstawowe funkcje przemysłowe.
    static QString sourceName(uint8_t sa);

    /// Lista wszystkich zarejestrowanych PGN.
    QList<uint32_t> knownPgns() const { return m_pgnDb.keys(); }

    /// Dekoduje wartość SPN z surowych danych ramki.
    static double decodeSpn(const uint8_t *data, int dlc,
                            const SpnDef &spn);

    /// Buduje czytelny string z wartościami wszystkich SPN dla danego PGN.
    QString decodeSignals(const J1939Frame &frame) const;

private:
    void registerPg(const PgnDef &pg);
    QHash<uint32_t, PgnDef> m_pgnDb;
};
