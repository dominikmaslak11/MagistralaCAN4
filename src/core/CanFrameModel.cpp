#include "CanFrameModel.h"
#include "DbcParser.h"
#include "J1939Parser.h"
#include <QColor>

CanFrameModel::CanFrameModel(QObject *parent) : QAbstractTableModel(parent) {}

int CanFrameModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    QMutexLocker lock(&m_mutex);
    return m_frames.size();
}

int CanFrameModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return Column::_COUNT;
}

QVariant CanFrameModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_frames.size())
        return {};

    QMutexLocker lock(&m_mutex);
    const CanFrame &frame = m_frames.at(index.row());

    if (role == Qt::DisplayRole) {
        int row = index.row();
        // Sprawdź cache
        if (row < m_cache.size() && m_cache[row].valid) {
            switch (index.column()) {
            case Column::ID:        return m_cache[row].id;
            case Column::EXT:       return m_cache[row].ext;
            case Column::RTR:       return m_cache[row].rtr;
            case Column::DLC:       return m_cache[row].dlc;
            case Column::DATA:      return m_cache[row].data;
            case Column::TIMESTAMP: return m_cache[row].timestamp;
            case Column::FD:        return m_cache[row].fd;
            case Column::DELTA:     return m_cache[row].delta;
            case Column::SIGNAL:    return m_cache[row].signal;
            }
        }
        // Cache miss — oblicz i zapisz wszystkie kolumny na raz
        if (row >= m_cache.size())
            m_cache.resize(row + 1);
        auto &c = m_cache[row];
        c.id        = QString::number(frame.id, 16).toUpper().rightJustified(3, '0');
        c.ext       = frame.extended ? QStringLiteral("EXT") : QStringLiteral("STD");
        c.rtr       = frame.rtr ? QStringLiteral("RTR") : QStringLiteral("Data");
        c.dlc       = QString::number(frame.dlc);
        {
            QString dataHex;
            int maxShow = frame.xl ? 16 : (frame.fd ? 64 : 8);
            for (int i = 0; i < frame.dlc && i < maxShow; ++i)
                dataHex += QString("%1 ").arg(frame.data[i], 2, 16, QChar('0')).toUpper();
            if (frame.dlc > maxShow)
                dataHex += QString("... (%1 bajtów)").arg(frame.dlc);
            c.data = dataHex.trimmed();
        }
        c.timestamp = QString("%1 µs").arg(frame.timestamp);
        c.fd        = frame.xl ? QStringLiteral("XL") : frame.fd ? QStringLiteral("FD") : QStringLiteral("CAN");
        if (row < m_deltas.size() && m_deltas[row] > 0)
            c.delta = QString("%1 µs").arg(m_deltas[row]);
        else
            c.delta = QStringLiteral("\u2014"); // em dash
        {
            QString desc;
            if (m_dbc && frame.id) {
                DbcMessage dm = m_dbc->messageForId(frame.id);
                if (dm.id != 0 && !dm.sigList.isEmpty())
                    desc = dm.sigList.first().name;
            }
            if (desc.isEmpty() && m_j1939 && (frame.id & 0x80000000)) {
                uint32_t pf = (frame.id >> 16) & 0xFF;
                uint32_t ps = (frame.id >> 8) & 0xFF;
                uint32_t dp = (frame.id >> 24) & 0x1;
                uint32_t r  = (frame.id >> 25) & 0x1;
                uint32_t pgn = (r << 17) | (dp << 16) | (pf << 8) | (pf < 240 ? ps : 0);
                desc = m_j1939->pgnName(pgn);
            }
            c.signal = desc;
        }
        c.valid = true;
        switch (index.column()) {
        case Column::ID:        return c.id;
        case Column::EXT:       return c.ext;
        case Column::RTR:       return c.rtr;
        case Column::DLC:       return c.dlc;
        case Column::DATA:      return c.data;
        case Column::TIMESTAMP: return c.timestamp;
        case Column::FD:        return c.fd;
        case Column::DELTA:     return c.delta;
        case Column::SIGNAL:    return c.signal;
        }
    } else if (role == Qt::TextAlignmentRole) {
        return Qt::AlignCenter;
    } else if (role == Qt::ForegroundRole) {
        switch (index.column()) {
        case Column::ID:        return QColor("#00ffaa");
        case Column::EXT:       return QColor("#ff66cc");
        case Column::RTR:       return QColor("#ffaa00");
        case Column::DLC:       return QColor("#66ccff");
        case Column::DATA:      return QColor("#aa44ff");
        case Column::TIMESTAMP: return QColor("#888888");
        case Column::FD:        return QColor(frame.xl ? "#ff4444" : frame.fd ? "#00ffaa" : "#666666");
        case Column::DELTA:     return QColor("#8888cc");
        case Column::SIGNAL:    return QColor("#ffaa00");
        }
    } else if (role == Qt::ToolTipRole && index.column() == Column::DATA && frame.xl) {
        QString fullHex;
        for (int i = 0; i < frame.dlc && i < 64; ++i)
            fullHex += QString("%1 ").arg(frame.data[i], 2, 16, QChar('0')).toUpper();
        if (frame.dlc > 256) fullHex += "...";
        return QString("CAN XL %1 bajtów:\n%2").arg(frame.dlc).arg(fullHex.trimmed());
    } else if (role == Qt::ToolTipRole && index.column() == Column::SIGNAL) {
        QString tip;
        if (m_dbc && frame.id) {
            DbcMessage dm = m_dbc->messageForId(frame.id);
            if (dm.id != 0) {
                QStringList parts;
                for (const auto &sig : dm.sigList) {
                    int byteIdx = sig.startBit / 8;
                    if (byteIdx < frame.dlc) {
                        double val = frame.data[byteIdx] * sig.scale + sig.offset;
                        parts.append(QString("%1 = %2 %3").arg(sig.name).arg(val, 0, 'f', 2).arg(sig.unit));
                    }
                }
                tip = parts.join("\n");
            }
        }
        return tip.isEmpty() ? QVariant() : tip;
    } else if (role == Qt::BackgroundRole) {
        if (index.row() < m_isBurst.size() && m_isBurst[index.row()])
            return QColor("#2e1a0a"); // ciemnopomarańczowe dla burstów
        // Kolorowanie wg zakresu ID
        static const QColor idColors[] = {
            QColor("#0d1117"), QColor("#111a1a"), QColor("#1a111a"),
            QColor("#1a1a11"), QColor("#111a11"), QColor("#1a1111")
        };
        int colorIdx = (frame.id / 0x100) % 6;
        return idColors[colorIdx];
    } else if (role == Qt::UserRole) {
        // Wartości numeryczne do sortowania
        switch (index.column()) {
        case Column::ID:        return frame.id;
        case Column::DLC:       return frame.dlc;
        case Column::TIMESTAMP: return QVariant::fromValue(frame.timestamp);
        case Column::DELTA: {
            if (index.row() < m_deltas.size()) return QVariant::fromValue(m_deltas[index.row()]);
            return QVariant::fromValue(0ULL);
        }
        default: return {};
        }
    }
    return {};
}

QVariant CanFrameModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case Column::ID:        return "CAN ID";
        case Column::EXT:       return "Typ";
        case Column::RTR:       return "RTR";
        case Column::DLC:       return "DLC";
        case Column::DATA:      return "Dane (hex)";
        case Column::TIMESTAMP: return "Czas [µs]";
        case Column::FD:        return "FD";
        case Column::DELTA:     return "Delta";
        case Column::SIGNAL:    return "DBC / J1939";
        }
    }
    return {};
}

void CanFrameModel::processIncomingFrames(const QVector<CanFrame> &newFrames) {
    if (newFrames.isEmpty()) return;

    QMutexLocker lock(&m_mutex);
    QVector<int> changedRows;
    int oldSize = m_frames.size();

    for (const CanFrame &frame : newFrames) {
        if (m_overwrite) {
            auto it = m_idToRow.find(frame.id);
            if (it != m_idToRow.end()) {
                int row = it.value();
                // Śledź zmiany bajtów
                QVector<uint8_t> prev = m_previousData.value(frame.id);
                QVector<uint8_t> changed(64, 0);
                for (int i = 0; i < frame.dlc && i < prev.size(); ++i)
                    changed[i] = (frame.data[i] != prev[i]) ? 1 : 0;
                // Zapisz zmiany jako metadane w CanFrame (użyj pola error tymczasowo)
                m_frames[row] = frame;
                m_cache[row].valid = false;  // invalidate cache
                changedRows.append(row);
                // Zapisz poprzednie dane
                QVector<uint8_t> cur(64, 0);
                for (int i = 0; i < frame.dlc && i < 64; ++i) cur[i] = frame.data[i];
                m_previousData[frame.id] = cur;
                continue;
            }
        }
        int newRow = m_frames.size();
        // Oblicz deltę czasu
        uint64_t delta = 0;
        auto lastIt = m_lastTimestampPerId.find(frame.id);
        if (lastIt != m_lastTimestampPerId.end() && frame.timestamp > lastIt.value())
            delta = frame.timestamp - lastIt.value();
        m_lastTimestampPerId[frame.id] = frame.timestamp;
        m_deltas.append(delta);

        // Burst detection: < 1000µs od poprzedniej ramki tego ID
        bool burst = false;
        auto burstIt = m_lastBurstTs.find(frame.id);
        if (burstIt != m_lastBurstTs.end()) {
            uint64_t burstDelta = (frame.timestamp > burstIt.value())
                ? frame.timestamp - burstIt.value() : 0;
            burst = (burstDelta < 1000);
        }
        m_lastBurstTs[frame.id] = frame.timestamp;
        m_isBurst.append(burst);

        m_frames.append(frame);
        if (m_overwrite)
            m_idToRow.insert(frame.id, newRow);
    }
    lock.unlock();

    // Emituj sygnał do WebSocket broadcast dla każdej nowej/zmienionej ramki
    for (const CanFrame &frame : newFrames)
        emit frameUpdated(frame);

    int totalInserted = m_frames.size() - oldSize;
    if (totalInserted > 0) {
        beginInsertRows(QModelIndex(), oldSize, oldSize + totalInserted - 1);
        endInsertRows();
    }

    if (!changedRows.isEmpty()) {
        std::sort(changedRows.begin(), changedRows.end());
        changedRows.erase(std::unique(changedRows.begin(), changedRows.end()), changedRows.end());
        QVector<QPair<int,int>> ranges;
        int start = changedRows.first();
        int end = start;
        for (int i = 1; i < changedRows.size(); ++i) {
            if (changedRows[i] == end + 1) {
                end = changedRows[i];
            } else {
                ranges.append({start, end});
                start = changedRows[i];
                end = start;
            }
        }
        ranges.append({start, end});
        for (const auto &r : ranges) {
            emit dataChanged(index(r.first, 0), index(r.second, Column::_COUNT - 1));
        }
    }

    // Auto-cleanup: usuń najstarsze jeśli przekroczono limit
    if (m_maxFrames > 0 && m_frames.size() > m_maxFrames) {
        int toRemove = m_frames.size() - m_maxFrames;
        beginRemoveRows(QModelIndex(), 0, toRemove - 1);
        m_frames.erase(m_frames.begin(), m_frames.begin() + toRemove);
        if (!m_deltas.isEmpty())
            m_deltas.erase(m_deltas.begin(), m_deltas.begin() + toRemove);
        if (!m_isBurst.isEmpty())
            m_isBurst.erase(m_isBurst.begin(), m_isBurst.begin() + toRemove);
        if (!m_cache.isEmpty())
            m_cache.erase(m_cache.begin(), m_cache.begin() + toRemove);
        m_idToRow.clear(); // odbuduj mapę
        for (int i = 0; i < m_frames.size(); ++i)
            m_idToRow[m_frames[i].id] = i;
        endRemoveRows();
    }
}

void CanFrameModel::setOverwriteMode(bool enabled) {
    {
        QMutexLocker lock(&m_mutex);
        if (m_overwrite == enabled) return;
        m_overwrite = enabled;
    }
    beginResetModel();
    {
        QMutexLocker lock(&m_mutex);
        m_frames.clear();
        m_idToRow.clear();
        m_deltas.clear();
        m_lastTimestampPerId.clear();
        m_previousData.clear();
        m_isBurst.clear();
        m_lastBurstTs.clear();
        m_cache.clear();
    }
    endResetModel();
}

CanFrame CanFrameModel::frameAt(int row) const {
    QMutexLocker lock(&m_mutex);
    if (row >= 0 && row < m_frames.size())
        return m_frames.at(row);
    return CanFrame();
}

void CanFrameModel::clear() {
    beginResetModel();
    {
        QMutexLocker lock(&m_mutex);
        m_frames.clear();
        m_idToRow.clear();
        m_deltas.clear();
        m_lastTimestampPerId.clear();
        m_previousData.clear();
        m_isBurst.clear();
        m_lastBurstTs.clear();
        m_cache.clear();
    }
    endResetModel();
}

QVector<CanFrame> CanFrameModel::allFrames() const {
    QMutexLocker lock(&m_mutex);
    return m_frames;
}

void CanFrameModel::invalidateRowCache(int row) {
    if (row >= 0 && row < m_cache.size())
        m_cache[row].valid = false;
}


