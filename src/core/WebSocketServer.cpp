#include "WebSocketServer.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHostAddress>
#include <QSslKey>
#include <QSslCertificate>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QRandomGenerator>
#include <QDebug>
#include <algorithm>

WebSocketServer::WebSocketServer(QObject *parent) : QObject(parent) {
    setupDirs();
    m_token = loadOrGenerateToken();
}

WebSocketServer::~WebSocketServer() {
    stop();
}

void WebSocketServer::setupDirs() {
    m_configDir = QDir::homePath() + "/.magistrala_can4";
    QDir().mkpath(m_configDir);
    m_certPath = m_configDir + "/server.crt";
    m_keyPath  = m_configDir + "/server.key";
}

QString WebSocketServer::certPath() const { return m_certPath; }
QString WebSocketServer::keyPath()  const { return m_keyPath; }
QString WebSocketServer::token()    const { return m_token; }

// ── Token ────────────────────────────────────────────────────

QString WebSocketServer::loadOrGenerateToken() {
    QString tokenFile = m_configDir + "/token";
    QFile f(tokenFile);
    if (f.open(QIODevice::ReadOnly)) {
        QString tok = f.readAll().trimmed();
        f.close();
        if (tok.length() == 64) return tok;
    }
    return regenerateToken();
}

QString WebSocketServer::regenerateToken() {
    QString tokenFile = m_configDir + "/token";
    QString tok;
    tok.reserve(64);
    for (int i = 0; i < 64; ++i)
        tok += QString("%1").arg(QRandomGenerator::global()->bounded(256), 2, 16, QChar('0'));
    QFile f(tokenFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(tok.toUtf8());
        f.close();
    }
    m_token = tok;
    qDebug() << "Token zapisany do" << tokenFile;
    return tok;
}

// ── Certyfikat self‑signed (openssl) ─────────────────────────

bool WebSocketServer::ensureCertificate() {
    if (QFile::exists(m_certPath) && QFile::exists(m_keyPath))
        return true;

    qDebug() << "Generowanie certyfikatu self-signed dla WSS...";
    QProcess proc;
    proc.start("openssl", {
        "req", "-x509", "-newkey", "rsa:4096",
        "-keyout", m_keyPath,
        "-out", m_certPath,
        "-days", "3650",
        "-nodes",
        "-subj", "/C=PL/O=MagistralaCAN4/CN=remote-can-server"
    });
    proc.waitForFinished(15000);

    if (proc.exitCode() != 0) {
        qWarning() << "openssl nie powiódł się:" << proc.readAllStandardError();
        return false;
    }

    QFile::setPermissions(m_keyPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    qDebug() << "Certyfikat wygenerowany:" << m_certPath;
    return true;
}

// ── Start / Stop ─────────────────────────────────────────────

void WebSocketServer::start(quint16 port) {
    if (m_running) { stop(); }

    m_secureMode = false;
    m_server = new QWebSocketServer(
        QStringLiteral("MagistralaCAN4"),
        QWebSocketServer::NonSecureMode, this);

    if (!m_server->listen(QHostAddress::Any, port)) {
        emit errorOccurred(QString("WebSocket: nie można nasłuchiwać na porcie %1 (%2)")
                           .arg(port).arg(m_server->errorString()));
        delete m_server; m_server = nullptr;
        return;
    }

    connect(m_server, &QWebSocketServer::newConnection, this, &WebSocketServer::onNewConnection);
    connect(m_server, &QWebSocketServer::acceptError, this, [this]() {
        emit errorOccurred(QString("WebSocket accept error: %1").arg(m_server->errorString()));
    });

    m_running = true;
    emit statusChanged(true);
    qDebug() << "WebSocket (ws://) nasłuchuje na porcie" << port;
}

void WebSocketServer::startSecure(quint16 port) {
    if (m_running) { stop(); }

    if (!ensureCertificate()) {
        emit errorOccurred("Nie można wygenerować certyfikatu TLS. Sprawdź openssl.");
        return;
    }

    m_secureMode = true;
    m_server = new QWebSocketServer(
        QStringLiteral("MagistralaCAN4"),
        QWebSocketServer::SecureMode, this);

    // Konfiguracja SSL
    QSslConfiguration sslConfig = m_server->sslConfiguration();
    QFile certFile(m_certPath);
    if (!certFile.open(QIODevice::ReadOnly)) {
        emit errorOccurred("Nie można odczytać certyfikatu TLS.");
        delete m_server; m_server = nullptr;
        return;
    }
    QSslCertificate cert(&certFile, QSsl::Pem);
    certFile.close();

    QFile keyFile(m_keyPath);
    if (!keyFile.open(QIODevice::ReadOnly)) {
        emit errorOccurred("Nie można odczytać klucza TLS.");
        delete m_server; m_server = nullptr;
        return;
    }
    QSslKey key(&keyFile, QSsl::Rsa, QSsl::Pem);
    keyFile.close();

    sslConfig.setLocalCertificate(cert);
    sslConfig.setPrivateKey(key);
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone); // self‑signed
    m_server->setSslConfiguration(sslConfig);

    if (!m_server->listen(QHostAddress::Any, port)) {
        emit errorOccurred(QString("WebSocket WSS: nie można nasłuchiwać na porcie %1 (%2)")
                           .arg(port).arg(m_server->errorString()));
        delete m_server; m_server = nullptr;
        return;
    }

    connect(m_server, &QWebSocketServer::newConnection, this, &WebSocketServer::onNewConnection);
    connect(m_server, &QWebSocketServer::acceptError, this, [this]() {
        emit errorOccurred(QString("WSS accept error: %1").arg(m_server->errorString()));
    });
    connect(m_server, &QWebSocketServer::peerVerifyError, this, [this](const QSslError &e) {
        qDebug() << "WSS peer verify error:" << e.errorString();
    });

    m_running = true;
    emit statusChanged(true);
    qDebug() << "WebSocket (wss://) nasłuchuje na porcie" << port
             << "– token wymagany do autoryzacji";
}

void WebSocketServer::stop() {
    if (!m_running) return;

    if (m_server) m_server->close();

    // Zatrzymaj timery autoryzacji
    for (auto it = m_pendingAuthClients.begin(); it != m_pendingAuthClients.end(); ++it) {
        it.value()->stop();
        delete it.value();
    }
    m_pendingAuthClients.clear();

    // Rozłącz autoryzowanych klientów
    for (QWebSocket *client : m_authenticatedClients) {
        if (client->state() == QAbstractSocket::ConnectedState)
            client->close();
        client->deleteLater();
    }
    m_authenticatedClients.clear();

    if (m_server) { m_server->deleteLater(); m_server = nullptr; }

    m_running = false;
    m_secureMode = false;
    emit statusChanged(false);
    emit clientCountChanged(0);
    qDebug() << "WebSocket server zatrzymany";
}

// ── Połączenia ───────────────────────────────────────────────

void WebSocketServer::onNewConnection() {
    while (m_server && m_server->hasPendingConnections()) {
        QWebSocket *client = m_server->nextPendingConnection();
        if (!client) continue;

        connect(client, &QWebSocket::disconnected,
                this, &WebSocketServer::onClientDisconnected);
        connect(client, &QWebSocket::textMessageReceived,
                this, &WebSocketServer::onTextMessageReceived);
        connect(client, &QWebSocket::errorOccurred,
                this, [client](QAbstractSocket::SocketError) {
            qDebug() << "WS client error:" << client->errorString();
        });

        if (m_secureMode) {
            // W trybie WSS – klient musi się autoryzować
            auto *authTimer = new QTimer(this);
            authTimer->setSingleShot(true);
            connect(authTimer, &QTimer::timeout, this, &WebSocketServer::checkAuthTimeout);
            m_pendingAuthClients[client] = authTimer;
            authTimer->start(AUTH_TIMEOUT_MS);
            qDebug() << "WSS: nowy klient oczekuje na autoryzację";
        } else {
            // W trybie plaintext – akceptuj od razu
            m_authenticatedClients.append(client);
            qDebug() << "WS: klient podłączony – razem:" << m_authenticatedClients.size();
            emit clientCountChanged(m_authenticatedClients.size());
        }
    }
}

void WebSocketServer::onClientDisconnected() {
    auto *client = qobject_cast<QWebSocket *>(sender());
    if (!client) return;

    m_authenticatedClients.removeAll(client);
    if (m_pendingAuthClients.contains(client)) {
        delete m_pendingAuthClients.take(client);
    }
    client->deleteLater();
    qDebug() << "WS: klient rozłączony – razem:" << m_authenticatedClients.size();
    emit clientCountChanged(m_authenticatedClients.size());
}

void WebSocketServer::onTextMessageReceived(const QString &message) {
    auto *client = qobject_cast<QWebSocket *>(sender());
    if (!client) return;

    // Sprawdź, czy to wiadomość autoryzacyjna
    if (m_secureMode && m_pendingAuthClients.contains(client)) {
        QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj["type"].toString() == "auth" && obj["token"].toString() == m_token) {
                // Autoryzacja OK
                delete m_pendingAuthClients.take(client);
                m_authenticatedClients.append(client);
                qDebug() << "WSS: klient autoryzowany – razem:" << m_authenticatedClients.size();
                emit clientCountChanged(m_authenticatedClients.size());
                // Wyślij potwierdzenie
                client->sendTextMessage("{\"type\":\"auth_ok\"}");
                return;
            }
        }
        // Błędny token – zamknij połączenie
        qDebug() << "WSS: nieudana autoryzacja, zamykanie";
        client->sendTextMessage("{\"type\":\"auth_error\",\"msg\":\"Invalid token\"}");
        delete m_pendingAuthClients.take(client);
        client->close();
        return;
    }

    // Inne wiadomości – loguj
    qDebug() << "WS wiadomość od klienta:" << message.left(120);
}

void WebSocketServer::checkAuthTimeout() {
    // Znajdź klientów, którzy przekroczyli timeout
    QList<QWebSocket *> toRemove;
    for (auto it = m_pendingAuthClients.begin(); it != m_pendingAuthClients.end(); ++it) {
        if (!it.value()->isActive()) {
            toRemove.append(it.key());
        }
    }
    for (auto *client : toRemove) {
        qDebug() << "WSS: timeout autoryzacji, zamykanie";
        delete m_pendingAuthClients.take(client);
        client->sendTextMessage("{\"type\":\"auth_error\",\"msg\":\"Auth timeout\"}");
        client->close();
    }
}

// ── Broadcast ────────────────────────────────────────────────

void WebSocketServer::broadcastFrameBatch(const QVector<CanFrame> &frames) {
    if (!m_running || frames.isEmpty()) return;

    // Usuń martwe sockety
    m_authenticatedClients.erase(
        std::remove_if(m_authenticatedClients.begin(), m_authenticatedClients.end(),
                       [](QWebSocket *ws) {
                           return !ws || ws->state() != QAbstractSocket::ConnectedState;
                       }),
        m_authenticatedClients.end()
    );

    if (m_authenticatedClients.isEmpty()) return;

    // Zbuduj tablicę JSON
    QJsonArray arr;
    for (const CanFrame &frame : frames) {
        QJsonObject obj;
        obj[QStringLiteral("id")]        = static_cast<int>(frame.id);
        obj[QStringLiteral("extended")]  = frame.extended;
        obj[QStringLiteral("rtr")]       = frame.rtr;
        obj[QStringLiteral("fd")]        = frame.fd;
        obj[QStringLiteral("dlc")]       = static_cast<int>(frame.dlc);
        obj[QStringLiteral("timestamp")] = static_cast<qint64>(frame.timestamp);
        QString hexData;
        hexData.reserve(frame.dlc * 2);
        for (int i = 0; i < frame.dlc && i < 64; ++i)
            hexData += QStringLiteral("%1").arg(frame.data[i], 2, 16, QLatin1Char('0'));
        obj[QStringLiteral("data")] = hexData;
        arr.append(obj);
    }

    QJsonObject root;
    root[QStringLiteral("type")]   = QStringLiteral("frames");
    root[QStringLiteral("count")]  = arr.size();
    root[QStringLiteral("frames")] = arr;

    const QString json = QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Compact));

    for (QWebSocket *client : m_authenticatedClients)
        client->sendTextMessage(json);
}


