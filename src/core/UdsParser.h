#pragma once
#include <QHash>
#include <QString>
#include <QList>
#include <QPair>
#include <cstdint>
#include "UdsFrame.h"

/**
 * @brief Baza wiedzy UDS: nazwy serwisów (SID), kody NRC, typowe DID.
 */
class UdsParser {
public:
    UdsParser();

    /// Nazwa serwisu UDS (SID).
    [[nodiscard]] QString serviceName(uint8_t sid) const;

    /// Czy SID jest znany.
    [[nodiscard]] bool isKnownSid(uint8_t sid) const { return m_sidDb.contains(sid); }

    /// Nazwa kodu błędu NRC.
    [[nodiscard]] static QString nrcName(uint8_t nrc);

    /// Nazwa Data Identifier.
    [[nodiscard]] QString didName(uint16_t did) const;

    /// Sub-funkcja jako tekst (np. "Default Session" dla 0x01).
    [[nodiscard]] static QString subFunctionName(uint8_t sid, uint8_t sub);

    /// Dekoduje dane odpowiedzi (ReadDataByIdentifier itp.).
    [[nodiscard]] static QString decodeResponseData(uint8_t sid, const uint8_t *data, int len);

    /// Lista znanych SID.
    [[nodiscard]] QList<uint8_t> knownSids() const { return m_sidDb.keys(); }

private:
    struct SidInfo { QString name; QString desc; };
    QHash<uint8_t, SidInfo> m_sidDb;
    QHash<uint16_t, QString> m_didDb;
};
