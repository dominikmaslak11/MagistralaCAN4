#include "GvretDriver.h"
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QSerialPortInfo>

// GVRET protocol constants
static constexpr uint8_t GVRET_MAGIC   = 0xF1;
static constexpr uint8_t CMD_FRAME_OUT = 0x00;
static constexpr uint8_t CMD_FRAME_IN  = 0x01;
static constexpr uint8_t CMD_SETUP_BUS = 0x06;
static constexpr uint8_t CMD_GET_INFO  = 0x08;

GvretDriver::GvretDriver()  = default;
GvretDriver::~GvretDriver() { close(); }

// ═══════════════════════════════════════════════════════════════
// Static pure protocol helpers — no hardware, fully testable
// ═══════════════════════════════════════════════════════════════

uint32_t GvretDriver::baudStringToBps(const QString &baudStr) {
    static const QHash<QString, uint32_t> map = {
        {"1M",   1000000}, {"800K", 800000}, {"500K", 500000},
        {"250K",  250000}, {"125K", 125000}, {"100K", 100000},
        {"50K",    50000}, {"20K",   20000}, {"10K",   10000},
    };
    return map.value(baudStr, 0);
}

QByteArray GvretDriver::buildSetupBusPacket(uint32_t canBps) {
    QByteArray buf(8, '\0');
    buf[0] = static_cast<char>(GVRET_MAGIC);
    buf[1] = static_cast<char>(CMD_SETUP_BUS);
    buf[2] = static_cast<char>((canBps)       & 0xFF);
    buf[3] = static_cast<char>((canBps >>  8) & 0xFF);
    buf[4] = static_cast<char>((canBps >> 16) & 0xFF);
    buf[5] = static_cast<char>((canBps >> 24) & 0xFF);
    buf[6] = 0; // busNum = 0
    buf[7] = 0; // flags  = 0
    return buf;
}

QByteArray GvretDriver::buildGvretTxPacket(const CanFrame &frame) {
    uint32_t id  = frame.id & (frame.extended ? 0x1FFFFFFFUL : 0x7FFUL);
    if (frame.extended) id |= 0x80000000UL;

    uint8_t dlc     = frame.dlc <= 8 ? frame.dlc : 8;
    uint8_t len_bus = dlc; // bus=0 in upper nibble

    QByteArray buf;
    buf.reserve(7 + dlc);
    buf.append(static_cast<char>(GVRET_MAGIC));
    buf.append(static_cast<char>(CMD_FRAME_IN));
    buf.append(static_cast<char>((id)       & 0xFF));
    buf.append(static_cast<char>((id >>  8) & 0xFF));
    buf.append(static_cast<char>((id >> 16) & 0xFF));
    buf.append(static_cast<char>((id >> 24) & 0xFF));
    buf.append(static_cast<char>(len_bus));
    for (int i = 0; i < dlc; ++i)
        buf.append(static_cast<char>(frame.data[i]));
    return buf;
}

CanFrame GvretDriver::parseGvretFrame(QByteArray &rxBuf) {
    while (rxBuf.size() >= 2) {
        // Scan for F1 00 (CMD_FRAME_OUT) header
        int pos = -1;
        for (int i = 0; i + 1 < rxBuf.size(); ++i) {
            if (static_cast<uint8_t>(rxBuf[i])     == GVRET_MAGIC &&
                static_cast<uint8_t>(rxBuf[i + 1]) == CMD_FRAME_OUT) {
                pos = i;
                break;
            }
        }

        if (pos < 0) {
            // Keep last byte — might be split F1 | 00 across reads
            if (!rxBuf.isEmpty()) rxBuf = rxBuf.right(1);
            return {};
        }

        if (pos > 0) rxBuf.remove(0, pos);

        // F1 00 [ts:4LE] [id:4LE] [len_bus:1] [data:dlc]  → min 11 bytes
        if (rxBuf.size() < 11) return {};

        uint8_t len_bus = static_cast<uint8_t>(rxBuf[10]);
        int dlc = len_bus & 0x0F;
        if (dlc > 8) dlc = 8;

        int pktLen = 11 + dlc;
        if (rxBuf.size() < pktLen) return {};

        uint32_t id = static_cast<uint8_t>(rxBuf[6])
                    | (static_cast<uint32_t>(static_cast<uint8_t>(rxBuf[7]))  <<  8)
                    | (static_cast<uint32_t>(static_cast<uint8_t>(rxBuf[8]))  << 16)
                    | (static_cast<uint32_t>(static_cast<uint8_t>(rxBuf[9]))  << 24);

        bool ext = (id & 0x80000000UL) != 0;
        id &= 0x1FFFFFFFUL;

        CanFrame f{};
        f.id       = id;
        f.extended = ext;
        f.dlc      = static_cast<uint8_t>(dlc);
        f.timestamp = QDateTime::currentMSecsSinceEpoch() * 1000;

        for (int i = 0; i < dlc; ++i)
            f.data[i] = static_cast<uint8_t>(rxBuf[11 + i]);

        rxBuf.remove(0, pktLen);

        // Skip zero-ID/zero-DLC frames that CanSniffer treats as "no data"
        if (f.id == 0 && f.dlc == 0) continue;
        return f;
    }
    return {};
}

// ═══════════════════════════════════════════════════════════════
// setBaudRate
// ═══════════════════════════════════════════════════════════════

void GvretDriver::setBaudRate(const QString &baudStr) {
    uint32_t bps = baudStringToBps(baudStr);
    if (bps > 0) {
        m_canBps = bps;
        qDebug() << "GvretDriver: CAN speed queued ->" << baudStr << "(" << m_canBps << "bps)";
        if (m_port && m_port->isOpen())
            sendSetupBus();
    }
}

// ═══════════════════════════════════════════════════════════════
// sendSetupBus
// ═══════════════════════════════════════════════════════════════

void GvretDriver::sendSetupBus() {
    if (!m_port || !m_port->isOpen()) return;
    QByteArray pkt = buildSetupBusPacket(m_canBps);
    m_port->write(pkt);
    m_port->waitForBytesWritten(100);
    qDebug() << "GvretDriver: CMD_SETUP_BUS sent ->" << m_canBps << "bps";
}

// ═══════════════════════════════════════════════════════════════
// Core API
// ═══════════════════════════════════════════════════════════════

bool GvretDriver::open(const QString &device) {
    if (m_port && m_port->isOpen()) close();

    QString portName = device.section(' ', 0, 0).trimmed();
    m_port = new QSerialPort(portName);
    m_port->setBaudRate(115200);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port->open(QIODevice::ReadWrite)) {
        qWarning() << "GvretDriver: cannot open" << portName << ":" << m_port->errorString();
        delete m_port;
        m_port = nullptr;
        return false;
    }

    m_port->clear();
    m_rxBuffer.clear();

    const uint8_t probe[2] = {GVRET_MAGIC, CMD_GET_INFO};
    m_port->write(reinterpret_cast<const char *>(probe), 2);
    m_port->waitForBytesWritten(100);

    QByteArray resp;
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 500 && resp.size() < 32) {
        if (m_port->waitForReadyRead(50))
            resp.append(m_port->readAll());
        for (int i = 0; i + 1 < resp.size(); ++i) {
            if (static_cast<uint8_t>(resp[i])     == GVRET_MAGIC &&
                static_cast<uint8_t>(resp[i + 1]) == CMD_GET_INFO) {
                qDebug() << "GvretDriver: GET_INFO confirmed on" << portName;
                goto info_ok;
            }
        }
    }
    qDebug() << "GvretDriver: opened" << portName << "(GET_INFO timeout — continuing)";

info_ok:
    m_port->clear();
    m_rxBuffer.clear();
    sendSetupBus();
    qDebug() << "GvretDriver: ready on" << portName << "@" << m_canBps << "bps";
    return true;
}

void GvretDriver::close() {
    if (!m_port) return;
    if (m_port->isOpen()) {
        m_port->close();
        qDebug() << "GvretDriver: closed";
    }
    delete m_port;
    m_port = nullptr;
    m_rxBuffer.clear();
}

bool GvretDriver::isValid() const {
    return m_port && m_port->isOpen();
}

CanFrame GvretDriver::readFrame() {
    if (!m_port || !m_port->isOpen()) return {};

    CanFrame cached = parseGvretFrame(m_rxBuffer);
    if (cached.dlc || cached.id || cached.error) return cached;

    if (!m_port->waitForReadyRead(5)) return {};
    m_rxBuffer.append(m_port->readAll());
    return parseGvretFrame(m_rxBuffer);
}

void GvretDriver::writeFrame(const CanFrame &frame) {
    if (!m_port || !m_port->isOpen()) return;
    m_port->write(buildGvretTxPacket(frame));
}

QStringList GvretDriver::availableDevices() const {
    return detectDevices();
}

// ═══════════════════════════════════════════════════════════════
// Auto-detekcja
// ═══════════════════════════════════════════════════════════════

QStringList GvretDriver::detectDevices(int timeoutMs) {
    QStringList found;

    for (const auto &info : QSerialPortInfo::availablePorts()) {
        QString desc = info.description().toLower();
        QString mfr  = info.manufacturer().toLower();

        if (desc.contains("mouse") || desc.contains("keyboard") ||
            mfr.contains("microsoft") || mfr.contains("logitech"))
            continue;

        QSerialPort probe(info.portName());
        probe.setBaudRate(115200);
        probe.setDataBits(QSerialPort::Data8);
        probe.setStopBits(QSerialPort::OneStop);
        probe.setParity(QSerialPort::NoParity);
        probe.setFlowControl(QSerialPort::NoFlowControl);

        if (!probe.open(QIODevice::ReadWrite)) continue;
        probe.clear();

        const uint8_t cmd[2] = {GVRET_MAGIC, CMD_GET_INFO};
        probe.write(reinterpret_cast<const char *>(cmd), 2);
        if (!probe.waitForBytesWritten(100)) { probe.close(); continue; }

        QByteArray resp;
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            int rem = timeoutMs - static_cast<int>(timer.elapsed());
            if (rem <= 0) break;
            if (probe.waitForReadyRead(rem)) {
                resp.append(probe.readAll());
                if (resp.size() >= 4) break;
            } else break;
        }
        probe.close();

        bool isGvret = false;
        for (int i = 0; i + 1 < resp.size(); ++i) {
            if (static_cast<uint8_t>(resp[i])     == GVRET_MAGIC &&
                static_cast<uint8_t>(resp[i + 1]) == CMD_GET_INFO) {
                isGvret = true;
                break;
            }
        }

        if (isGvret) {
            found.append(QString("%1 [GVRET-ESP32]").arg(info.portName()));
            qDebug() << "GvretDriver: found GVRET device on" << info.portName();
        }
    }

    qDebug() << "GvretDriver::detectDevices: found" << found.size() << "device(s)";
    return found;
}
