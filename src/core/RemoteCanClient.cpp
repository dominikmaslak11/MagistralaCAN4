#include "RemoteCanClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSslError>
#include <QDebug>

RemoteCanClient::RemoteCanClient(QObject *parent) : QObject(parent) {
    m_reconnectTimer.setSingleShot(true);
    m_reconnectTimer.setInterval(3000);

    connect(&m_socket, &QWebSocket::connected, this, &RemoteCanClient::onConnected);
    connect(&m_socket, &QWebSocket::disconnected, this, &RemoteCanClient::onDisconnected);
    connect(&m_socket, &QWebSocket::textMessageReceived, this, &RemoteCanClient::onTextMessageReceived);
    connect(&m_socket, &QWebSocket::errorOccurred, this, &RemoteCanClient::onError);

    // SSL – akceptuj self‑signed certyfikaty
    connect(&m_socket, &QWebSocket::sslErrors, this, &RemoteCanClient::onSslErrors);

    connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (!m_intentionalDisconnect && !isConnected()) {
            qDebug() << "RemoteCanClient: ponawianie połączenia (backoff" << m_reconnectDelayMs << "ms)...";
            m_socket.open(QUrl(m_url));
        }
    });
}

RemoteCanClient::~RemoteCanClient() {
    m_intentionalDisconnect = true;
    m_reconnectTimer.stop();
    m_socket.close();
}

void RemoteCanClient::connectToServer(const QString &url, const QString &token) {
    m_url = url;
    m_token = token;
    m_intentionalDisconnect = false;
    m_authenticated = false;
    m_frameCount = 0;
    m_reconnectDelayMs = 1000;  // reset backoff

    qDebug() << "RemoteCanClient: łączę z" << url;
    m_socket.open(QUrl(url));
}

void RemoteCanClient::disconnect() {
    m_intentionalDisconnect = true;
    m_reconnectTimer.stop();
    m_socket.close();
    m_connected = false;
    m_authenticated = false;
    emit statusChanged(false, "Rozłączono");
}

// ── Sloty ────────────────────────────────────────────────────

void RemoteCanClient::onConnected() {
    m_connected = true;

    if (m_url.startsWith("ws://", Qt::CaseInsensitive)) {
        // Tryb plaintext — serwer akceptuje bez tokena, klient jest od razu gotowy
        m_authenticated = true;
        m_reconnectDelayMs = 1000;
        qDebug() << "RemoteCanClient: połączono (WS plaintext), gotowy";
        emit statusChanged(true, "Połączono");
    } else {
        // Tryb WSS — wyślij wiadomość autoryzacyjną
        QJsonObject auth;
        auth["type"]  = "auth";
        auth["token"] = m_token;
        m_socket.sendTextMessage(QString::fromUtf8(
            QJsonDocument(auth).toJson(QJsonDocument::Compact)));
        qDebug() << "RemoteCanClient: połączono (WSS), wysyłam token...";
        emit statusChanged(true, "Autoryzacja...");
    }
}

void RemoteCanClient::onDisconnected() {
    bool wasConnected = m_connected;
    m_connected = false;
    m_authenticated = false;
    qDebug() << "RemoteCanClient: rozłączono";

    if (!m_intentionalDisconnect) {
        emit statusChanged(false, QString("Rozłączono – ponawianie za %1s...").arg(m_reconnectDelayMs / 1000.0, 0, 'f', 1));
        m_reconnectTimer.setInterval(m_reconnectDelayMs);
        m_reconnectTimer.start();
        // Exponential backoff: podwój delay, maks. 30s
        m_reconnectDelayMs = std::min(m_reconnectDelayMs * 2, MAX_RECONNECT_DELAY_MS);
    } else {
        emit statusChanged(false, "Rozłączono");
    }
}

void RemoteCanClient::onTextMessageReceived(const QString &message) {
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) return;

    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();

    if (type == "auth_ok") {
        m_authenticated = true;
        m_reconnectTimer.stop();
        m_reconnectDelayMs = 1000;  // reset backoff po udanym połączeniu
        qDebug() << "RemoteCanClient: autoryzacja OK, odbieram ramki";
        emit statusChanged(true, "Połączono i autoryzowano");
        return;
    }

    if (type == "auth_error") {
        qWarning() << "RemoteCanClient: błąd autoryzacji:" << obj["msg"].toString();
        emit errorOccurred("Autoryzacja odrzucona: " + obj["msg"].toString());
        m_intentionalDisconnect = true;
        m_socket.close();
        return;
    }

    if (type == "frames" && m_authenticated) {
        QJsonArray arr = obj["frames"].toArray();
        for (const QJsonValue &val : arr) {
            if (!val.isObject()) continue;
            CanFrame frame = parseFrameJson(val.toObject());
            m_frameCount++;
            emit newFrame(frame);
        }
    }
}

void RemoteCanClient::onError(QAbstractSocket::SocketError error) {
    Q_UNUSED(error);
    qWarning() << "RemoteCanClient: błąd:" << m_socket.errorString();
    emit errorOccurred(m_socket.errorString());
}

void RemoteCanClient::onSslErrors(const QList<QSslError> &errors) {
    // Akceptuj wszystkie błędy SSL (self‑signed cert)
    for (const auto &e : errors) {
        qDebug() << "RemoteCanClient: SSL error (ignorowany):" << e.errorString();
    }
    m_socket.ignoreSslErrors();
}

// ── Parsowanie JSON ──────────────────────────────────────────

CanFrame RemoteCanClient::parseFrameJson(const QJsonObject &obj) const {
    CanFrame frame;
    frame.id        = static_cast<uint32_t>(obj["id"].toInt());
    frame.extended  = obj["extended"].toBool();
    frame.rtr       = obj["rtr"].toBool();
    frame.error     = obj["error"].toBool();
    frame.fd        = obj["fd"].toBool();
    frame.dlc       = static_cast<uint8_t>(obj["dlc"].toInt());
    frame.timestamp = static_cast<uint64_t>(obj["timestamp"].toVariant().toLongLong());

    // Server encodes data as lowercase hex string (e.g. "0102aabb")
    QString hex = obj["data"].toString();
    int maxBytes = qMin(frame.dlc, static_cast<uint8_t>(64));
    for (int i = 0; i < maxBytes && (i * 2 + 1) < hex.length(); ++i)
        frame.data[i] = static_cast<uint8_t>(hex.mid(i * 2, 2).toUInt(nullptr, 16));

    return frame;
}
