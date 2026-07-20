#include "MqttBridge.h"
#include "Logger.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <cmath>

// ── Konstrukcja / destrukcja ─────────────────────────────────────────────────

MqttBridge::MqttBridge(QObject *parent) : QObject(parent) {
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected,    this, &MqttBridge::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &MqttBridge::onDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred,this, &MqttBridge::onError);
    connect(m_socket, &QTcpSocket::readyRead,    this, &MqttBridge::onReadyRead);

    m_drainTimer.setInterval(m_publishIntervalMs);
    connect(&m_drainTimer, &QTimer::timeout, this, &MqttBridge::drainQueue);

    m_reconnectTimer.setSingleShot(true);
    connect(&m_reconnectTimer, &QTimer::timeout, this, &MqttBridge::connectToBroker);
}

MqttBridge::~MqttBridge() {
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->write(buildDisconnect());
        m_socket->waitForBytesWritten(200);
        m_socket->disconnectFromHost();
    }
}

// ── Publiczny interfejs ──────────────────────────────────────────────────────

void MqttBridge::setBroker(const QString &host, int port) {
    m_brokerHost = host;
    m_brokerPort = port;
}

void MqttBridge::setEnabled(bool on) {
    m_enabled = on;
    if (on) {
        connectToBroker();
    } else {
        m_reconnectTimer.stop();
        disconnectFromBroker();
        m_lastValues.clear();
    }
    Logger::log(QString("MQTT: %1").arg(on ? "włączony" : "wyłączony"));
}

bool MqttBridge::isConnected() const {
    return m_socket->state() == QAbstractSocket::ConnectedState && m_connackReceived;
}

void MqttBridge::connectToBroker() {
    if (m_socket->state() != QAbstractSocket::UnconnectedState) return;
    m_connackReceived = false;
    m_socket->connectToHost(m_brokerHost, static_cast<quint16>(m_brokerPort));
}

void MqttBridge::disconnectFromBroker() {
    m_drainTimer.stop();
    m_queue.clear();
    m_connackReceived = false;
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->write(buildDisconnect());
        m_socket->flush();
    }
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort(); // also aborts Connecting state
}

// ── CAN frame processing ─────────────────────────────────────────────────────

void MqttBridge::onNewFrame(const CanFrame &frame) {
    if (!m_enabled || !m_dbc || !isConnected()) return;

    const auto decoded = m_dbc->decodeSignals(frame.id, frame.data.data(), frame.dlc);
    if (decoded.isEmpty()) return;

    DbcMessage msg = m_dbc->messageForId(frame.id);
    QHash<QString, QString> sigUnits;
    for (const auto &sig : msg.sigList)
        sigUnits[sig.name] = sig.unit;

    for (auto it = decoded.cbegin(); it != decoded.cend(); ++it) {
        const QString &name = it.key();
        double value = it.value();
        if (m_lastValues.contains(name) &&
            fabs(m_lastValues[name] - value) < m_changeThreshold)
            continue;
        m_lastValues[name] = value;
        doPublish(name, value, sigUnits.value(name), frame.timestamp);
    }
}

void MqttBridge::doPublish(const QString &topic, double value,
                            const QString &unit, uint64_t ts) {
    QJsonObject obj;
    obj["value"]     = value;
    obj["unit"]      = unit;
    obj["timestamp"] = static_cast<qint64>(ts);
    QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    m_queue.enqueue({"magistrala/" + topic, payload});
    if (!m_drainTimer.isActive())
        m_drainTimer.start();
}

void MqttBridge::drainQueue() {
    if (m_queue.isEmpty()) { m_drainTimer.stop(); return; }
    if (!isConnected())    { m_drainTimer.stop(); return; }

    auto item = m_queue.dequeue();
    m_socket->write(buildPublish(item.topic, item.payload));

    // Emit signal for UI update (approximate, based on topic name)
    emit publishedSignal(item.topic.section('/', -1), 0.0);
}

void MqttBridge::flushQueue() {
    while (!m_queue.isEmpty() && isConnected()) {
        auto item = m_queue.dequeue();
        m_socket->write(buildPublish(item.topic, item.payload));
    }
}

// ── QTcpSocket callbacks ─────────────────────────────────────────────────────

void MqttBridge::onConnected() {
    m_reconnectDelayMs = 1000; // reset backoff
    m_socket->write(buildConnect());
    Logger::log(QString("MQTT: TCP połączono z %1:%2, wysyłam CONNECT")
                .arg(m_brokerHost).arg(m_brokerPort));
}

void MqttBridge::onDisconnected() {
    m_connackReceived = false;
    m_drainTimer.stop();
    emit connectionStateChanged(false);
    Logger::log("MQTT: rozłączono");
    if (m_enabled)
        scheduleReconnect();
}

void MqttBridge::onError(QAbstractSocket::SocketError err) {
    QString msg = QString("MQTT socket error %1: %2")
                  .arg(static_cast<int>(err)).arg(m_socket->errorString());
    Logger::log(msg);
    emit errorOccurred(msg);
    if (m_enabled && m_socket->state() == QAbstractSocket::UnconnectedState)
        scheduleReconnect();
}

void MqttBridge::onReadyRead() {
    QByteArray data = m_socket->readAll();
    if (data.isEmpty()) return;

    uint8_t type = static_cast<uint8_t>(data[0]) >> 4;
    if (type == 2) { // CONNACK
        if (data.size() >= 4) {
            uint8_t returnCode = static_cast<uint8_t>(data[3]);
            if (returnCode == 0) {
                m_connackReceived = true;
                m_drainTimer.start();
                emit connectionStateChanged(true);
                Logger::log("MQTT: CONNACK OK — połączono z brokerem");
            } else {
                QString reason;
                switch (returnCode) {
                case 1: reason = "Unacceptable protocol version"; break;
                case 2: reason = "Client ID rejected"; break;
                case 3: reason = "Server unavailable"; break;
                case 4: reason = "Bad credentials"; break;
                case 5: reason = "Not authorized"; break;
                default: reason = QString("Error %1").arg(returnCode); break;
                }
                emit errorOccurred("MQTT CONNACK: " + reason);
                Logger::log("MQTT: CONNACK odmowa — " + reason);
            }
        }
    }
    // PINGRESP (type=13) — ignore, keepalive not needed for fire-and-forget
}

void MqttBridge::scheduleReconnect() {
    Logger::log(QString("MQTT: ponawianie za %1ms").arg(m_reconnectDelayMs));
    m_reconnectTimer.start(m_reconnectDelayMs);
    m_reconnectDelayMs = std::min(m_reconnectDelayMs * 2, kMaxReconnectDelayMs);
}

// ── MQTT 3.1.1 packet builders ───────────────────────────────────────────────

QByteArray MqttBridge::mqttString(const QString &s) {
    QByteArray b = s.toUtf8();
    QByteArray result;
    result.append(static_cast<char>((b.size() >> 8) & 0xFF));
    result.append(static_cast<char>(b.size() & 0xFF));
    result.append(b);
    return result;
}

QByteArray MqttBridge::remainingLength(int len) {
    QByteArray result;
    do {
        uint8_t byte = len & 0x7F;
        len >>= 7;
        if (len > 0) byte |= 0x80;
        result.append(static_cast<char>(byte));
    } while (len > 0);
    return result;
}

QByteArray MqttBridge::buildConnect() const {
    // Variable header
    QByteArray varHdr;
    varHdr.append(mqttString("MQTT"));    // protocol name
    varHdr.append(static_cast<char>(4));  // protocol level 3.1.1
    varHdr.append(static_cast<char>(2));  // connect flags: Clean Session
    varHdr.append(static_cast<char>(0));  // keep-alive MSB
    varHdr.append(static_cast<char>(60)); // keep-alive LSB = 60s

    // Payload: client ID only (no will, no credentials)
    QByteArray payload = mqttString(m_clientId);

    QByteArray packet;
    packet.append(static_cast<char>(0x10)); // CONNECT
    packet.append(remainingLength(varHdr.size() + payload.size()));
    packet.append(varHdr);
    packet.append(payload);
    return packet;
}

QByteArray MqttBridge::buildPublish(const QString &topic,
                                    const QByteArray &payload) const {
    QByteArray topicBytes = mqttString(topic);
    // No packet ID needed for QoS 0

    QByteArray packet;
    packet.append(static_cast<char>(0x30)); // PUBLISH, QoS 0, no retain
    packet.append(remainingLength(topicBytes.size() + payload.size()));
    packet.append(topicBytes);
    packet.append(payload);
    return packet;
}

QByteArray MqttBridge::buildDisconnect() const {
    QByteArray packet;
    packet.append(static_cast<char>(0xE0)); // DISCONNECT
    packet.append(static_cast<char>(0x00)); // remaining length = 0
    return packet;
}
