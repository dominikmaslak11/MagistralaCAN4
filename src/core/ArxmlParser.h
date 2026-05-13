#pragma once
#include "DbcParser.h"
#include <QString>

/**
 * @brief Minimal AUTOSAR 4.x ARXML parser — extracts CAN signal definitions
 *        and converts them to DbcMessage/DbcSignal format.
 *
 * Supports:
 *  - I-SIGNAL: bit length, byte order
 *  - I-SIGNAL-I-PDU: PDU with signal-to-PDU mappings (start position)
 *  - FRAME: frame length, PDU references
 *  - CAN-FRAME-TRIGGERING: CAN ID + frame reference
 *  - COMPU-METHOD: linear scale (a/b numerator + denominator)
 *  - UNIT: physical unit name
 */
class ArxmlParser {
public:
    QVector<DbcMessage> load(const QString &path);
    QString lastError() const { return m_error; }

private:
    QString m_error;
};
