#include "CanRecorder.h"
#include <QDebug>

CanRecorder::CanRecorder(QObject *parent) : QObject(parent) {}
CanRecorder::~CanRecorder() { stopRecording(); }

bool CanRecorder::startRecording(const QString &filePath) {
    if (m_recording) stopRecording();

    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::WriteOnly)) return false;

    m_stream.setDevice(&m_file);
    m_stream << MAGIC << VERSION;
    m_recording = true;
    m_frameCount = 0;
    emit recordingStarted(filePath);
    qDebug() << "Nagrywanie rozpoczęte:" << filePath;
    return true;
}

void CanRecorder::stopRecording() {
    if (!m_recording) return;
    m_recording = false;
    m_file.close();
    emit recordingStopped(m_frameCount);
    qDebug() << "Nagrywanie zatrzymane. Ramki:" << m_frameCount;
}

void CanRecorder::recordFrame(const CanFrame &frame) {
    if (!m_recording) return;
    uint8_t flags = (frame.extended ? 0x01 : 0) | (frame.rtr ? 0x02 : 0) | (frame.error ? 0x04 : 0) | (frame.fd ? 0x08 : 0);
    m_stream << frame.timestamp << frame.id << flags << frame.dlc;
    m_stream.writeRawData(reinterpret_cast<const char *>(frame.data.data()), frame.dlc);
    m_frameCount++;
}
