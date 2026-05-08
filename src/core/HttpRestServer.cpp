#include "HttpRestServer.h"
#include "CanFrameModel.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

HttpRestServer::HttpRestServer(QObject *parent) : QObject(parent) {}

HttpRestServer::~HttpRestServer() { stop(); }

bool HttpRestServer::start(quint16 port) {
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::Any, port)) return false;
    connect(m_server, &QTcpServer::newConnection, this, &HttpRestServer::onNewConnection);
    m_running = true;
    qDebug() << "REST API nasłuchuje na porcie" << port;
    return true;
}

void HttpRestServer::stop() {
    if (m_server) { m_server->close(); delete m_server; m_server = nullptr; }
    m_running = false;
}

void HttpRestServer::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        auto *sock = m_server->nextPendingConnection();
        connect(sock, &QTcpSocket::readyRead, this, &HttpRestServer::onReadyRead);
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

void HttpRestServer::onReadyRead() {
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock) return;
    QByteArray data = sock->readAll();
    QString req = QString::fromUtf8(data);
    QStringList lines = req.split("\r\n");
    if (lines.isEmpty()) return;
    QString first = lines[0];
    QStringList parts = first.split(' ');
    if (parts.size() < 2) { sock->close(); return; }
    QString method = parts[0], path = parts[1];
    int bodyStart = req.indexOf("\r\n\r\n");
    QByteArray body = (bodyStart >= 0) ? data.mid(bodyStart + 4) : QByteArray();
    handleRequest(sock, method, path, body);
}

void HttpRestServer::handleRequest(QTcpSocket *sock, const QString &method, const QString &path, const QByteArray &body) {
    Q_UNUSED(body);

    if (path == "/api/status") {
        QJsonObject obj;
        obj["running"] = m_model ? (m_model->rowCount() > 0) : false;
        obj["frames"] = m_model ? m_model->rowCount() : 0;
        obj["fps"] = fps;
        obj["uniqueIds"] = uniqueIds;
        sendJson(sock, 200, QJsonDocument(obj));
    } else if (path == "/api/frames") {
        QJsonArray arr;
        if (m_model) {
            for (int i = 0; i < std::min(m_model->rowCount(), 100); ++i) {
                QModelIndex idx = m_model->index(i, 0);
                QJsonObject f;
                CanFrame frame = m_model->frameAt(i);
                f["id"] = (int)frame.id;
                f["dlc"] = frame.dlc;
                f["timestamp"] = (qint64)frame.timestamp;
                QString data;
                for (int b = 0; b < frame.dlc && b < 8; ++b)
                    data += QString("%1").arg(frame.data[b], 2, 16, QChar('0'));
                f["data"] = data;
                arr.append(f);
            }
        }
        sendJson(sock, 200, QJsonDocument(arr));
    } else if (path == "/api/start" && method == "POST") {
        emit startRequested();
        sendText(sock, 200, "{\"status\":\"ok\"}");
    } else if (path == "/api/stop" && method == "POST") {
        emit stopRequested();
        sendText(sock, 200, "{\"status\":\"ok\"}");
    } else {
        sendText(sock, 404, "{\"error\":\"not found\"}");
    }
    sock->close();
}

void HttpRestServer::sendJson(QTcpSocket *sock, int code, const QJsonDocument &doc) {
    QByteArray body = doc.toJson(QJsonDocument::Compact);
    QString resp = QString("HTTP/1.1 %1 OK\r\nContent-Type: application/json\r\nContent-Length: %2\r\n\r\n")
        .arg(code).arg(body.size());
    sock->write(resp.toUtf8() + body);
}

void HttpRestServer::sendText(QTcpSocket *sock, int code, const QString &text) {
    QByteArray body = text.toUtf8();
    QString resp = QString("HTTP/1.1 %1 OK\r\nContent-Type: application/json\r\nContent-Length: %2\r\n\r\n")
        .arg(code).arg(body.size());
    sock->write(resp.toUtf8() + body);
}
