#include "Mdf4Writer.h"
#include <QDebug>
#include <cstring>

Mdf4Writer::Mdf4Writer(QObject *parent) : QObject(parent) {
    m_buffer.reserve(BUFFER_CAPACITY);
}

Mdf4Writer::~Mdf4Writer() { stop(); }

uint64_t Mdf4Writer::filePos() {
    return static_cast<uint64_t>(m_file.pos());
}

bool Mdf4Writer::start(const QString &filePath) {
    if (m_recording) stop();

    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::WriteOnly)) return false;

    m_stream.setDevice(&m_file);
    m_frameCount = 0;
    m_channels.clear();
    m_startTime = 0;
    m_idBlockPos = 0;

    writeHeader();
    m_recording = true;

    // Uruchom wątek I/O
    m_ioRunning = true;
    m_ioThread = QThread::create([this] { ioWorker(); });
    m_ioThread->start();

    qDebug() << "MDF4 (async): nagrywanie rozpoczęte:" << filePath;
    return true;
}

void Mdf4Writer::stop() {
    if (!m_recording) return;

    m_recording = false;
    m_ioRunning = false;
    m_bufferNotEmpty.wakeAll();

    if (m_ioThread) {
        m_ioThread->wait(5000);
        delete m_ioThread;
        m_ioThread = nullptr;
    }

    // I/O worker już dokończył — plik jest zamknięty, sygnał wyemitowany
    qDebug() << "MDF4 (async): nagrywanie zakończone, ramek:" << m_frameCount;
}

void Mdf4Writer::recordFrame(const CanFrame &frame) {
    if (!m_recording) return;

    if (!m_buffer.tryPush(frame)) {
        QMutexLocker lock(&m_waitMutex);
        int retries = 0;
        while (!m_buffer.tryPush(frame) && m_recording && retries < 100) {
            m_bufferNotEmpty.wait(&m_waitMutex, 1);
            ++retries;
        }
        if (retries >= 100)
            qWarning() << "MDF4Writer: bufor pełny >100ms, pomijam ramkę";
    } else {
        m_bufferNotEmpty.wakeOne();
    }
}

void Mdf4Writer::ioWorker() {
    QVector<CanFrame> batch;
    batch.reserve(512);

    while (m_ioRunning || m_buffer.size() > 0) {
        int n = m_buffer.drain(batch);
        if (n > 0) {
            for (int i = 0; i < n; ++i) {
                const CanFrame &frame = batch[i];

                if (m_startTime == 0)
                    m_startTime = frame.timestamp;

                auto &ch = m_channels[frame.id];
                if (ch.channelBlockPos == 0) {
                    ch.channelBlockPos = filePos();
                    writeChannelBlock(frame.id);
                }

                double relTime = (frame.timestamp - m_startTime) / 1'000'000.0;
                ch.buffer.append(reinterpret_cast<const char *>(&relTime), 8);

                double val = frame.dlc > 0 ? static_cast<double>(frame.data[0]) : 0.0;
                ch.buffer.append(reinterpret_cast<const char *>(&val), 8);

                ch.samples++;
                m_frameCount++;

                if (ch.samples >= MAX_CHAN_SAMPLES)
                    flushDataBlock(frame.id);
            }
            batch.clear();
        } else {
            QMutexLocker lock(&m_waitMutex);
            if (m_ioRunning && m_buffer.size() == 0)
                m_bufferNotEmpty.wait(&m_waitMutex, 10);
        }
    }

    // Flush końcowy
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it)
        if (it->samples > 0)
            flushDataBlock(it.key());

    // Zaktualizuj ID block nagłówka
    uint64_t endPos = filePos();
    m_file.seek(0);
    QByteArray idBlock(64, '\0');
    idBlock[0] = 'I'; idBlock[1] = 'D'; idBlock[2] = 'B'; idBlock[3] = 'L';
    idBlock[4] = 'O'; idBlock[5] = 'C'; idBlock[6] = 'K'; idBlock[7] = '\0';
    idBlock[8] = 0x00; idBlock[9] = 0x00; idBlock[10] = 0x00; idBlock[11] = 0x00;
    *reinterpret_cast<uint64_t *>(&idBlock[16]) = 64;
    *reinterpret_cast<uint64_t *>(&idBlock[24]) = endPos - 64;
    m_file.write(idBlock);
    m_file.close();

    emit recordingStopped(m_file.fileName(), m_frameCount);
}

void Mdf4Writer::writeHeader() {
    QByteArray idBlock(64, '\0');
    idBlock[0] = 'I'; idBlock[1] = 'D'; idBlock[2] = 'B'; idBlock[3] = 'L';
    idBlock[4] = 'O'; idBlock[5] = 'C'; idBlock[6] = 'K'; idBlock[7] = '\0';
    *reinterpret_cast<uint64_t *>(&idBlock[16]) = 64;
    m_file.write(idBlock);
}

void Mdf4Writer::writeChannelBlock(uint32_t canId) {
    QByteArray cn(128, '\0');
    cn[0] = 'C'; cn[1] = 'N'; cn[2] = 'B'; cn[3] = 'L';
    cn[4] = 'O'; cn[5] = 'C'; cn[6] = 'K'; cn[7] = '\0';
    *reinterpret_cast<uint64_t *>(&cn[16]) = 128;
    *reinterpret_cast<uint32_t *>(&cn[40]) = 0;
    QString name = QString("CAN_ID_0x%1").arg(canId, 3, 16, QChar('0'));
    QByteArray nameBytes = name.toUtf8().left(32);
    std::memcpy(&cn[80], nameBytes.constData(), nameBytes.size());
    m_file.write(cn);
}

void Mdf4Writer::flushDataBlock(uint32_t canId) {
    auto &ch = m_channels[canId];
    if (ch.buffer.isEmpty()) return;

    uint64_t dtPos = filePos();
    uint32_t dataLen = ch.buffer.size();
    uint32_t blockLen = ((dataLen + 23 + 7) / 8) * 8;

    QByteArray dt(24 + dataLen, '\0');
    dt[0] = 'D'; dt[1] = 'T'; dt[2] = 'B'; dt[3] = 'L';
    *reinterpret_cast<uint64_t *>(&dt[16]) = 24 + dataLen;
    std::memcpy(&dt[24], ch.buffer.constData(), dataLen);
    m_file.write(dt);

    if (ch.channelBlockPos > 0 && ch.dataBlockPos == 0) {
        uint64_t savedPos = filePos();
        m_file.seek(ch.channelBlockPos + 32);
        m_file.write(reinterpret_cast<const char *>(&dtPos), 8);
        m_file.seek(savedPos);
    }
    ch.dataBlockPos = dtPos;
    ch.buffer.clear();
    ch.samples = 0;
}