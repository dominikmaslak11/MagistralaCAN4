#pragma once
#include <cstdint>
#include <QString>
#include <QHash>
#include <QList>
#include <QPair>
#include "CanFrame.h"

struct ObdFrame {
    enum Type { Unknown, Request, Response };
    Type type = Unknown;
    uint8_t mode = 0;     // 0x01–0x0A
    uint16_t pid = 0;     // Parameter ID
    uint8_t len = 0;      // data length from ISO-TP header
    uint32_t canId = 0;
    uint64_t timestamp = 0;
    uint8_t dlc = 0;
    std::array<uint8_t, 64> data{};

    static ObdFrame fromCanFrame(const CanFrame &frame);
    static bool isObd(const CanFrame &frame);
    QString toString() const;
};

class ObdParser {
public:
    ObdParser();
    QString modeName(uint8_t mode) const;
    QString pidName(uint8_t mode, uint16_t pid) const;
    QString pidUnit(uint8_t mode, uint16_t pid) const;
    double decodePidValue(uint8_t mode, uint16_t pid, const uint8_t *data, int len) const;
    QStringList parseDtcs(const ObdFrame &f) const;
    QList<uint8_t> knownModes() const { return m_modeDb.keys(); }

    static QString decodeDtc(uint16_t rawCode);

private:
    struct PidDef { QString name; QString unit; double scale; double offset; int numBytes = 2; };
    QHash<uint8_t, QString> m_modeDb;
    QHash<QPair<uint8_t, uint16_t>, PidDef> m_pidDb;
};
