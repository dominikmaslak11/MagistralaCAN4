#pragma once
#include <QObject>
#include <QFile>
#include <QDataStream>
#include <QHash>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>
#include "CanFrame.h"
#include "RingBuffer.h"

/**
 * @brief Zapis sesji CAN w formacie ASAM MDF 4.x — asynchroniczny.
 *
 * Ramki są buforowane w lock-free RingBuffer i zapisywane
 * w dedykowanym wątku I/O. Zero blokowania GUI.
 *
 * Format: IDBL + CNBL per CAN ID + DTBL z timestampami i wartościami.
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
    /// Bezpieczne dla wątku GUI — push do ring buffera, natychmiastowy powrót.
    void recordFrame(const CanFrame &frame);

signals:
    void recordingStopped(const QString &path, uint64_t frameCount);

private:
    void ioWorker();   // uruchamiane w m_ioThread
    void writeHeader();
    void writeChannelBlock(uint32_t canId);
    void flushDataBlock(uint32_t canId);
    uint64_t filePos();

    // Stan dostępny tylko z wątku I/O (lub głównego przed start/po stop)
    QFile m_file;
    QDataStream m_stream;
    uint64_t m_frameCount = 0;
    uint64_t m_startTime = 0;
    uint64_t m_idBlockPos = 0;

    struct ChanData {
        QByteArray buffer;
        int samples = 0;
        uint64_t dataBlockPos = 0;
        uint64_t channelBlockPos = 0;
    };
    QHash<uint32_t, ChanData> m_channels;

    // Async I/O
    std::atomic<bool> m_recording{false};
    RingBuffer<CanFrame> m_buffer;
    QThread *m_ioThread = nullptr;
    std::atomic<bool> m_ioRunning{false};
    QMutex m_waitMutex;
    QWaitCondition m_bufferNotEmpty;

    static constexpr int MAX_CHAN_SAMPLES = 256;
    static constexpr int BUFFER_CAPACITY = 65536; // ~1.3 MB
};