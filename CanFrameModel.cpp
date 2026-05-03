#include "CanFrameModel.h"
#include <QColor>

CanFrameModel::CanFrameModel(QObject *parent) : QAbstractTableModel(parent) {
    m_pendingFrames.reserve(500); // prealokacja na batch
}

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
        // Cyberpunkowy neonowy tekst
        switch (index.column()) {
            case Column::ID:        return QColor("#00ffaa");
            case Column::EXT:       return QColor("#ff66cc");
            case Column::RTR:       return QColor("#ffaa00");
            case Column::DLC:       return QColor("#66ccff");
            case Column::DATA:      return QColor("#aa44ff");
            case Column::TIMESTAMP: return QColor("#888888");
        }
    } else if (role == Qt::BackgroundRole) {
        // Alternujące wiersze dla lepszego odczytu na ciemnym tle
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

void CanFrameModel::appendFrames(const QVector<CanFrame> &newFrames) {
    if (newFrames.isEmpty()) return;

    QMutexLocker lock(&m_mutex);
    // Przenieś oczekujące ramki do tymczasowego, aby zredukować lock zone
    m_pendingFrames.append(newFrames);
    lock.unlock();

    // Aktualizuj model poza mutexem? Niestety, beginInsertRows/endInsertRows muszą być w wątku GUI.
    // Wywołanie tej metody przez timer gwarantuje, że jesteśmy w GUI.
    if (!m_pendingFrames.isEmpty()) {
        int start = m_frames.size();
        int count = m_pendingFrames.size();
        beginInsertRows(QModelIndex(), start, start + count - 1);
        {
            QMutexLocker relock(&m_mutex); // ponowna blokada, aby dodać do m_frames
            m_frames.append(m_pendingFrames);
            m_pendingFrames.clear();
        }
        endInsertRows();
    }
}
