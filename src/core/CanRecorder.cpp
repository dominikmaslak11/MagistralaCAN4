#include "CanRecorder.h"
#include <QDebug>
#include <zstd.h>

CanRecorder::CanRecorder(QObject *parent) : QObject(parent) {
    m_buffer.reserve(BUFFER_CAPACITY);
}

CanRecorder::~CanRecorder() {
    stopRecording();
}

bool CanRecorder::startRecording(const QString &filePath) {
    if (m_recording) stopRecording();

    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::WriteOnly)) return false;

    m_stream.setDevice(&m_file);
    m_stream.setByteOrder(QDataStream::LittleEndian);
    // Nagłówek pliku
    m_stream << (quint32)MAGIC << (quint32)VERSION;

    m_frameCount = 0;
    m_recording = true;

    // Uruchom wątek I/O
    m_ioRunning = true;
    m_ioThread = QThread::create([this] { ioWorker(); });
    m_ioThread->start();

    emit recordingStarted(filePath);
    qDebug() << "Nagrywanie (async) rozpoczęte:" << filePath;
    return true;
}

void CanRecorder::stopRecording() {
    if (!m_recording) return;

    // Zatrzymaj producera
    m_recording = false;

    // Obudź wątek I/O i poczekaj aż dokończy (zamknięcie pliku, kompresja, sygnał)
    m_ioRunning = false;
    m_bufferNotEmpty.wakeAll();

    if (m_ioThread) {
        m_ioThread->wait(5000); // max 5s na dokończenie + kompresję
        delete m_ioThread;
        m_ioThread = nullptr;
    }

    qDebug() << "Nagrywanie zatrzymane (async). Ramki:" << m_frameCount;
}

void CanRecorder::recordFrame(const CanFrame &frame) {
    if (!m_recording) return;

    // Non-blocking push do ring buffera
    // Jeśli bufor pełny — zapis jest szybszy niż CAN (dysk > 100 MB/s vs CAN 0.1 MB/s),
    // więc full to sytuacja wyjątkowa (np. HDD pod obciążeniem). Wtedy czekamy krótko.
    if (!m_buffer.tryPush(frame)) {
        QMutexLocker lock(&m_waitMutex);
        // Spróbuj jeszcze raz, potem czekaj na sygnał od konsumera
        int retries = 0;
        while (!m_buffer.tryPush(frame) && m_recording && retries < 100) {
            m_bufferNotEmpty.wait(&m_waitMutex, 1); // 1ms
            ++retries;
        }
        if (retries >= 100) {
            qWarning() << "CanRecorder: nie można zapisać ramki, bufor pełny przez >100ms — pomijam";
        }
    } else {
        m_bufferNotEmpty.wakeOne(); // sygnał dla konsumera: są dane
    }
}

void CanRecorder::ioWorker() {
    QVector<CanFrame> batch;
    batch.reserve(512);

    while (m_ioRunning || m_buffer.size() > 0) {
        int n = m_buffer.drain(batch);
        if (n > 0) {
            // Zapis partii na dysk
            for (int i = 0; i < n; ++i) {
                const CanFrame &frame = batch[i];
                uint8_t flags = (frame.extended ? 0x01 : 0) | (frame.rtr ? 0x02 : 0) |
                                (frame.error ? 0x04 : 0) | (frame.fd ? 0x08 : 0) |
                                (frame.xl ? 0x10 : 0);
                if (frame.xl) {
                    m_stream << (quint64)frame.timestamp << (quint32)frame.id
                             << flags << (quint16)frame.dlc;
                } else {
                    m_stream << (quint64)frame.timestamp << (quint32)frame.id
                             << flags << frame.dlc;
                }
                m_stream.writeRawData(reinterpret_cast<const char *>(frame.data.data()), frame.dlc);
                ++m_frameCount;
            }
            batch.clear();
        } else {
            // Bufor pusty — czekaj na sygnał od producenta
            QMutexLocker lock(&m_waitMutex);
            if (m_ioRunning && m_buffer.size() == 0)
                m_bufferNotEmpty.wait(&m_waitMutex, 10); // max 10ms wait
        }
    }

    // Finalizacja: zamknij plik i skompresuj
    QString origPath = m_file.fileName();
    m_file.close();

    // Kompresja zstd
    compressAndReplace(origPath);

    emit recordingStopped(m_frameCount);
}

void CanRecorder::compressAndReplace(const QString &path) {
    QFile inFile(path);
    if (!inFile.open(QIODevice::ReadOnly)) return;

    QByteArray raw = inFile.readAll();
    inFile.close();

    if (raw.size() < 512) return; // za małe na sensowną kompresję

    size_t bound = ZSTD_compressBound(raw.size());
    QByteArray compressed(static_cast<qsizetype>(bound), '\0');
    size_t cSize = ZSTD_compress(compressed.data(), bound, raw.constData(), raw.size(), 1);

    if (ZSTD_isError(cSize)) {
        qWarning() << "CanRecorder: błąd kompresji zstd:" << ZSTD_getErrorName(cSize);
        return;
    }

    compressed.resize(static_cast<qsizetype>(cSize));
    float ratio = raw.size() > 0 ? static_cast<float>(cSize) / static_cast<float>(raw.size()) * 100.0f : 100.0f;

    QString zstPath = path + ".zst";
    QFile outFile(zstPath);
    if (!outFile.open(QIODevice::WriteOnly)) return;
    outFile.write(compressed);
    outFile.close();

    // Usuń niekompresowany oryginał
    inFile.remove();

    qDebug() << "CanRecorder: skompresowano" << path << "→" << zstPath
             << QString("(%1 KB → %2 KB, %3%)")
                    .arg(raw.size() / 1024)
                    .arg(cSize / 1024)
                    .arg(ratio, 0, 'f', 1);
}