#include "CanPlayer.h"
#include <QFile>
#include <QDataStream>
#include <QDebug>
#include <zstd.h>

static constexpr uint32_t MAGIC = 0x4E41434D; // "MCAN" LE
static constexpr uint32_t VERSION = 1;

CanPlayer::CanPlayer(QObject *parent) : QObject(parent) {
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &CanPlayer::emitNextFrame);
}

int CanPlayer::loadFile(const QString &path) {
    stop();
    m_frames.clear();
    m_loaded = false;

    // Wczytaj surowe dane (z dekompresją zstd jeśli .zst)
    QByteArray rawData;
    if (path.endsWith(".zst", Qt::CaseInsensitive)) {
        QFile zstFile(path);
        if (!zstFile.open(QIODevice::ReadOnly)) {
            qWarning() << "CanPlayer: nie można otworzyć" << path;
            return 0;
        }
        QByteArray compressed = zstFile.readAll();
        zstFile.close();

        unsigned long long origSize = ZSTD_getFrameContentSize(compressed.constData(), compressed.size());
        if (origSize == ZSTD_CONTENTSIZE_ERROR || origSize == ZSTD_CONTENTSIZE_UNKNOWN) {
            qWarning() << "CanPlayer: nieprawidłowy plik zst";
            return 0;
        }
        rawData.resize(static_cast<int>(origSize));
        size_t decompSize = ZSTD_decompress(rawData.data(), origSize,
                                            compressed.constData(), compressed.size());
        if (ZSTD_isError(decompSize)) {
            qWarning() << "CanPlayer: błąd dekompresji:" << ZSTD_getErrorName(decompSize);
            return 0;
        }
    } else {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "CanPlayer: nie można otworzyć" << path;
            return 0;
        }
        rawData = file.readAll();
        file.close();
    }

    // Parsuj format .mcan
    QDataStream stream(rawData);
    stream.setByteOrder(QDataStream::LittleEndian);

    quint32 magic = 0, version = 0;
    stream >> magic >> version;
    if (magic != MAGIC) {
        qWarning() << "CanPlayer: nieprawidłowy magic (oczekiwano MCAN)";
        return 0;
    }

    m_frames.reserve(10000);
    while (!stream.atEnd()) {
        CanFrame f;
        quint64 ts;
        quint32 id;
        quint8 flags;

        stream >> ts >> id >> flags;
        f.timestamp = ts;
        f.id = id;
        f.extended = flags & 0x01;
        f.rtr      = flags & 0x02;
        f.error    = flags & 0x04;
        f.fd       = flags & 0x08;
        f.xl       = flags & 0x10;

        if (f.xl) {
            quint16 dlc16;
            stream >> dlc16;
            f.dlc = static_cast<quint8>(std::min(static_cast<int>(dlc16), 64));
        } else {
            quint8 dlc8;
            stream >> dlc8;
            f.dlc = dlc8;
        }

        int dataLen = std::min(static_cast<int>(f.dlc), 64);
        if (stream.readRawData(reinterpret_cast<char *>(f.data.data()), dataLen) != dataLen)
            break;

        m_frames.append(f);
    }

    m_currentIndex = 0;
    m_baseTimestamp = m_frames.isEmpty() ? 0 : m_frames[0].timestamp;

    if (!m_frames.isEmpty()) {
        m_loaded = true;
        qDebug() << "CanPlayer: wczytano" << m_frames.size() << "ramek z" << path;
    }
    return m_frames.size();
}

void CanPlayer::play() {
    if (!m_loaded) return;

    if (m_currentIndex >= m_frames.size()) {
        // Zatrzymano po końcu — przewiń na początek
        m_currentIndex = 0;
        m_baseTimestamp = m_frames[0].timestamp;
    }

    m_playing = true;
    emit positionChanged(m_currentIndex, m_frames.size());
    emitNextFrame();
}

void CanPlayer::pause() {
    m_playing = false;
    m_timer.stop();
}

void CanPlayer::stop() {
    m_playing = false;
    m_timer.stop();
    m_currentIndex = 0;
    if (m_loaded) {
        m_baseTimestamp = m_frames[0].timestamp;
        emit positionChanged(0, m_frames.size());
    }
}

void CanPlayer::setSpeed(float multiplier) {
    m_speed = std::max(0.0f, multiplier);
}

void CanPlayer::seekTo(int idx) {
    if (!m_loaded) return;
    m_currentIndex = std::max(0, std::min(idx, static_cast<int>(m_frames.size()) - 1));
    emit positionChanged(m_currentIndex, m_frames.size());
}

void CanPlayer::setOverride(int idx, const CanFrame &modified) {
    if (idx >= 0 && idx < m_frames.size())
        m_overrides[idx] = modified;
}

void CanPlayer::clearOverride(int idx) {
    m_overrides.remove(idx);
}

void CanPlayer::clearAllOverrides() {
    m_overrides.clear();
    m_skippedIndices.clear();
}

void CanPlayer::setSkipped(int idx, bool skip) {
    if (skip)
        m_skippedIndices.insert(idx);
    else
        m_skippedIndices.remove(idx);
}

bool CanPlayer::isSkipped(int idx) const {
    return m_skippedIndices.contains(idx);
}

bool CanPlayer::hasOverride(int idx) const {
    return m_overrides.contains(idx);
}

void CanPlayer::emitNextFrame() {
    if (!m_playing || m_currentIndex >= m_frames.size()) {
        m_playing = false;
        if (m_currentIndex >= m_frames.size())
            emit playbackFinished();
        return;
    }

    // Pomijaj oznaczone ramki
    while (m_currentIndex < m_frames.size() && m_skippedIndices.contains(m_currentIndex))
        ++m_currentIndex;

    if (m_currentIndex >= m_frames.size()) {
        m_playing = false;
        emit playbackFinished();
        return;
    }

    // Emituj override jeśli istnieje, zachowując oryginalny timestamp dla wyświetlania
    if (m_overrides.contains(m_currentIndex)) {
        CanFrame f = m_overrides[m_currentIndex];
        f.timestamp = m_frames[m_currentIndex].timestamp;
        emit frameEmitted(f);
    } else {
        emit frameEmitted(m_frames[m_currentIndex]);
    }

    ++m_currentIndex;
    emit positionChanged(m_currentIndex, m_frames.size());

    if (m_currentIndex >= m_frames.size()) {
        m_playing = false;
        emit playbackFinished();
        return;
    }

    // Oblicz delay do następnej ramki
    qint64 delta = static_cast<qint64>(m_frames[m_currentIndex].timestamp)
                   - static_cast<qint64>(m_frames[m_currentIndex - 1].timestamp);

    if (m_speed > 0.0f) {
        delta = static_cast<qint64>(delta / m_speed);
    } else {
        // m_speed == 0 → max (odtwarzaj tak szybko jak się da)
        delta = 0;
    }

    int delayMs = static_cast<int>(std::min(delta / 1000, 1000LL)); // max 1s na ramkę
    m_timer.start(std::max(1, delayMs));
}