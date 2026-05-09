#pragma once
#include <QString>
#include <QVector>
#include <QHash>
#include <cstdint>

struct DbcSignal {
    QString name;
    int startBit;       // bit startu (0‑63)
    int length;         // długość w bitach
    bool isLittleEndian;
    bool isSigned;
    double scale;
    double offset;
    double minimum;
    double maximum;
    QString unit;
};

struct DbcMessage {
    uint32_t id;
    QString name;
    int dlc;
    QVector<DbcSignal> sigList;
};

class DbcParser {
public:
    bool load(const QString &fileName);
    bool save(const QString &fileName) const;

    QVector<DbcMessage> messages() const;
    DbcMessage messageForId(uint32_t id) const;
    QString signalDescriptions(uint32_t id, const uint8_t* data, int dlc) const;

    /// Dekoduje wartości wszystkich sygnałów dla danej ramki CAN.
    /// Zwraca mapę nazwa_sygnału → wartość (w jednostkach fizycznych).
    QHash<QString, double> decodeSignals(uint32_t id, const uint8_t* data, int dlc) const;

    /// Podmienia całą bazę wiadomości (np. z edytora).
    void setMessages(const QVector<DbcMessage> &msgs);

    /// Serializuje do stringa DBC (do podglądu).
    static QString serialize(const QVector<DbcMessage> &msgs);

private:
    void rebuildMap();

    QVector<DbcMessage> m_messages;
    QHash<uint32_t, DbcMessage> m_messageMap;
};
