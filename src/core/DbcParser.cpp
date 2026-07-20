#include "DbcParser.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>
#include <algorithm>

bool DbcParser::load(const QString &fileName) {
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&file);
    QString line;
    DbcMessage currentMsg;
    bool inMessage = false;

    while (in.readLineInto(&line)) {
        line = line.trimmed();

        if (line.startsWith("BO_ ")) {
            // BO_ <id> <name>: <dlc> <sender>
            if (inMessage) m_messages.append(currentMsg);
            currentMsg = DbcMessage();
            inMessage = true;

            QRegularExpression re(R"(BO_ (0[xX][0-9a-fA-F]+|\d+)\s+(\w+)\s*:\s*(\d+)\s+\w+)");
            auto match = re.match(line);
            if (match.hasMatch()) {
                currentMsg.id = match.captured(1).toUInt(nullptr, 0);  // handles 0x... and decimal
                currentMsg.name = match.captured(2);
                currentMsg.dlc = match.captured(3).toInt();
            }
        }
        else if (inMessage && line.startsWith("SG_ ")) {
            // SG_ <name> : <start>|<length>@<endian><sign> (<scale>,<offset>) [<min>|<max>] "<unit>" <receivers>
            QRegularExpression re(R"(SG_\s+(\w+)\s*:\s*(\d+)\|(\d+)@([01])([+-])\s*\(([^,]+),([^)]+)\)\s*\[([^|]+)\|([^]]+)\]\s*\"([^\"]*)\")");
            auto match = re.match(line);
            if (match.hasMatch()) {
                DbcSignal sig;
                sig.name = match.captured(1);
                sig.startBit = match.captured(2).toInt();
                sig.length = match.captured(3).toInt();
                sig.isLittleEndian = match.captured(4) == "1";
                sig.isSigned = match.captured(5) == "-";
                sig.scale = match.captured(6).toDouble();
                sig.offset = match.captured(7).toDouble();
                sig.minimum = match.captured(8).toDouble();
                sig.maximum = match.captured(9).toDouble();
                sig.unit = match.captured(10);
                currentMsg.sigList.append(sig);
            }
        }
    }
    if (inMessage) m_messages.append(currentMsg);
    file.close();

    for (const auto &msg : m_messages)
        m_messageMap[msg.id] = msg;

    return true;
}

QVector<DbcMessage> DbcParser::messages() const {
    return m_messages;
}

DbcMessage DbcParser::messageForId(uint32_t id) const {
    return m_messageMap.value(id);
}

QString DbcParser::signalDescriptions(uint32_t id, const uint8_t* data, int dlc) const {
    auto decoded = decodeSignals(id, data, dlc);
    if (decoded.isEmpty()) return "";

    DbcMessage msg = messageForId(id);
    QStringList desc;
    for (const auto &sig : msg.sigList) {
        auto it = decoded.find(sig.name);
        if (it != decoded.end())
            desc.append(QString("%1 = %2 %3").arg(sig.name).arg(it.value(), 0, 'f', 2).arg(sig.unit));
    }
    return desc.join("; ");
}

QHash<QString, double> DbcParser::decodeSignals(uint32_t id, const uint8_t* data, int dlc) const {
    QHash<QString, double> result;
    DbcMessage msg = messageForId(id);
    if (msg.id == 0) return result;

    for (const auto &sig : msg.sigList) {
        // Wyciągnij surową wartość z bitów
        uint64_t raw = 0;
        int bitsExtracted = 0;

        if (sig.isLittleEndian) {
            // Intel (little-endian): bity idą od startBit w górę, LSB first
            int bitPos = sig.startBit;
            while (bitsExtracted < sig.length) {
                int byteIdx = bitPos / 8;
                int bitInByte = bitPos % 8;
                if (byteIdx >= dlc) break;
                int bitsAvailable = 8 - bitInByte;
                int bitsNow = std::min(bitsAvailable, sig.length - bitsExtracted);
                uint8_t mask = ((1U << bitsNow) - 1) << bitInByte;
                uint64_t chunk = (data[byteIdx] & mask) >> bitInByte;
                raw |= (chunk << bitsExtracted);
                bitsExtracted += bitsNow;
                bitPos += bitsNow;
            }
        } else {
            // Motorola (big-endian): bity idą od startBit w dół
            int bitPos = sig.startBit;
            while (bitsExtracted < sig.length) {
                int byteIdx = bitPos / 8;
                int bitInByte = bitPos % 8;
                if (byteIdx >= dlc) break;
                int bitsAvailable = bitInByte + 1;
                int bitsNow = std::min(bitsAvailable, sig.length - bitsExtracted);
                int startBitInByte = bitInByte - bitsNow + 1;
                uint8_t mask = ((1U << bitsNow) - 1) << startBitInByte;
                uint64_t chunk = (data[byteIdx] & mask) >> startBitInByte;
                raw |= (chunk << bitsExtracted);
                bitsExtracted += bitsNow;
                bitPos -= bitsNow;
            }
        }

        if (bitsExtracted == 0) continue;

        // Konwersja signed/unsigned
        double value;
        if (sig.isSigned && bitsExtracted < 64) {
            uint64_t signMask = 1ULL << (bitsExtracted - 1);
            if (raw & signMask) {
                // ujemna
                uint64_t signExtend = (~0ULL) << bitsExtracted;
                int64_t sraw = static_cast<int64_t>(raw | signExtend);
                value = static_cast<double>(sraw) * sig.scale + sig.offset;
            } else {
                value = static_cast<double>(static_cast<int64_t>(raw)) * sig.scale + sig.offset;
            }
        } else {
            value = static_cast<double>(raw) * sig.scale + sig.offset;
        }

        result[sig.name] = value;
    }
    return result;
}

// ── Nowe metody (edytor DBC) ────────────────────────────────

bool DbcParser::save(const QString &fileName) const {
    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out << serialize(m_messages);
    file.close();
    qDebug() << "Zapisano DBC:" << fileName;
    return true;
}

QString DbcParser::serialize(const QVector<DbcMessage> &msgs) {
    QString out;
    QTextStream ts(&out);

    // Sortuj wg ID
    auto sorted = msgs;
    std::sort(sorted.begin(), sorted.end(),
              [](const DbcMessage &a, const DbcMessage &b) { return a.id < b.id; });

    for (const auto &msg : sorted) {
        ts << "BO_ " << msg.id << " " << msg.name << ": " << msg.dlc << " Vector__XXX\n";
        for (const auto &sig : msg.sigList) {
            ts << " SG_ " << sig.name << " : "
               << sig.startBit << "|" << sig.length << "@"
               << (sig.isLittleEndian ? "1" : "0")
               << (sig.isSigned ? "-" : "+")
               << " (" << sig.scale << "," << sig.offset << ")"
               << " [" << sig.minimum << "|" << sig.maximum << "]"
               << " \"" << sig.unit << "\""
               << " Vector__XXX\n";
        }
        ts << "\n";
    }
    return out;
}

void DbcParser::setMessages(const QVector<DbcMessage> &msgs) {
    m_messages = msgs;
    rebuildMap();
}

void DbcParser::rebuildMap() {
    m_messageMap.clear();
    for (const auto &msg : m_messages)
        m_messageMap[msg.id] = msg;
}
