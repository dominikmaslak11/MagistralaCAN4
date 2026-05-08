#pragma once
#include <QObject>
#include <QFile>
#include <QDataStream>
#include <QHash>
#include "CanFrame.h"

/**
 * @brief Zapis sesji CAN w formacie ASAM MDF 4.x.
 *
 * Generuje minimalny poprawny plik MDF4 z ramkami CAN jako kanałami.
 * Otwieralny w Vector CANape, ETAS INCA i innych narzędziach.
 * Implementuje podzbiór standardu – kanały dla każdego unikalnego CAN ID
 * plus kanał czasu jako master.
 */
class Mdf4Writer : public QObject {
    Q_OBJECT
public:
    explicit Mdf4Writer(QObject *parent = nullptr);
    ~Mdf4Writer() override;

    bool start(const QString &filePath);
    void stop();
    bool isRecording() const { return m_recording; }

public slots:
    void recordFrame(const CanFrame &frame);

signals:
    void recordingStopped(const QString &path, uint64_t frameCount);

private:
    void writeHeader();
    void writeChannelBlock(uint32_t canId);
    void flushDataBlock(uint32_t canId);
    uint64_t filePos() const;

    QFile m_file;
    QDataStream m_stream;
    bool m_recording = false;
    uint64_t m_frameCount = 0;
    uint64_t m_idBlockPos = 0;
    int m_nextChannelBlock = 0;

    struct ChanData {
        QByteArray buffer;
        uint64_t lastTs = 0;
        int samples = 0;
        uint64_t dataBlockPos = 0;
        uint64_t channelBlockPos = 0;
    };
    QHash<uint32_t, ChanData> m_channels;
    uint64_t m_startTime = 0;

    static constexpr int BLOCK_SIZE = 4096;
    static constexpr int MAX_CHAN_SAMPLES = 256;
};
