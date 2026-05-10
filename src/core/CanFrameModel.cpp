#include "CanFrameModel.h"
#include "DbcParser.h"
#include "J1939Parser.h"
#include <QColor>
#include <algorithm>

CanFrameModel::CanFrameModel(QObject *parent) : QAbstractTableModel(parent) {
    // Pre-alokuj wektory do m_maxFrames — zero realokacji w runtime,
    // dostęp przez m_head + m_size (bufor cykliczny).
    if (m_maxFrames > 0) {
        m_frames.resize(m_maxFrames);
        m_deltas.resize(m_maxFrames);
        m_isBurst.resize(m_maxFrames);
        m_cache.resize(m_maxFrames);
    }
}

int CanFrameModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    QMutexLocker lock(&m_mutex);
    return m_size;
}

int CanFrameModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return Column::_COUNT;
}

QVariant CanFrameModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_size)
        return {};

    QMutexLocker lock(&m_mutex);
    int phys = physRow(index.row());
    const CanFrame &frame = m_frames.at(phys);

    if (role == Qt::DisplayRole) {
        int row = index.row();
        // Sprawdź cache
        auto &c = m_cache[phys];
        if (c.valid) {
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
        }
        // Cache miss — oblicz i zapisz wszystkie kolumny na raz
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
        if (m_deltas[phys] > 0)
            c.delta = QString("%1 µs").arg(m_deltas[phys]);
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
        if (m_isBurst[phys])
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
        case Column::DELTA:     return QVariant::fromValue(m_deltas[phys]);
        default: return {};
        }
    } else if (role == ChangedMaskRole && index.column() == Column::DATA) {
        return QVariant::fromValue(frame.changedMask);
    } else if (role == BurstRole) {
        return m_isBurst[phys];
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

    // Pierwsze wywołanie — pre-alokuj jeśli jeszcze nie zrobione
    if (m_maxFrames > 0 && m_frames.size() < m_maxFrames) {
        m_frames.resize(m_maxFrames);
        m_deltas.resize(m_maxFrames);
        m_isBurst.resize(m_maxFrames);
        m_cache.resize(m_maxFrames);
    }

    QVector<int> changedRows;
    const int oldSize = m_size;
    int appendedCount = 0;

    for (const CanFrame &frame : newFrames) {
        if (m_overwrite) {
            auto it = m_idToRow.find(frame.id);
            if (it != m_idToRow.end()) {
                int logicalRow = it.value();
                int phys = physRow(logicalRow);
                // Śledź zmiany bajtów — zapisz jako bitmask (64 bity)
                uint64_t mask = 0;
                QVector<uint8_t> prev = m_previousData.value(frame.id);
                for (int i = 0; i < frame.dlc && i < prev.size() && i < 64; ++i) {
                    if (frame.data[i] != prev[i])
                        mask |= (1ULL << i);
                }
                CanFrame f = frame;
                f.changedMask = mask;
                m_frames[phys] = f;
                m_cache[phys].valid = false;
                changedRows.append(logicalRow);
                // Zapisz poprzednie dane
                QVector<uint8_t> cur(64, 0);
                for (int i = 0; i < frame.dlc && i < 64; ++i) cur[i] = frame.data[i];
                m_previousData[frame.id] = cur;
                continue;
            }
        }
        // Append nowej ramki
        int logicalRow = m_size + appendedCount;
        int phys = physRow(logicalRow);

        // Oblicz deltę czasu
        uint64_t delta = 0;
        auto lastIt = m_lastTimestampPerId.find(frame.id);
        if (lastIt != m_lastTimestampPerId.end() && frame.timestamp > lastIt.value())
            delta = frame.timestamp - lastIt.value();
        m_lastTimestampPerId[frame.id] = frame.timestamp;
        m_deltas[phys] = delta;

        // Burst detection: < 1000µs od poprzedniej ramki tego ID
        bool burst = false;
        auto burstIt = m_lastBurstTs.find(frame.id);
        if (burstIt != m_lastBurstTs.end()) {
            uint64_t burstDelta = (frame.timestamp > burstIt.value())
                ? frame.timestamp - burstIt.value() : 0;
            burst = (burstDelta < 1000);
        }
        m_lastBurstTs[frame.id] = frame.timestamp;
        m_isBurst[phys] = burst;

        m_frames[phys] = frame;
        if (m_overwrite)
            m_idToRow.insert(frame.id, logicalRow);

        ++appendedCount;
    }
    lock.unlock();

    // Emituj batch ramek do WebSocket broadcast (jedna tablica JSON)
    emit frameBatchUpdated(newFrames);

    // --- Obsługa przepełnienia: usuń najstarsze wiersze (bez kopiowania!) ---
    int overflow = 0;
    if (m_maxFrames > 0 && m_size + appendedCount > m_maxFrames)
        overflow = m_size + appendedCount - m_maxFrames;

    if (overflow > 0) {
        beginRemoveRows(QModelIndex(), 0, overflow - 1);
        {
            QMutexLocker lock2(&m_mutex);
            // Przesuń głowę — najstarsze ramki są logicznie usunięte, zero kopiowania
            m_head = (m_head + overflow) % m_maxFrames;
            m_size -= overflow;
            // Przebuduj mapę ID→wiersz (indeksy logiczne przesunęły się)
            m_idToRow.clear();
            for (int i = 0; i < m_size; ++i) {
                int phys = physRow(i);
                if (m_frames[phys].id != 0)
                    m_idToRow[m_frames[phys].id] = i;
            }
        }
        endRemoveRows();
    }

    // --- Wstaw nowe wiersze ---
    if (appendedCount > 0) {
        int insertStart = m_size;
        beginInsertRows(QModelIndex(), insertStart, insertStart + appendedCount - 1);
        {
            QMutexLocker lock3(&m_mutex);
            m_size += appendedCount;
        }
        endInsertRows();
    }

    // --- Skoryguj changedRows po przesunięciu głowy ---
    if (overflow > 0 && !changedRows.isEmpty()) {
        QVector<int> adjusted;
        for (int row : changedRows) {
            int adj = row - overflow;
            if (adj >= 0) adjusted.append(adj);
        }
        changedRows = adjusted;
    }

    // --- Emituj dataChanged dla nadpisanych wierszy ---
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
}

void CanFrameModel::setOverwriteMode(bool enabled) {
    {
        QMutexLocker lock(&m_mutex);
        if (m_overwrite == enabled) return;
        m_overwrite = enabled;
    }
    if (m_size > 0) saveUndoState();
    beginResetModel();
    {
        QMutexLocker lock(&m_mutex);
        m_head = 0;
        m_size = 0;
        m_idToRow.clear();
        m_lastTimestampPerId.clear();
        m_previousData.clear();
        m_lastBurstTs.clear();
        // Wektory pozostają z pre-alokowaną pojemnością — tylko
        // czyścimy cache (valid=false). Danych nie trzeba zerować,
        // bo m_size=0 — są logicznie puste.
        for (auto &c : m_cache) c.valid = false;
    }
    endResetModel();
}

CanFrame CanFrameModel::frameAt(int row) const {
    QMutexLocker lock(&m_mutex);
    if (row >= 0 && row < m_size)
        return m_frames.at(physRow(row));
    return CanFrame();
}

void CanFrameModel::clear() {
    if (m_size > 0) saveUndoState();
    beginResetModel();
    {
        QMutexLocker lock(&m_mutex);
        m_head = 0;
        m_size = 0;
        m_idToRow.clear();
        m_lastTimestampPerId.clear();
        m_previousData.clear();
        m_lastBurstTs.clear();
        for (auto &c : m_cache) c.valid = false;
    }
    endResetModel();
}

QVector<CanFrame> CanFrameModel::allFrames() const {
    QMutexLocker lock(&m_mutex);
    QVector<CanFrame> result;
    result.reserve(m_size);
    for (int i = 0; i < m_size; ++i)
        result.append(m_frames.at(physRow(i)));
    return result;
}

void CanFrameModel::invalidateRowCache(int row) {
    if (row >= 0 && row < m_size)
        m_cache[physRow(row)].valid = false;
}

// ── Undo/Redo ────────────────────────────────────────────────

void CanFrameModel::saveUndoState() {
    QMutexLocker lock(&m_mutex);
    if (m_size == 0) return;

    Snapshot snap;
    snap.head = m_head;
    snap.size = m_size;

    // Kopiuj dane w logicznej kolejności
    snap.frames.reserve(m_size);
    snap.deltas.reserve(m_size);
    snap.isBurst.reserve(m_size);
    for (int i = 0; i < m_size; ++i) {
        int phys = physRow(i);
        snap.frames.append(m_frames[phys]);
        snap.deltas.append(m_deltas[phys]);
        snap.isBurst.append(m_isBurst[phys]);
    }
    snap.idToRow = m_idToRow;
    snap.previousData = m_previousData;
    snap.lastTimestampPerId = m_lastTimestampPerId;
    snap.lastBurstTs = m_lastBurstTs;

    m_undoStack.append(std::move(snap));
    if (m_undoStack.size() > MAX_UNDO)
        m_undoStack.erase(m_undoStack.begin());

    m_redoStack.clear(); // nowa operacja → redo traci sens
}

void CanFrameModel::restoreSnapshot(const Snapshot &snap) {
    QMutexLocker lock(&m_mutex);

    m_head = snap.head;
    m_size = snap.size;

    // Zapisz dane w fizycznej kolejności
    for (int i = 0; i < snap.size; ++i) {
        int phys = physRow(i);
        m_frames[phys] = snap.frames[i];
        m_deltas[phys] = snap.deltas[i];
        m_isBurst[phys] = snap.isBurst[i];
    }
    m_idToRow = snap.idToRow;
    m_previousData = snap.previousData;
    m_lastTimestampPerId = snap.lastTimestampPerId;
    m_lastBurstTs = snap.lastBurstTs;

    // Invalidate cache
    for (auto &c : m_cache) c.valid = false;
}

void CanFrameModel::undo() {
    if (m_undoStack.isEmpty()) return;

    // Zapisz bieżący stan na stosie redo
    {
        QMutexLocker lock(&m_mutex);
        if (m_size == 0) return; // nic do cofnięcia
    }

    Snapshot current;
    {
        QMutexLocker lock(&m_mutex);
        current.head = m_head;
        current.size = m_size;
        current.frames.reserve(m_size);
        current.deltas.reserve(m_size);
        current.isBurst.reserve(m_size);
        for (int i = 0; i < m_size; ++i) {
            int phys = physRow(i);
            current.frames.append(m_frames[phys]);
            current.deltas.append(m_deltas[phys]);
            current.isBurst.append(m_isBurst[phys]);
        }
        current.idToRow = m_idToRow;
        current.previousData = m_previousData;
        current.lastTimestampPerId = m_lastTimestampPerId;
        current.lastBurstTs = m_lastBurstTs;
    }

    // Pobierz poprzedni stan
    Snapshot prev = m_undoStack.takeLast();
    m_redoStack.append(std::move(current));

    beginResetModel();
    restoreSnapshot(prev);
    endResetModel();
}

void CanFrameModel::redo() {
    if (m_redoStack.isEmpty()) return;

    // Zapisz bieżący stan na stosie undo
    Snapshot current;
    {
        QMutexLocker lock(&m_mutex);
        current.head = m_head;
        current.size = m_size;
        current.frames.reserve(m_size);
        current.deltas.reserve(m_size);
        current.isBurst.reserve(m_size);
        for (int i = 0; i < m_size; ++i) {
            int phys = physRow(i);
            current.frames.append(m_frames[phys]);
            current.deltas.append(m_deltas[phys]);
            current.isBurst.append(m_isBurst[phys]);
        }
        current.idToRow = m_idToRow;
        current.previousData = m_previousData;
        current.lastTimestampPerId = m_lastTimestampPerId;
        current.lastBurstTs = m_lastBurstTs;
    }

    Snapshot next = m_redoStack.takeLast();
    m_undoStack.append(std::move(current));

    beginResetModel();
    restoreSnapshot(next);
    endResetModel();
}