#include "DbcParser.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>

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

            QRegularExpression re(R"(BO_ (\d+)\s+(\w+)\s*:\s*(\d+)\s+\w+)");
            auto match = re.match(line);
            if (match.hasMatch()) {
                currentMsg.id = match.captured(1).toUInt();
                currentMsg.name = match.captured(2);
                currentMsg.dlc = match.captured(3).toInt();
            }
        }
        else if (inMessage && line.startsWith(" SG_ ")) {
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
    DbcMessage msg = messageForId(id);
    if (msg.id == 0) return "";

    QStringList desc;
    for (const auto &sig : msg.sigList) {
        // Ekstrakcja wartości sygnału (uproszczone – tylko dla sygnałów mieszczących się w bajcie)
        int byteIdx = sig.startBit / 8;
        if (byteIdx >= dlc) continue;
        uint8_t rawValue = data[byteIdx];
        double value = rawValue * sig.scale + sig.offset;
        desc.append(QString("%1 = %2 %3").arg(sig.name).arg(value, 0, 'f', 2).arg(sig.unit));
    }
    return desc.join("; ");
}
