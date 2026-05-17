#include "SlCanDriver.h"
#include <QDebug>
#include <QDateTime>
#include <QSerialPortInfo>
#include <QThread>
#include <QElapsedTimer>
#include <cstring>

// ═══════════════════════════════════════════════════════════════
// Konstrukcja / destrukcja
// ═══════════════════════════════════════════════════════════════

SlCanDriver::SlCanDriver()  = default;
SlCanDriver::~SlCanDriver() { close(); }

// ═══════════════════════════════════════════════════════════════
// Core API
// ═══════════════════════════════════════════════════════════════

bool SlCanDriver::open(const QString &device) {
    if (m_port && m_port->isOpen()) {
        qDebug() << "SlCanDriver: already open, closing first";
        close();
    }

    // Parsuj nazwę – obsługuje "COM3", "COM3 (opis)", "COM3 [opis @ 921600bps]", "/dev/ttyACM0"
    QString portName = device.section(' ', 0, 0).trimmed();

    // Backward-compat: wyciągnij UART baud z etykiety (np. "COM3 [v1.0 @ 500000bps]")
    qint32 uartBaud = m_uartBaudRate;
    int atPos = device.indexOf('@');
    if (atPos >= 0) {
        int bpsPos = device.indexOf("bps", atPos);
        if (bpsPos > atPos) {
            QString baudStr = device.mid(atPos + 1, bpsPos - atPos - 1).trimmed();
            qint32 parsed = baudStr.toInt();
            if (parsed > 0) uartBaud = parsed;
        }
    }

    m_port = new QSerialPort(portName);
    m_port->setBaudRate(uartBaud);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port->open(QIODevice::ReadWrite)) {
        qWarning() << "SlCanDriver: cannot open" << portName << ":" << m_port->errorString();
        delete m_port;
        m_port = nullptr;
        return false;
    }

    // Wyczyść bufory i zamknij ewentualną poprzednią sesję
    m_port->clear();
    sendCommand("C", 50);   // Close any existing session
    m_port->clear();

    // Ustaw prędkość magistrali CAN (komenda Sx — przed otwarciem kanału)
    sendCommand(QString("S%1").arg(m_canSpeedCode), 50);

    // Otwórz kanał CAN — odpowiedź OK to samo '\r' (trimuje do ""), błąd to '\a' (BEL)
    QString ver = sendCommand("O", 100);
    if (ver.contains('\a') || ver.contains("ERROR", Qt::CaseInsensitive)) {
        qWarning() << "SlCanDriver: O command failed on" << portName;
        m_port->close();
        delete m_port;
        m_port = nullptr;
        return false;
    }

    qDebug() << "SlCanDriver: opened" << portName << "UART baud" << uartBaud
             << "CAN S" << m_canSpeedCode;
    return true;
}

void SlCanDriver::close() {
    if (!m_port) return;
    if (m_port->isOpen()) {
        sendCommand("C", 50);   // Close CAN channel
        m_port->close();
        qDebug() << "SlCanDriver: closed";
    }
    delete m_port;
    m_port = nullptr;
    m_rxBuffer.clear();
}

bool SlCanDriver::isValid() const {
    return m_port && m_port->isOpen();
}

// ═══════════════════════════════════════════════════════════════
// Odczyt ramki – nieblokujący
// ═══════════════════════════════════════════════════════════════

CanFrame SlCanDriver::readFrame() {
    if (!m_port || !m_port->isOpen()) return {};

    // waitForReadyRead uses WaitForSingleObject on Windows — thread-safe, event-loop-free.
    // 1 ms timeout keeps doWork() responsive without busy-spinning.
    if (m_port->waitForReadyRead(1))
        m_rxBuffer.append(m_port->readAll());

    // Szukaj kompletnej linii (zakończonej \r lub \n)
    int cr = m_rxBuffer.indexOf('\r');
    int lf = m_rxBuffer.indexOf('\n');
    int end = -1;
    if (cr >= 0 && lf >= 0) end = std::min(cr, lf);
    else if (cr >= 0) end = cr;
    else if (lf >= 0) end = lf;
    else return {};  // brak kompletnej linii

    QByteArray line = m_rxBuffer.left(end).trimmed();
    m_rxBuffer.remove(0, end + 1);

    if (line.isEmpty()) return {};
    return parseIncoming(line);
}

// ═══════════════════════════════════════════════════════════════
// Wysyłanie ramki
// ═══════════════════════════════════════════════════════════════

void SlCanDriver::writeFrame(const CanFrame &frame) {
    if (!m_port || !m_port->isOpen()) return;

    QByteArray cmd;
    if (frame.extended) {
        // T + 8-cyfrowy ID (hex) + 1-cyfrowy DLC (decimal) + dane
        cmd = QString("T%1%2")
                  .arg(frame.id & 0x1FFFFFFF, 8, 16, QChar('0'))
                  .arg(frame.dlc)
                  .toLatin1();
    } else {
        // t + 3-cyfrowy ID + 1-cyfrowy DLC + dane
        cmd = QString("t%1%2")
                  .arg(frame.id & 0x7FF, 3, 16, QChar('0'))
                  .arg(frame.dlc, 1, 16, QChar('0'))
                  .toLatin1();
    }

    // Dolej dane
    for (int i = 0; i < frame.dlc && i < 8; ++i)
        cmd.append(QByteArray::number(frame.data[i], 16).rightJustified(2, '0').toUpper());

    cmd.append('\r');
    m_port->write(cmd);
}

// ═══════════════════════════════════════════════════════════════
// Baud rate (interfejs ICanDriver)
// ═══════════════════════════════════════════════════════════════

void SlCanDriver::setBaudRate(const QString &baudStr) {
    // Maps CAN bus speed string to SLCAN Sx command code (S0-S8).
    // This does NOT affect the UART baud rate (set separately via setUartBaudRate()).
    static const QHash<QString, int> slcanMap = {
        {"10K", 0}, {"20K", 1}, {"50K", 2}, {"100K", 3},
        {"125K", 4}, {"250K", 5}, {"500K", 6}, {"800K", 7}, {"1M", 8}
    };
    if (slcanMap.contains(baudStr)) {
        m_canSpeedCode = slcanMap[baudStr];
        qDebug() << "SlCanDriver: CAN speed set to" << baudStr
                 << "(S" << m_canSpeedCode << "), UART baud unchanged at" << m_uartBaudRate;
    }
}

// ═══════════════════════════════════════════════════════════════
// Lista urządzeń – pusta (używamy autodetekcji)
// ═══════════════════════════════════════════════════════════════

QStringList SlCanDriver::availableDevices() const {
    return detectDevices();
}

// ═══════════════════════════════════════════════════════════════
// Autodetekcja urządzeń SLCAN
// ═══════════════════════════════════════════════════════════════

QStringList SlCanDriver::detectDevices(int timeoutMs) {
    QStringList found;

    const auto ports = QSerialPortInfo::availablePorts();
    if (ports.isEmpty()) {
        qDebug() << "SlCanDriver::detectDevices: no serial ports found";
        return {};
    }

    qDebug() << "SlCanDriver::detectDevices: probing" << ports.size() << "serial ports...";

    for (const auto &info : ports) {
        // Pomijamy oczywiste nie-CAN porty
        QString desc = info.description().toLower();
        QString mfr  = info.manufacturer().toLower();
        if (desc.contains("mouse") || desc.contains("keyboard") ||
            mfr.contains("microsoft") || mfr.contains("logitech"))
            continue;

        // Próbuj kolejno popularnych prędkości transmisji
        // candleLight: 921600 | Lawicel CAN232: 115200 | USBtin: 500000 | Canable: 1M
        static constexpr qint32 baudRates[] = {921600, 1000000, 500000, 115200};
        bool foundOnPort = false;

        for (qint32 baud : baudRates) {
            QSerialPort probe(info.portName());
            probe.setBaudRate(baud);
            probe.setDataBits(QSerialPort::Data8);
            probe.setStopBits(QSerialPort::OneStop);
            probe.setParity(QSerialPort::NoParity);
            probe.setFlowControl(QSerialPort::NoFlowControl);

            if (!probe.open(QIODevice::ReadWrite)) continue;
            probe.clear();

            // Wyślij 'V\r' (version) i czekaj na odpowiedź
            probe.write("V\r");
            if (!probe.waitForBytesWritten(100)) { probe.close(); continue; }

            QByteArray response;
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < timeoutMs) {
                if (probe.waitForReadyRead(timeoutMs - timer.elapsed())) {
                    response.append(probe.readAll());
                    if (response.contains('\r') || response.contains('\n')) break;
                } else break;
            }

            // Wyślij C\r i zamknij port
            probe.write("C\r");
            probe.waitForBytesWritten(50);
            probe.close();

            if (response.isEmpty()) continue;

            QString resp = QString::fromLatin1(response).trimmed();
            bool isSlCan = false;
            QString label;

            if (resp.startsWith('V') && resp.length() >= 2) {
                isSlCan = true;
                label = resp.mid(1);
            } else if (resp.length() >= 4 && resp[0].isDigit()) {
                // numeryczna wersja bez prefiksu V – candleLight
                isSlCan = true;
                label = resp;
            } else if (resp.toLower().contains("can") || resp.toLower().contains("slcan")) {
                isSlCan = true;
                label = resp;
            }

            if (isSlCan) {
                QString entry = QString("%1 [%2 @ %3bps]")
                    .arg(info.portName())
                    .arg(label.isEmpty() ? "SLCAN" : label)
                    .arg(baud);
                found.append(entry);
                qDebug() << "SlCanDriver: found SLCAN device on" << info.portName()
                         << "@" << baud << "->" << label;
                foundOnPort = true;
                break;  // znaleziono — nie próbuj innych prędkości
            }
        }
    }

    qDebug() << "SlCanDriver::detectDevices: found" << found.size() << "device(s)";
    return found;
}

// ═══════════════════════════════════════════════════════════════
// Parsowanie ramki przychodzącej
// ═══════════════════════════════════════════════════════════════

CanFrame SlCanDriver::parseIncoming(const QByteArray &line) const {
    CanFrame f;
    if (line.isEmpty()) return f;

    char prefix = line.at(0);
    bool isExtended = false;
    int idLen = 3;      // standard ID: 3 hex digits
    int dlcLen = 1;     // standard DLC: 1 hex digit

    // Określ typ ramki
    if (prefix == 'T' || prefix == 'R') {
        isExtended = true;
        idLen = 8;      // extended ID: 8 hex digits
        dlcLen = 1;     // DLC: always 1 decimal digit in SLCAN
    } else if (prefix == 't' || prefix == 'r') {
        isExtended = false;
        idLen = 3;
        dlcLen = 1;
    } else {
        // Nieznany prefix – może być wersja (V), status (F) itp.
        return {};
    }

    if (line.length() < 1 + idLen + dlcLen) return {};

    bool ok = false;
    uint32_t id = line.mid(1, idLen).toUInt(&ok, 16);
    if (!ok) return {};
    int dlc = line.mid(1 + idLen, dlcLen).toInt(&ok, 16);
    if (!ok) return {};
    if (dlc > 8) dlc = 8;   // SLCAN classic nie wspiera FD, ale na wszelki wypadek

    f.id       = id;
    f.extended = isExtended;
    f.dlc      = dlc;
    f.timestamp = QDateTime::currentMSecsSinceEpoch() * 1000;  // pseudo-timestamp w µs

    // Parsuj dane
    int dataStart = 1 + idLen + dlcLen;
    for (int i = 0; i < dlc && dataStart + i * 2 + 1 < line.length(); ++i) {
        f.data[i] = line.mid(dataStart + i * 2, 2).toUInt(&ok, 16);
        if (!ok) break;
    }

    return f;
}

// ═══════════════════════════════════════════════════════════════
// Komenda AT (wysyła + czeka na odpowiedź)
// ═══════════════════════════════════════════════════════════════

QString SlCanDriver::sendCommand(const QString &cmd, int waitMs) {
    if (!m_port || !m_port->isOpen()) return {};

    m_port->write(cmd.toLatin1());
    if (!cmd.endsWith('\r'))
        m_port->write("\r");

    if (!m_port->waitForBytesWritten(waitMs))
        return {};

    QByteArray resp;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < waitMs) {
        int remaining = waitMs - timer.elapsed();
        if (remaining <= 0) break;
        if (m_port->waitForReadyRead(remaining)) {
            resp.append(m_port->readAll());
            if (resp.contains('\r') || resp.contains('\n')) break;
        } else break;
    }

    return QString::fromLatin1(resp).trimmed();
}
