#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QQueue>
#include <QHash>
#include "CanFrame.h"
#include "DbcParser.h"

/**
 * @brief Mostek CAN→MQTT — natywny klient MQTT 3.1.1 przez QTcpSocket.
 *
 * Nie wymaga mosquitto_pub. Łączy się bezpośrednio z brokerem TCP.
 * Każdy sygnał DBC jest publikowany jako osobny temat:
 *   magistrala/<signal_name>
 * Payload JSON: {"value": 123.4, "unit": "rpm", "timestamp": 1715123456789}
 *
 * Funkcje:
 *  - Auto-reconnect z backoffem (1s→2s→5s→30s)
 *  - Rate limiting: max 50 publish/s (kolejka)
 *  - QoS 0 (fire-and-forget, wystarczy dla CAN live data)
 *  - Filtrowanie: publikuj tylko gdy wartość zmieniła się o > threshold
 */
class MqttBridge : public QObject {
    Q_OBJECT
public:
    explicit MqttBridge(QObject *parent = nullptr);
    ~MqttBridge() override;

    void setDbcParser(const DbcParser *dbc) { m_dbc = dbc; }
    void setBroker(const QString &host, int port = 1883);
    void setClientId(const QString &id) { m_clientId = id; }
    void setEnabled(bool on);
    bool isEnabled()    const { return m_enabled; }
    bool isConnected()  const;

    void setChangeThreshold(double t) { m_changeThreshold = t; }
    void setMaxRate(int publishPerSec) { m_publishIntervalMs = (publishPerSec > 0) ? (1000 / publishPerSec) : 20; }

public slots:
    void onNewFrame(const CanFrame &frame);
    void connectToBroker();
    void disconnectFromBroker();

signals:
    void publishedSignal(const QString &signal, double value);
    void errorOccurred(const QString &msg);
    void connectionStateChanged(bool connected);

private slots:
    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError);
    void onReadyRead();
    void drainQueue();
    void scheduleReconnect();

private:
    // MQTT 3.1.1 packet builders
    QByteArray buildConnect()  const;
    QByteArray buildPublish(const QString &topic, const QByteArray &payload) const;
    QByteArray buildDisconnect() const;
    static QByteArray mqttString(const QString &s);
    static QByteArray remainingLength(int len);

    void doPublish(const QString &topic, double value, const QString &unit, uint64_t ts);
    void flushQueue();

    const DbcParser *m_dbc = nullptr;
    QString m_brokerHost   = "localhost";
    int     m_brokerPort   = 1883;
    QString m_clientId     = "MagistralaCAN4";
    bool    m_enabled      = false;

    QTcpSocket *m_socket   = nullptr;
    bool m_connackReceived = false;

    // Rate limiting
    struct PendingPublish { QString topic; QByteArray payload; };
    QQueue<PendingPublish> m_queue;
    QTimer m_drainTimer;
    int    m_publishIntervalMs = 20; // 50/s default

    // Change filtering
    QHash<QString, double> m_lastValues;
    double m_changeThreshold = 0.001;

    // Reconnect backoff
    QTimer m_reconnectTimer;
    int    m_reconnectDelayMs = 1000;
    static constexpr int kMaxReconnectDelayMs = 30000;
};
