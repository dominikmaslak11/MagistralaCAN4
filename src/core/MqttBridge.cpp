#include "MqttBridge.h"
#include "Logger.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <cmath>

MqttBridge::MqttBridge(QObject *parent) : QObject(parent) {
    m_throttleTimer.setInterval(200); // max 5 publikacji/s per sygnał
    m_throttleTimer.setSingleShot(true);
}

void MqttBridge::setBroker(const QString &host, int port) {
    m_brokerHost = host; m_brokerPort = port;
}

void MqttBridge::setEnabled(bool on) {
    m_enabled = on;
    if (!on) m_lastValues.clear();
    Logger::log(QString("MQTT: %1").arg(on ? "włączony" : "wyłączony"));
}

void MqttBridge::onNewFrame(const CanFrame &frame) {
    if (!m_enabled || !m_dbc) return;

    const auto decoded = m_dbc->decodeSignals(frame.id, frame.data.data(), frame.dlc);
    if (decoded.isEmpty()) return;

    DbcMessage msg = m_dbc->messageForId(frame.id);
    QHash<QString, QString> sigUnits;
    for (const auto &sig : msg.sigList)
        sigUnits[sig.name] = sig.unit;

    for (auto it = decoded.cbegin(); it != decoded.cend(); ++it) {
        const QString &name = it.key();
        double value = it.value();

        if (m_lastValues.contains(name) && fabs(m_lastValues[name] - value) < 0.001)
            continue;
        m_lastValues[name] = value;
        publish(name, value, sigUnits.value(name), frame.timestamp);
    }
}

void MqttBridge::publish(const QString &topic, double value, const QString &unit, uint64_t ts) {
    QJsonObject obj;
    obj["value"] = value;
    obj["unit"] = unit;
    obj["timestamp"] = (qint64)ts;
    QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    auto *proc = new QProcess(this);
    QStringList args;
    args << "-h" << m_brokerHost << "-p" << QString::number(m_brokerPort)
         << "-t" << ("magistrala/" + topic) << "-m" << QString::fromUtf8(payload);

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, topic, value, proc](int code, QProcess::ExitStatus) {
        if (code == 0) emit publishedSignal(topic, value);
        else emit errorOccurred(proc->readAllStandardError());
        proc->deleteLater();
    });

    proc->start("mosquitto_pub", args);
}
