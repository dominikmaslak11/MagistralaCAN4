#include "CanSignalMapperModel.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QColor>
#include <cmath>
#include <algorithm>
#include <limits>

// ── Bit-accurate signal extraction (Intel/Motorola) ───────────────────────────
// Intel (LE): startBit = LSBit position, bits consecutive in memory
// Motorola (BE): startBit = MSBit in DBC bit numbering (byte*8 + bit_within_byte)

namespace {

double extractSignal(const uint8_t *data, int dlc,
                      int startBit, int length,
                      bool isLittleEndian, bool isSigned,
                      double scale, double offset)
{
    if (length <= 0 || length > 64)
        return std::numeric_limits<double>::quiet_NaN();

    uint64_t raw = 0;

    if (isLittleEndian) {
        for (int i = 0; i < length; ++i) {
            int bitPos  = startBit + i;
            int byteIdx = bitPos / 8;
            int bitIdx  = bitPos % 8;
            if (byteIdx >= dlc || byteIdx < 0) break;
            if ((data[byteIdx] >> bitIdx) & 1u)
                raw |= (1ULL << i);
        }
    } else {
        // Motorola: traverse MSBit → LSBit in DBC bit ordering
        int bitPos = startBit;
        for (int i = 0; i < length; ++i) {
            int byteIdx = bitPos / 8;
            int bitIdx  = bitPos % 8;
            if (byteIdx >= dlc || byteIdx < 0) break;
            if ((data[byteIdx] >> bitIdx) & 1u)
                raw |= (1ULL << (length - 1 - i));
            // Advance: within byte go right-to-left; at byte boundary jump to MSBit of next byte
            if (bitIdx == 0)
                bitPos += 15;
            else
                --bitPos;
        }
    }

    // Sign extend
    if (isSigned && length < 64) {
        if (raw & (1ULL << (length - 1)))
            raw |= ~((1ULL << length) - 1ULL);
    }

    if (isSigned)
        return static_cast<double>(static_cast<int64_t>(raw)) * scale + offset;
    return static_cast<double>(raw) * scale + offset;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

CanSignalMapperModel::CanSignalMapperModel(QObject *parent)
    : QAbstractTableModel(parent) {}

int CanSignalMapperModel::rowCount   (const QModelIndex &) const { return static_cast<int>(m_signals.size()); }
int CanSignalMapperModel::columnCount(const QModelIndex &) const { return ColCount; }

QVariant CanSignalMapperModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const char *headers[] = {
        "Nazwa", "CAN ID", "Bit start", "Dług.", "Endian", "Ze znakiem",
        "Scale", "Offset", "Jednostka", "Wartość live"
    };
    if (section < 0 || section >= ColCount) return {};
    return QLatin1String(headers[section]);
}

QVariant CanSignalMapperModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_signals.size()) return {};
    const auto &s = m_signals[index.row()];
    const int col = index.column();

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (col) {
        case ColName:     return s.name;
        case ColId:       return QString("0x%1").arg(s.canId, 3, 16, QChar('0')).toUpper();
        case ColStartBit: return s.startBit;
        case ColLength:   return s.length;
        case ColEndian:   return s.isLittleEndian ? "LE" : "BE";
        case ColSigned:   return s.isSigned ? "Tak" : "Nie";
        case ColScale:    return s.scale;
        case ColOffset:   return s.offset;
        case ColUnit:     return s.unit;
        case ColLive:
            if (std::isnan(s.liveValue)) return QStringLiteral("—");
            return QString::number(s.liveValue, 'f', 4)
                   + (s.unit.isEmpty() ? QString() : QStringLiteral(" ") + s.unit);
        }
    }

    if (role == Qt::ForegroundRole && col == ColLive && !std::isnan(s.liveValue))
        return QColor("#00ffaa");

    return {};
}

Qt::ItemFlags CanSignalMapperModel::flags(const QModelIndex &index) const {
    if (!index.isValid()) return Qt::NoItemFlags;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() != ColLive)
        f |= Qt::ItemIsEditable;
    return f;
}

bool CanSignalMapperModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    if (!index.isValid() || role != Qt::EditRole) return false;
    auto &s = m_signals[index.row()];
    switch (index.column()) {
    case ColName:     s.name = value.toString(); break;
    case ColId: {
        bool ok = false;
        uint32_t id = value.toString().toUInt(&ok, 16);
        if (!ok) return false;
        s.canId = id;
        break;
    }
    case ColStartBit: s.startBit = value.toInt(); break;
    case ColLength:   s.length   = std::max(1, std::min(64, value.toInt())); break;
    case ColEndian:   s.isLittleEndian = (value.toString().toUpper() == "LE"); break;
    case ColSigned:   s.isSigned = (value.toString().toLower() == "tak" || value.toBool()); break;
    case ColScale:    s.scale    = value.toDouble(); break;
    case ColOffset:   s.offset   = value.toDouble(); break;
    case ColUnit:     s.unit     = value.toString(); break;
    default: return false;
    }
    emit dataChanged(index, index, {role});
    return true;
}

void CanSignalMapperModel::addSignal(const MapperSignal &sig) {
    beginInsertRows({}, static_cast<int>(m_signals.size()), static_cast<int>(m_signals.size()));
    m_signals.append(sig);
    endInsertRows();
}

void CanSignalMapperModel::removeSignal(int row) {
    if (row < 0 || row >= m_signals.size()) return;
    beginRemoveRows({}, row, row);
    m_signals.removeAt(row);
    endRemoveRows();
}

void CanSignalMapperModel::clearSignals() {
    if (m_signals.isEmpty()) return;
    beginResetModel();
    m_signals.clear();
    endResetModel();
}

void CanSignalMapperModel::processFrame(const CanFrame &frame) {
    for (int i = 0; i < m_signals.size(); ++i) {
        auto &s = m_signals[i];
        if (s.canId != frame.id) continue;
        double v = extractSignal(frame.data.data(), frame.dlc,
                                 s.startBit, s.length,
                                 s.isLittleEndian, s.isSigned,
                                 s.scale, s.offset);
        if (v == s.liveValue) continue;
        s.liveValue = v;
        QModelIndex idx = createIndex(i, ColLive);
        emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::ForegroundRole});
    }
}

// ── JSON persistence ──────────────────────────────────────────────────────────

bool CanSignalMapperModel::saveToFile(const QString &path) const {
    QJsonArray arr;
    for (const auto &s : m_signals) {
        QJsonObject o;
        o["name"]           = s.name;
        o["canId"]          = static_cast<int>(s.canId);
        o["startBit"]       = s.startBit;
        o["length"]         = s.length;
        o["isLittleEndian"] = s.isLittleEndian;
        o["isSigned"]       = s.isSigned;
        o["scale"]          = s.scale;
        o["offset"]         = s.offset;
        o["unit"]           = s.unit;
        arr.append(o);
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(arr).toJson());
    return true;
}

bool CanSignalMapperModel::loadFromFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) return false;

    beginResetModel();
    m_signals.clear();
    for (const auto &v : doc.array()) {
        QJsonObject o = v.toObject();
        MapperSignal s;
        s.name           = o["name"].toString();
        s.canId          = static_cast<uint32_t>(o["canId"].toInt());
        s.startBit       = o["startBit"].toInt();
        s.length         = o["length"].toInt(8);
        s.isLittleEndian = o["isLittleEndian"].toBool(true);
        s.isSigned       = o["isSigned"].toBool(false);
        s.scale          = o["scale"].toDouble(1.0);
        s.offset         = o["offset"].toDouble(0.0);
        s.unit           = o["unit"].toString();
        m_signals.append(s);
    }
    endResetModel();
    return true;
}
