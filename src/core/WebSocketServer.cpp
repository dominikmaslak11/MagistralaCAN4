#include "WebSocketServer.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHostAddress>
#include <QDebug>

WebSocketServer::WebSocketServer(QObject *parent) : QObject(parent) {}

WebSocketServer::~WebSocketServer() {
    stop();
}

void WebSocketServer::start(quint16 port) {
    if (m_running) {
        qDebug() << "WebSocket server already running";
        return;
    }

    m_server = new QWebSocketServer(
        QStringLiteral("MagistralaCAN4"),
        QWebSocketServer::NonSecureMode,
        this
    );

    if (!m_server->listen(QHostAddress::Any, port)) {
        const QString msg = QStringLiteral(
            "WebSocket: nie można nasłuchiwać na porcie %1 (%2)"
        ).arg(port).arg(m_server->errorString());
        emit errorOccurred(msg);
        delete m_server;
        m_server = nullptr;
        return;
    }

    connect(m_server, &QWebSocketServer::newConnection,
            this, &WebSocketServer::onNewConnection);
    connect(m_server, &QWebSocketServer::acceptError,
            this, [this]() {
        emit errorOccurred(QStringLiteral("WebSocket accept error: %1")
                          .arg(m_server->errorString()));
    });

    m_running = true;
    emit statusChanged(true);
    qDebug() << "WebSocket server nasłuchuje na porcie" << port;
}

void WebSocketServer::stop() {
    if (!m_running) return;

    // Zamknij socket nasłuchujący – zatrzyma nowe połączenia
    if (m_server) {
        m_server->close();
    }

    // Rozłącz wszystkich klientów
    for (QWebSocket *client : m_clients) {
        if (client->state() == QAbstractSocket::ConnectedState) {
            client->close();
        }
        client->deleteLater();
    }
    m_clients.clear();

    if (m_server) {
        m_server->deleteLater();
        m_server = nullptr;
    }

    m_running = false;
    emit statusChanged(false);
    emit clientCountChanged(0);
    qDebug() << "WebSocket server zatrzymany";
}

void WebSocketServer::broadcastFrame(const CanFrame &frame) {
    if (!m_running) return;

    // Usuń martwe sockety z listy
    m_clients.erase(
        std::remove_if(m_clients.begin(), m_clients.end(),
                       [](QWebSocket *ws) {
                           return !ws || ws->state() != QAbstractSocket::ConnectedState;
                       }),
        m_clients.end()
    );

    if (m_clients.isEmpty()) return;

    const QString json = frameToJson(frame);
    for (QWebSocket *client : m_clients) {
        client->sendTextMessage(json);
    }
}

void WebSocketServer::onNewConnection() {
    while (m_server && m_server->hasPendingConnections()) {
        QWebSocket *client = m_server->nextPendingConnection();
        if (!client) continue;

        connect(client, &QWebSocket::disconnected,
                this, &WebSocketServer::onClientDisconnected);
        connect(client, &QWebSocket::textMessageReceived,
                this, &WebSocketServer::onTextMessageReceived);

        // Obsługa błędów per-klient (Qt 6.10+: errorOccurred)
        connect(client, &QWebSocket::errorOccurred,
                this, [client](QAbstractSocket::SocketError) {
            qDebug() << "WebSocket client error:" << client->errorString();
        });

        m_clients.append(client);
        qDebug() << "WebSocket klient podłączony – razem:" << m_clients.size();
    }
    emit clientCountChanged(m_clients.size());
}

void WebSocketServer::onClientDisconnected() {
    auto *client = qobject_cast<QWebSocket *>(sender());
    if (!client) return;

    m_clients.removeAll(client);
    client->deleteLater();
    qDebug() << "WebSocket klient rozłączony – razem:" << m_clients.size();
    emit clientCountChanged(m_clients.size());
}

void WebSocketServer::onTextMessageReceived(const QString &message) {
    // Placeholder na przyszłe komendy od klientów
    qDebug() << "WebSocket wiadomość od klienta:" << message.left(120);
}

QString WebSocketServer::frameToJson(const CanFrame &frame) const {
    QJsonObject obj;
    obj[QStringLiteral("type")]      = QStringLiteral("frame");
    obj[QStringLiteral("id")]        = static_cast<int>(frame.id);
    obj[QStringLiteral("extended")]  = frame.extended;
    obj[QStringLiteral("rtr")]       = frame.rtr;
    obj[QStringLiteral("error")]     = frame.error;
    obj[QStringLiteral("fd")]        = frame.fd;
    obj[QStringLiteral("dlc")]       = static_cast<int>(frame.dlc);
    obj[QStringLiteral("timestamp")] = static_cast<qint64>(frame.timestamp);

    // Dane jako hex string (zwarty)
    QString hexData;
    hexData.reserve(frame.dlc * 2);
    for (int i = 0; i < frame.dlc && i < 64; ++i) {
        hexData += QStringLiteral("%1").arg(frame.data[i], 2, 16, QLatin1Char('0'));
    }
    obj[QStringLiteral("data")] = hexData;

    // Dane jako tablica bajtów (dla wygody JS)
    QJsonArray dataBytes;
    for (int i = 0; i < frame.dlc && i < 64; ++i) {
        dataBytes.append(static_cast<int>(frame.data[i]));
    }
    obj[QStringLiteral("dataBytes")] = dataBytes;

    return QString::fromUtf8(
        QJsonDocument(obj).toJson(QJsonDocument::Compact)
    );
}
