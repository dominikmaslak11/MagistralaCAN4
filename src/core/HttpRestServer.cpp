#include "HttpRestServer.h"
#include "CanFrameModel.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>
#include <QUrl>
#include <QDebug>

// ── Construction ─────────────────────────────────────────────────────────────

HttpRestServer::HttpRestServer(QObject *parent) : QObject(parent) {}

HttpRestServer::~HttpRestServer() { stop(); }

// ── Public API ────────────────────────────────────────────────────────────────

bool HttpRestServer::start(quint16 port) {
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::Any, port)) return false;
    connect(m_server, &QTcpServer::newConnection, this, &HttpRestServer::onNewConnection);
    m_running = true;
    qDebug() << "REST API listening on port" << port;
    return true;
}

void HttpRestServer::stop() {
    if (m_server) { m_server->close(); delete m_server; m_server = nullptr; }
    m_running = false;
}

void HttpRestServer::setAlertEngine(CanAlertEngine *engine) {
    m_alertEngine = engine;
    if (engine)
        connect(engine, &CanAlertEngine::alertTriggered,
                this,   &HttpRestServer::onAlertTriggered,
                Qt::QueuedConnection);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void HttpRestServer::onNewConnection() {
    while (m_server && m_server->hasPendingConnections()) {
        auto *sock = m_server->nextPendingConnection();
        connect(sock, &QTcpSocket::readyRead,    this, &HttpRestServer::onReadyRead);
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

void HttpRestServer::onReadyRead() {
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock) return;
    QByteArray raw = sock->readAll();
    auto req = parseRawRequest(raw);
    if (req.method.isEmpty()) { sock->close(); return; }
    handleRequest(sock, req);
}

void HttpRestServer::onAlertTriggered(const CanAlert &alert) {
    QJsonObject obj;
    obj["ruleName"]    = alert.ruleName;
    obj["description"] = alert.description;
    obj["id"]          = QString("0x%1").arg(alert.frame.id, 0, 16);
    obj["dlc"]         = alert.frame.dlc;
    obj["timestampUs"] = static_cast<qint64>(alert.timestampUs);
    QString data;
    for (int b = 0; b < alert.frame.dlc && b < 8; ++b)
        data += QString("%1").arg(alert.frame.data[b], 2, 16, QChar('0'));
    obj["data"] = data;

    std::lock_guard<std::mutex> lock(m_alertMutex);
    m_recentAlerts.push_back(std::move(obj));
    if (static_cast<int>(m_recentAlerts.size()) > kMaxAlerts)
        m_recentAlerts.pop_front();
}

// ── Request parsing ───────────────────────────────────────────────────────────

HttpRestServer::ParsedRequest HttpRestServer::parseRawRequest(const QByteArray &raw) const {
    ParsedRequest pr;
    QString text = QString::fromUtf8(raw);
    QStringList lines = text.split("\r\n");
    if (lines.isEmpty()) return pr;

    QStringList parts = lines[0].split(' ');
    if (parts.size() < 2) return pr;
    pr.method = parts[0].toUpper();

    // Split path from query string
    QString fullPath = parts[1];
    int qmark = static_cast<int>(fullPath.indexOf('?'));
    if (qmark >= 0) {
        pr.path  = fullPath.left(qmark);
        pr.query = fullPath.mid(qmark + 1);
    } else {
        pr.path = fullPath;
    }

    int bodyStart = static_cast<int>(raw.indexOf("\r\n\r\n"));
    if (bodyStart >= 0)
        pr.body = raw.mid(bodyStart + 4);

    return pr;
}

QString HttpRestServer::queryParam(const QString &query, const QString &key) const {
    // Simple key=value parser (URL-decoded via QUrlQuery)
    QUrlQuery q(query);
    return q.queryItemValue(key, QUrl::FullyDecoded);
}

// ── Request dispatch ──────────────────────────────────────────────────────────

void HttpRestServer::handleRequest(QTcpSocket *sock, const ParsedRequest &req) {
    const QString &method = req.method;
    const QString &path   = req.path;

    // CORS preflight
    if (method == "OPTIONS") {
        sendCors(sock);
        return;
    }

    // ── GET /api/status ───────────────────────────────────────────────────────
    if (path == "/api/status" && method == "GET") {
        QJsonObject obj;
        obj["running"]    = m_running;
        obj["frames"]     = m_model ? m_model->rowCount() : 0;
        obj["fps"]        = fps;
        obj["uniqueIds"]  = uniqueIds;
        sendJson(sock, 200, QJsonDocument(obj));
        return;
    }

    // ── GET /api/stats ────────────────────────────────────────────────────────
    if (path == "/api/stats" && method == "GET") {
        QJsonObject obj;
        obj["fps"]         = fps;
        obj["uniqueIds"]   = uniqueIds;
        obj["totalFrames"] = m_model ? m_model->rowCount() : 0;
        obj["busLoadPct"]  = busLoadPct;
        obj["alertCount"]  = m_alertEngine ? m_alertEngine->totalAlerts() : 0;
        sendJson(sock, 200, QJsonDocument(obj));
        return;
    }

    // ── GET /api/frames ───────────────────────────────────────────────────────
    if (path == "/api/frames" && method == "GET") {
        int limit = 100;
        QString limitStr = queryParam(req.query, "limit");
        if (!limitStr.isEmpty()) limit = std::clamp(limitStr.toInt(), 1, 5000);

        bool filterById = false;
        uint32_t filterId = 0;
        QString idStr = queryParam(req.query, "id");
        if (!idStr.isEmpty()) {
            bool ok;
            filterId   = idStr.toUInt(&ok, 0);  // 0 = auto-detect base (0x hex)
            filterById = ok;
        }

        QJsonArray arr;
        if (m_model) {
            int total = m_model->rowCount();
            int added = 0;
            // iterate newest first (rows are oldest-first in model, so we go backwards)
            for (int i = total - 1; i >= 0 && added < limit; --i) {
                CanFrame frame = m_model->frameAt(i);
                if (filterById && frame.id != filterId) continue;

                QJsonObject f;
                f["id"]        = QString("0x%1").arg(frame.id, 0, 16);
                f["idDec"]     = static_cast<qint64>(frame.id);
                f["dlc"]       = frame.dlc;
                f["timestamp"] = static_cast<qint64>(frame.timestamp);
                f["extended"]  = frame.extended;
                f["fd"]        = frame.fd;

                QString dataHex;
                for (int b = 0; b < frame.dlc && b < 64; ++b)
                    dataHex += QString("%1").arg(frame.data[b], 2, 16, QChar('0'));
                f["data"] = dataHex;

                QJsonArray dataArr;
                for (int b = 0; b < frame.dlc && b < 64; ++b)
                    dataArr.append(frame.data[b]);
                f["dataBytes"] = dataArr;

                arr.append(f);
                ++added;
            }
        }
        sendJson(sock, 200, QJsonDocument(arr));
        return;
    }

    // ── GET /api/ids ──────────────────────────────────────────────────────────
    if (path == "/api/ids" && method == "GET") {
        QHash<uint32_t, int> freq;
        if (m_model) {
            int total = m_model->rowCount();
            for (int i = 0; i < total; ++i)
                freq[m_model->frameAt(i).id]++;
        }

        // Sort by count descending
        QVector<QPair<int, uint32_t>> sorted;
        sorted.reserve(freq.size());
        for (auto it = freq.begin(); it != freq.end(); ++it)
            sorted.append({it.value(), it.key()});
        std::sort(sorted.begin(), sorted.end(),
                  [](const auto &a, const auto &b) { return a.first > b.first; });

        QJsonArray arr;
        for (const auto &[count, id] : sorted) {
            QJsonObject obj;
            obj["id"]    = QString("0x%1").arg(id, 0, 16);
            obj["idDec"] = static_cast<qint64>(id);
            obj["count"] = count;
            arr.append(obj);
        }
        sendJson(sock, 200, QJsonDocument(arr));
        return;
    }

    // ── GET /api/alerts ───────────────────────────────────────────────────────
    if (path == "/api/alerts" && method == "GET") {
        QJsonArray arr;
        std::lock_guard<std::mutex> lock(m_alertMutex);
        for (auto it = m_recentAlerts.rbegin(); it != m_recentAlerts.rend(); ++it)
            arr.append(*it);
        sendJson(sock, 200, QJsonDocument(arr));
        return;
    }

    // ── POST /api/send ────────────────────────────────────────────────────────
    if (path == "/api/send" && method == "POST") {
        if (req.body.isEmpty()) { sendError(sock, 400, "empty body"); return; }
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(req.body, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            sendError(sock, 400, "invalid JSON"); return;
        }
        QJsonObject body = doc.object();
        if (!body.contains("id")) { sendError(sock, 400, "missing 'id'"); return; }

        CanFrame frame{};
        // id can be decimal or "0x..." hex string
        QJsonValue idVal = body["id"];
        if (idVal.isString())
            frame.id = idVal.toString().toUInt(nullptr, 0);
        else
            frame.id = static_cast<uint32_t>(idVal.toInt());

        frame.dlc = static_cast<uint8_t>(std::min(body.value("dlc").toInt(8), 64));
        frame.extended = body.value("extended").toBool(frame.id > 0x7FF);

        QJsonArray dataArr = body.value("data").toArray();
        for (int i = 0; i < dataArr.size() && i < 64; ++i)
            frame.data[i] = static_cast<uint8_t>(dataArr[i].toInt());
        if (frame.dlc == 8 && dataArr.isEmpty()) frame.dlc = 0;

        emit sendFrameRequested(frame);
        QJsonObject resp;
        resp["status"] = "queued";
        resp["id"]     = QString("0x%1").arg(frame.id, 0, 16);
        resp["dlc"]    = frame.dlc;
        sendJson(sock, 202, QJsonDocument(resp));
        return;
    }

    // ── POST /api/start & /api/stop ───────────────────────────────────────────
    if (path == "/api/start" && method == "POST") {
        emit startRequested();
        sendJson(sock, 200, QJsonDocument(QJsonObject{{"status","ok"}}));
        return;
    }
    if (path == "/api/stop" && method == "POST") {
        emit stopRequested();
        sendJson(sock, 200, QJsonDocument(QJsonObject{{"status","ok"}}));
        return;
    }

    sendError(sock, 404, "not found");
}

// ── Response helpers ──────────────────────────────────────────────────────────

namespace {

const char *kCorsHeaders =
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type\r\n";

} // namespace

void HttpRestServer::sendJson(QTcpSocket *sock, int code, const QJsonDocument &doc) {
    QByteArray body = doc.toJson(QJsonDocument::Compact);
    QString resp = QString("HTTP/1.1 %1 OK\r\n"
                           "Content-Type: application/json; charset=utf-8\r\n"
                           "Content-Length: %2\r\n"
                           "%3\r\n")
                       .arg(code).arg(body.size()).arg(kCorsHeaders);
    sock->write(resp.toUtf8());
    sock->write(body);
    sock->flush();
    sock->close();
}

void HttpRestServer::sendError(QTcpSocket *sock, int code, const QString &msg) {
    QJsonObject obj;
    obj["error"] = msg;
    sendJson(sock, code, QJsonDocument(obj));
}

void HttpRestServer::sendCors(QTcpSocket *sock) {
    QString resp = QString("HTTP/1.1 204 No Content\r\n%1\r\n").arg(kCorsHeaders);
    sock->write(resp.toUtf8());
    sock->flush();
    sock->close();
}
