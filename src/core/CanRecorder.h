#pragma once
#include <QObject>
#include <QFile>
#include <QDataStream>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>
#include "CanFrame.h"
#include "RingBuffer.h"

/**
 * @brief Rejestrator sesji CAN — zapis do binarnego formatu .mcan.
 *
 * Format pliku:
 *   Magic: "MCAN" (4 bajty)
 *   Version: uint32 (4 bajty)
 *   Frame records: [timestamp(uint64) | id(uint32) | flags(uint8) | dlc(uint8) | data(dlc)]
 *
 * Ramki są buforowane w lock-free RingBuffer (SPSC) i zapisywane
 * asynchronicznie w dedykowanym wątku I/O — zero blokowania GUI.
 */
class CanRecorder : public QObject {
    Q_OBJECT
public:
    explicit CanRecorder(QObject *parent = nullptr);
    ~CanRecorder() override;

    bool startRecording(const QString &filePath);
    void stopRecording();
    bool isRecording() const { return m_recording; }

public slots:
    /// Bezpieczne dla wątku GUI — push do ring buffera, natychmiastowy powrót.
    void recordFrame(const CanFrame &frame);

signals:
    void recordingStarted(const QString &path);
    void recordingStopped(uint64_t frameCount);

private:
    void ioWorker();            // uruchamiane w m_ioThread
    void compressAndReplace(const QString &path);  // kompresja zstd po nagraniu

    QFile m_file;
    QDataStream m_stream;
    bool m_recording = false;
    uint64_t m_frameCount = 0;

    // Async I/O
    RingBuffer<CanFrame> m_buffer;
    QThread *m_ioThread = nullptr;
    std::atomic<bool> m_ioRunning{false};
    QMutex m_waitMutex;
    QWaitCondition m_bufferNotEmpty;

    static constexpr uint32_t MAGIC = 0x4E41434D; // "MCAN" LE
    static constexpr uint32_t VERSION = 1;
    static constexpr int BUFFER_CAPACITY = 65536; // ~1.3 MB, wystarczające na ~13 s przy 5000 fps
};