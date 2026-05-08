#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QHash>
#include "CanFrame.h"
#include "DbcParser.h"

/**
 * @brief Mostek CAN→MQTT – publikuje zdekodowane sygnały DBC do brokera MQTT.
 *
 * Używa mosquitto_pub (musi być zainstalowany).
 * Każdy sygnał DBC jest publikowany jako osobny temat:
 *   magistrala/<signal_name>
 * z payloadem: {"value": 123.4, "unit": "rpm", "timestamp": 1715123456789}
 */
class MqttBridge : public QObject {
    Q_OBJECT
public:
    explicit MqttBridge(QObject *parent = nullptr);

    void setDbcParser(const DbcParser *dbc) { m_dbc = dbc; }
    void setBroker(const QString &host, int port = 1883);
    void setEnabled(bool on);
    bool isEnabled() const { return m_enabled; }

public slots:
    void onNewFrame(const CanFrame &frame);

signals:
    void publishedSignal(const QString &signal, double value);
    void errorOccurred(const QString &msg);

private:
    void publish(const QString &topic, double value, const QString &unit, uint64_t ts);

    const DbcParser *m_dbc = nullptr;
    QString m_brokerHost = "localhost";
    int m_brokerPort = 1883;
    bool m_enabled = false;
    QTimer m_throttleTimer;
    QHash<QString, double> m_lastValues;
};
