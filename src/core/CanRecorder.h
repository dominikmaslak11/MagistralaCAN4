#pragma once
#include <QObject>
#include <QFile>
#include <QDataStream>
#include "CanFrame.h"

/**
 * @brief Rejestrator sesji CAN – zapis do binarnego formatu .mcan.
 *
 * Format pliku:
 *   Magic: "MCAN" (4 bajty)
 *   Version: uint32 (4 bajty)
 *   Frame records: [timestamp(uint64) | id(uint32) | flags(uint8) | dlc(uint8) | data(dlc)]
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
    void recordFrame(const CanFrame &frame);

signals:
    void recordingStarted(const QString &path);
    void recordingStopped(uint64_t frameCount);

private:
    QFile m_file;
    QDataStream m_stream;
    bool m_recording = false;
    uint64_t m_frameCount = 0;

    static constexpr uint32_t MAGIC = 0x4E41434D; // "MCAN" LE
    static constexpr uint32_t VERSION = 1;
};
