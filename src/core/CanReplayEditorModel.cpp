#include "CanReplayEditorModel.h"
#include <QColor>

CanReplayEditorModel::CanReplayEditorModel(CanPlayer *player, QObject *parent)
    : QAbstractTableModel(parent), m_player(player)
{}

void CanReplayEditorModel::reload() {
    beginResetModel();
    m_rowCount = m_player->totalFrames();
    endResetModel();
}

void CanReplayEditorModel::resetAllEdits() {
    m_player->clearAllOverrides();
    if (m_rowCount > 0)
        emit dataChanged(index(0, 0), index(m_rowCount - 1, ColCount - 1));
}

int CanReplayEditorModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_rowCount;
}

int CanReplayEditorModel::columnCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : ColCount;
}

CanFrame CanReplayEditorModel::effectiveFrame(int row) const {
    return m_player->effectiveFrameAt(row);
}

bool CanReplayEditorModel::isModified(int row) const {
    return m_player->hasOverride(row) || m_player->isSkipped(row);
}

QString CanReplayEditorModel::payloadHex(const CanFrame &f) const {
    QString s;
    int len = std::min((int)f.dlc, 64);
    for (int i = 0; i < len; ++i) {
        if (i > 0) s += ' ';
        s += QString("%1").arg(f.data[i], 2, 16, QChar('0')).toUpper();
    }
    return s;
}

bool CanReplayEditorModel::parsePayloadHex(const QString &hex, CanFrame &f) const {
    QStringList parts = hex.trimmed().split(' ', Qt::SkipEmptyParts);
    if (parts.size() > 64) return false;
    f.dlc = static_cast<uint8_t>(parts.size());
    f.data.fill(0);
    for (int i = 0; i < parts.size(); ++i) {
        bool ok = false;
        uint val = parts[i].toUInt(&ok, 16);
        if (!ok || val > 255) return false;
        f.data[i] = static_cast<uint8_t>(val);
    }
    return true;
}

QVariant CanReplayEditorModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_rowCount)
        return {};

    int row = index.row();
    int col = index.column();

    CanFrame f = m_player->effectiveFrameAt(row);

    if (role == Qt::CheckStateRole && col == ColEnabled)
        return m_player->isSkipped(row) ? Qt::Unchecked : Qt::Checked;

    if (role == Qt::CheckStateRole && col == ColExt)
        return f.extended ? Qt::Checked : Qt::Unchecked;

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (col) {
        case ColNum:       return row + 1;
        case ColEnabled:   return {};
        case ColTimestamp: return QString::number(f.timestamp);
        case ColId:        return QString("%1").arg(f.id, f.extended ? 8 : 3, 16, QChar('0')).toUpper();
        case ColExt:       return {};
        case ColDlc:       return f.dlc;
        case ColPayload:   return payloadHex(f);
        default:           return {};
        }
    }

    if (role == Qt::ForegroundRole) {
        if (isModified(row))
            return QColor("#ffaa00");  // zmienione wiersze — pomarańczowe
    }

    if (role == Qt::BackgroundRole && m_player->isSkipped(row))
        return QColor("#2a1a1a");

    return {};
}

bool CanReplayEditorModel::setData(const QModelIndex &idx, const QVariant &value, int role) {
    if (!idx.isValid() || idx.row() >= m_rowCount)
        return false;

    int row = idx.row();
    int col = idx.column();

    if (role == Qt::CheckStateRole && col == ColEnabled) {
        bool enabled = (value.toInt() == Qt::Checked);
        m_player->setSkipped(row, !enabled);
        emit dataChanged(idx, idx, {Qt::CheckStateRole, Qt::BackgroundRole, Qt::ForegroundRole});
        return true;
    }

    if (role == Qt::EditRole) {
        CanFrame f = m_player->effectiveFrameAt(row);

        bool changed = false;
        switch (col) {
        case ColId: {
            bool ok = false;
            uint32_t newId = value.toString().toUInt(&ok, 16);
            if (!ok) return false;
            f.id = newId;
            changed = true;
            break;
        }
        case ColExt: {
            f.extended = value.toBool();
            changed = true;
            break;
        }
        case ColDlc: {
            bool ok = false;
            int dlc = value.toInt(&ok);
            if (!ok || dlc < 0 || dlc > 64) return false;
            f.dlc = static_cast<uint8_t>(dlc);
            changed = true;
            break;
        }
        case ColPayload: {
            if (!parsePayloadHex(value.toString(), f)) return false;
            changed = true;
            break;
        }
        default:
            return false;
        }

        if (changed) {
            m_player->setOverride(row, f);
            emit dataChanged(index(row, 0), index(row, ColCount - 1),
                             {Qt::DisplayRole, Qt::ForegroundRole});
        }
        return changed;
    }

    return false;
}

Qt::ItemFlags CanReplayEditorModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    int col = index.column();
    if (col == ColEnabled || col == ColExt)
        f |= Qt::ItemIsUserCheckable;
    if (col == ColId || col == ColDlc || col == ColPayload)
        f |= Qt::ItemIsEditable;
    return f;
}

QVariant CanReplayEditorModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    switch (section) {
    case ColNum:       return "#";
    case ColEnabled:   return "Aktywna";
    case ColTimestamp: return "Timestamp";
    case ColId:        return "ID";
    case ColExt:       return "EXT";
    case ColDlc:       return "DLC";
    case ColPayload:   return "Payload";
    default:           return {};
    }
}
