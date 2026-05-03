#include "CanFrameModel.h"
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

    if (role == Qt::DisplayRole) {
        QMutexLocker lock(&m_mutex);
        const CanFrame &frame = m_frames.at(index.row());
        switch (index.column()) {
        case Column::ID:        return QString::number(frame.id, 16).toUpper().rightJustified(3, '0');
        case Column::EXT:       return frame.extended ? "EXT" : "STD";
        case Column::RTR:       return frame.rtr ? "RTR" : "Data";
        case Column::DLC:       return frame.dlc;
        case Column::DATA: {
            QString dataHex;
            for (int i = 0; i < frame.dlc; ++i) {
                dataHex += QString("%1 ").arg(frame.data[i], 2, 16, QChar('0')).toUpper();
            }
            return dataHex.trimmed();
        }
        case Column::TIMESTAMP: return QString("%1 µs").arg(frame.timestamp);
        default: break;
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
        }
    } else if (role == Qt::BackgroundRole) {
        return (index.row() % 2) ? QColor("#0d1117") : QColor("#161b22");
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
                m_frames[row] = frame;
                changedRows.append(row);
                continue;
            }
        }
        int newRow = m_frames.size();
        m_frames.append(frame);
        if (m_overwrite)
            m_idToRow.insert(frame.id, newRow);
    }
    lock.unlock();

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
    }
    endResetModel();
}

void CanFrameModel::clear() {
    beginResetModel();
    {
        QMutexLocker lock(&m_mutex);
        m_frames.clear();
        m_idToRow.clear();
    }
    endResetModel();
}
