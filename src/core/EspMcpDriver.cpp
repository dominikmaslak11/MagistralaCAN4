#include "EspMcpDriver.h"
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QSerialPortInfo>
#include <algorithm>
#include <cstring>

EspMcpDriver::EspMcpDriver()  = default;
EspMcpDriver::~EspMcpDriver() { close(); }

// ═══════════════════════════════════════════════════════════════
// Core API
// ═══════════════════════════════════════════════════════════════

bool EspMcpDriver::open(const QString &device) {
    if (m_port && m_port->isOpen()) close();

    QString portName = device.section(' ', 0, 0).trimmed();
    m_port = new QSerialPort(portName);
    m_port->setBaudRate(115200);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port->open(QIODevice::ReadWrite)) {
        qWarning() << "EspMcpDriver: cannot open" << portName << ":" << m_port->errorString();
        delete m_port;
        m_port = nullptr;
        return false;
    }

    m_port->clear();
    qDebug() << "EspMcpDriver: opened" << portName;
    return true;
}

void EspMcpDriver::close() {
    if (!m_port) return;
    if (m_port->isOpen()) {
        m_port->close();
        qDebug() << "EspMcpDriver: closed";
    }
    delete m_port;
    m_port = nullptr;
    m_rxBuffer.clear();
}

bool EspMcpDriver::isValid() const {
    return m_port && m_port->isOpen();
}

// ═══════════════════════════════════════════════════════════════
// Odczyt ramki – nieblokujący
// ═══════════════════════════════════════════════════════════════

CanFrame EspMcpDriver::readFrame() {
    if (!m_port || !m_port->isOpen()) return {};

    if (m_port->waitForReadyRead(1))
        m_rxBuffer.append(m_port->readAll());

    int cr = static_cast<int>(m_rxBuffer.indexOf('\r'));
    int lf = static_cast<int>(m_rxBuffer.indexOf('\n'));
    int end = -1;
    if (cr >= 0 && lf >= 0) end = std::min(cr, lf);
    else if (cr >= 0) end = cr;
    else if (lf >= 0) end = lf;
    else return {};

    QByteArray line = m_rxBuffer.left(end).trimmed();
    m_rxBuffer.remove(0, end + 1);

    if (line.isEmpty()) return {};
    return parseLine(line);
}

// ═══════════════════════════════════════════════════════════════
// Wysyłanie ramki
// ═══════════════════════════════════════════════════════════════

void EspMcpDriver::writeFrame(const CanFrame &frame) {
    if (!m_port || !m_port->isOpen()) return;

    QString cmd = QString("TX %1 %2 %3")
        .arg(frame.extended ? "EXT" : "STD")
        .arg(frame.id, frame.extended ? 8 : 3, 16, QChar('0'))
        .arg(frame.dlc);

    for (int i = 0; i < frame.dlc && i < 8; ++i)
        cmd += QString(" %1").arg(frame.data[i], 2, 16, QChar('0'));

    m_port->write((cmd + "\n").toLatin1());
}

// ═══════════════════════════════════════════════════════════════
// Lista urządzeń
// ═══════════════════════════════════════════════════════════════

QStringList EspMcpDriver::availableDevices() const {
    return detectDevices();
}

// ═══════════════════════════════════════════════════════════════
// Auto-detekcja
// ═══════════════════════════════════════════════════════════════

QStringList EspMcpDriver::detectDevices(int timeoutMs) {
    QStringList found;

    for (const auto &info : QSerialPortInfo::availablePorts()) {
        QString desc = info.description().toLower();
        QString mfr  = info.manufacturer().toLower();

        if (desc.contains("mouse") || desc.contains("keyboard") ||
            mfr.contains("microsoft") || mfr.contains("logitech"))
            continue;

        // Common ESP32 USB-serial chips
        bool likelyEsp = desc.contains("ch340") || desc.contains("cp210") ||
                         desc.contains("ftdi")   || desc.contains("usb serial") ||
                         mfr.contains("silicon") || mfr.contains("wch") ||
                         mfr.contains("ftdi");

        QSerialPort probe(info.portName());
        probe.setBaudRate(115200);
        probe.setDataBits(QSerialPort::Data8);
        probe.setStopBits(QSerialPort::OneStop);
        probe.setParity(QSerialPort::NoParity);
        probe.setFlowControl(QSerialPort::NoFlowControl);

        if (!probe.open(QIODevice::ReadWrite)) continue;
        probe.clear();

        probe.write("STATUS\n");
        if (!probe.waitForBytesWritten(100)) { probe.close(); continue; }

        QByteArray response;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            int remaining = timeoutMs - static_cast<int>(timer.elapsed());
            if (remaining <= 0) break;
            if (probe.waitForReadyRead(remaining)) {
                response.append(probe.readAll());
                if (response.contains('\n') || response.contains('\r')) break;
            } else break;
        }
        probe.close();

        QString resp = QString::fromLatin1(response).trimmed();
        bool confirmed = resp.contains("relays") || resp.contains("ESP MCP") ||
                         resp.contains("STATUS");

        if (confirmed) {
            found.prepend(QString("%1 [ESP-MCP2515]").arg(info.portName()));
        } else if (likelyEsp) {
            found.append(QString("%1 [ESP-MCP2515?]").arg(info.portName()));
        }
    }

    qDebug() << "EspMcpDriver::detectDevices: found" << found.size() << "device(s)";
    return found;
}

// ═══════════════════════════════════════════════════════════════
// Parsowanie linii ASCII
// ═══════════════════════════════════════════════════════════════

CanFrame EspMcpDriver::parseLine(const QByteArray &line) const {
    // Format: RX EXT|STD <ID_HEX> <DLC> [B0 B1 … BN]
    const QList<QByteArray> tokens = line.split(' ');
    if (tokens.size() < 4 || tokens[0] != "RX") return {};

    bool extended = (tokens[1] == "EXT");
    bool ok = false;
    uint32_t id = tokens[2].toUInt(&ok, 16);
    if (!ok) return {};
    int dlc = tokens[3].toInt(&ok);
    if (!ok || dlc < 0 || dlc > 8) return {};

    CanFrame f{};
    f.id        = id;
    f.extended  = extended;
    f.dlc       = static_cast<uint8_t>(dlc);
    f.timestamp = QDateTime::currentMSecsSinceEpoch() * 1000;

    for (int i = 0; i < dlc && (4 + i) < tokens.size(); ++i)
        f.data[i] = tokens[4 + i].toUInt(nullptr, 16);

    return f;
}
