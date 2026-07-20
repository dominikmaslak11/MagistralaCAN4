#include "McpServer.h"
#include "CanFrameModel.h"
#include "DbcParser.h"
#include "ArxmlParser.h"
#include "LuaScriptEngine.h"
#include "CanNodeSimulator.h"
#include "CanPlayer.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>
#include <QDebug>
#include <algorithm>

// ── Construction ─────────────────────────────────────────────────────────────

McpServer::McpServer(QObject *parent) : QObject(parent) {}

McpServer::~McpServer() { stop(); }

// ── Public API ────────────────────────────────────────────────────────────────

bool McpServer::start(quint16 port) {
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, port)) return false;
    connect(m_server, &QTcpServer::newConnection, this, &McpServer::onNewConnection);
    m_running = true;
    qDebug() << "MCP server listening on 127.0.0.1:" << port << "(endpoint: /mcp)";
    return true;
}

void McpServer::stop() {
    if (m_server) { m_server->close(); delete m_server; m_server = nullptr; }
    m_running = false;
}

void McpServer::setAlertEngine(CanAlertEngine *engine) {
    m_alertEngine = engine;
    if (engine)
        connect(engine, &CanAlertEngine::alertTriggered,
                this,   &McpServer::onAlertTriggered,
                Qt::QueuedConnection);
}

// ── Slots ─────────────────────────────────────────────────────────────────────

void McpServer::onNewConnection() {
    while (m_server && m_server->hasPendingConnections()) {
        auto *sock = m_server->nextPendingConnection();
        connect(sock, &QTcpSocket::readyRead,    this, &McpServer::onReadyRead);
        connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
    }
}

void McpServer::onReadyRead() {
    auto *sock = qobject_cast<QTcpSocket *>(sender());
    if (!sock) return;
    QByteArray raw = sock->readAll();
    auto req = parseRawRequest(raw);
    if (req.method.isEmpty()) { sock->close(); return; }
    handleRequest(sock, req);
}

void McpServer::onAlertTriggered(const CanAlert &alert) {
    QJsonObject obj;
    obj["ruleName"]    = alert.ruleName;
    obj["description"] = alert.description;
    obj["id"]          = QString("0x%1").arg(alert.frame.id, 0, 16);
    obj["dlc"]         = alert.frame.dlc;
    obj["timestampUs"] = static_cast<qint64>(alert.timestampUs);

    std::lock_guard<std::mutex> lock(m_alertMutex);
    m_recentAlerts.push_back(std::move(obj));
    if (static_cast<int>(m_recentAlerts.size()) > kMaxAlerts)
        m_recentAlerts.pop_front();
}

// ── Request parsing ───────────────────────────────────────────────────────────

McpServer::ParsedRequest McpServer::parseRawRequest(const QByteArray &raw) const {
    ParsedRequest pr;
    int headerEnd = static_cast<int>(raw.indexOf("\r\n\r\n"));
    QByteArray headPart = headerEnd >= 0 ? raw.left(headerEnd) : raw;
    pr.headerBlock = headPart;

    QList<QByteArray> lines = headPart.split('\n');
    if (lines.isEmpty()) return pr;
    QString requestLine = QString::fromUtf8(lines[0]).trimmed();
    QStringList parts = requestLine.split(' ');
    if (parts.size() < 2) return pr;
    pr.method = parts[0].toUpper();
    pr.path   = parts[1];

    if (headerEnd >= 0)
        pr.body = raw.mid(headerEnd + 4);

    return pr;
}

QString McpServer::headerValue(const QByteArray &headerBlock, const QString &name) const {
    const QList<QByteArray> lines = headerBlock.split('\n');
    const QString lowerName = name.toLower();
    for (const QByteArray &lineRaw : lines) {
        QString line = QString::fromUtf8(lineRaw).trimmed();
        int colon = static_cast<int>(line.indexOf(':'));
        if (colon < 0) continue;
        if (line.left(colon).trimmed().toLower() == lowerName)
            return line.mid(colon + 1).trimmed();
    }
    return QString();
}

// ── Request dispatch ──────────────────────────────────────────────────────────

void McpServer::handleRequest(QTcpSocket *sock, const ParsedRequest &req) {
    if (req.method == "OPTIONS") { sendHttp(sock, 204, "No Content", QByteArray()); return; }

    if (req.path != "/mcp") {
        sendHttp(sock, 404, "Not Found", QByteArrayLiteral("{\"error\":\"not found\"}"));
        return;
    }

    if (req.method == "GET") {
        // Nie oferujemy strumienia SSE serwer→klient — 405 zgodnie ze spec Streamable HTTP.
        sendHttp(sock, 405, "Method Not Allowed", QByteArray());
        return;
    }
    if (req.method == "DELETE") {
        // Brak zarządzania sesjami (tryb bezstanowy) — 405.
        sendHttp(sock, 405, "Method Not Allowed", QByteArray());
        return;
    }
    if (req.method != "POST") {
        sendHttp(sock, 405, "Method Not Allowed", QByteArray());
        return;
    }

    QString protoVersion = headerValue(req.headerBlock, "MCP-Protocol-Version");
    if (!protoVersion.isEmpty() && protoVersion != "2025-06-18" && protoVersion != "2025-03-26") {
        sendHttp(sock, 400, "Bad Request", QByteArrayLiteral("{\"error\":\"unsupported MCP-Protocol-Version\"}"));
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(req.body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        sendJsonRpcError(sock, QJsonValue(), -32700, "Parse error");
        return;
    }
    dispatchRpc(sock, doc.object(), protoVersion);
}

void McpServer::dispatchRpc(QTcpSocket *sock, const QJsonObject &rpc, const QString & /*protocolVersionHeader*/) {
    const QJsonValue idVal = rpc.value("id");
    const QString method   = rpc.value("method").toString();

    // Notyfikacja (brak "id") — np. notifications/initialized, notifications/cancelled.
    if (!rpc.contains("id")) { sendAccepted(sock); return; }

    if (method == "initialize") {
        sendJsonRpcResult(sock, idVal, handleInitialize(rpc.value("params").toObject()));
        return;
    }
    if (method == "ping") {
        sendJsonRpcResult(sock, idVal, QJsonObject{});
        return;
    }
    if (method == "tools/list") {
        QJsonObject result;
        result["tools"] = toolDefinitions();
        sendJsonRpcResult(sock, idVal, result);
        return;
    }
    if (method == "tools/call") {
        QJsonObject params = rpc.value("params").toObject();
        QString name = params.value("name").toString();
        QJsonObject args = params.value("arguments").toObject();

        if (name.isEmpty()) {
            sendJsonRpcError(sock, idVal, -32602, "Missing tool 'name'");
            return;
        }

        QJsonArray content;
        bool isError = false;
        callTool(name, args, content, isError);

        QJsonObject result;
        result["content"] = content;
        result["isError"] = isError;
        sendJsonRpcResult(sock, idVal, result);
        return;
    }

    sendJsonRpcError(sock, idVal, -32601, "Method not found: " + method);
}

QJsonObject McpServer::handleInitialize(const QJsonObject & /*params*/) const {
    QJsonObject result;
    result["protocolVersion"] = kProtocolVersion;

    QJsonObject tools;
    QJsonObject capabilities;
    capabilities["tools"] = tools;
    result["capabilities"] = capabilities;

    QJsonObject serverInfo;
    serverInfo["name"]    = "magistrala-can4";
    serverInfo["version"] = "2.3.0";
    result["serverInfo"] = serverInfo;

    return result;
}

// ── Definicje narzędzi (tools/list) ──────────────────────────────────────────

namespace {

QJsonObject makeSchema(const QJsonObject &properties, const QJsonArray &required = {}) {
    QJsonObject schema;
    schema["type"] = "object";
    schema["properties"] = properties;
    if (!required.isEmpty()) schema["required"] = required;
    return schema;
}

QJsonObject makeTool(const QString &name, const QString &desc, const QJsonObject &schema) {
    QJsonObject t;
    t["name"] = name;
    t["description"] = desc;
    t["inputSchema"] = schema;
    return t;
}

} // namespace

QJsonArray McpServer::toolDefinitions() const {
    QJsonArray arr;

    arr.append(makeTool("get_status", "Zwraca status i statystyki na żywo (liczba ramek, fps, unikalne ID, obciążenie magistrali, liczba alertów).",
        makeSchema({})));

    arr.append(makeTool("get_frames", "Zwraca ostatnie ramki CAN, opcjonalnie filtrowane po ID.",
        makeSchema({
            {"limit", QJsonObject{{"type","integer"},{"description","Maks. liczba ramek (domyślnie 50, max 2000)"}}},
            {"id",    QJsonObject{{"type","string"},{"description","Filtr po ID (dziesiętnie lub hex, np. '0x7E0')"}}}
        })));

    arr.append(makeTool("get_ids", "Zwraca listę unikalnych ID CAN widzianych w sesji, posortowaną po liczbie wystąpień malejąco.",
        makeSchema({})));

    arr.append(makeTool("get_alerts", "Zwraca ostatnie wyzwolone alerty (max 100) z CanAlertEngine.",
        makeSchema({})));

    arr.append(makeTool("send_frame", "Wysyła ramkę CAN na magistralę.",
        makeSchema({
            {"id",       QJsonObject{{"type","string"},{"description","ID ramki (dziesiętnie lub hex, np. '0x123')"}}},
            {"dlc",      QJsonObject{{"type","integer"},{"description","Długość danych (0-8, lub do 64 dla CAN FD)"}}},
            {"data",     QJsonObject{{"type","array"},{"items", QJsonObject{{"type","integer"}}},{"description","Bajty danych (0-255 każdy)"}}},
            {"extended", QJsonObject{{"type","boolean"},{"description","Ramka rozszerzona (29-bit ID)"}}}
        }, {"id"})));

    arr.append(makeTool("start_sniffing", "Uruchamia sniffing CAN na wybranym interfejsie (jeśli nie jest już aktywny).",
        makeSchema({})));
    arr.append(makeTool("stop_sniffing", "Zatrzymuje sniffing CAN (jeśli aktywny).",
        makeSchema({})));

    arr.append(makeTool("load_dbc_file", "Wczytuje plik DBC (Vector) i podmienia bieżącą bazę sygnałów używaną do dekodowania.",
        makeSchema({{"path", QJsonObject{{"type","string"},{"description","Ścieżka do pliku .dbc"}}}}, {"path"})));

    arr.append(makeTool("load_arxml_file", "Wczytuje plik ARXML (AUTOSAR 4.x) i podmienia bieżącą bazę sygnałów używaną do dekodowania.",
        makeSchema({{"path", QJsonObject{{"type","string"},{"description","Ścieżka do pliku .arxml"}}}}, {"path"})));

    arr.append(makeTool("list_dbc_messages", "Zwraca listę wiadomości (ID, nazwa, DLC, sygnały) z aktualnie wczytanej bazy DBC/ARXML.",
        makeSchema({})));

    arr.append(makeTool("decode_signals", "Dekoduje wartości fizyczne sygnałów DBC dla podanego ID i bajtów danych.",
        makeSchema({
            {"id",   QJsonObject{{"type","string"},{"description","ID ramki (dziesiętnie lub hex)"}}},
            {"data", QJsonObject{{"type","array"},{"items", QJsonObject{{"type","integer"}}},{"description","Bajty danych ramki"}}}
        }, {"id","data"})));

    arr.append(makeTool("run_lua_snippet", "Uruchamia fragment kodu Lua w silniku skryptowym aplikacji (API: sendFrame, log, getTick) i zwraca log/błąd.",
        makeSchema({{"code", QJsonObject{{"type","string"},{"description","Kod źródłowy Lua do wykonania"}}}}, {"code"})));

    arr.append(makeTool("list_sim_nodes", "Zwraca listę zdefiniowanych węzłów symulatora CAN (CanNodeSimulator) wraz ze statystykami dopasowań.",
        makeSchema({})));

    arr.append(makeTool("set_sim_node_enabled", "Włącza lub wyłącza symulowany węzeł CAN po nazwie.",
        makeSchema({
            {"name",    QJsonObject{{"type","string"}}},
            {"enabled", QJsonObject{{"type","boolean"}}}
        }, {"name","enabled"})));

    arr.append(makeTool("load_sim_config", "Wczytuje konfigurację węzłów symulatora CAN z pliku JSON.",
        makeSchema({{"path", QJsonObject{{"type","string"}}}}, {"path"})));

    arr.append(makeTool("load_replay_file", "Wczytuje nagranie .mcan/.mcan.zst do odtwarzacza (CanPlayer).",
        makeSchema({{"path", QJsonObject{{"type","string"}}}}, {"path"})));

    arr.append(makeTool("replay_control", "Steruje odtwarzaniem nagrania: play, pause, stop lub seek (z 'index'). Zwraca też bieżący status odtwarzania.",
        makeSchema({
            {"action", QJsonObject{{"type","string"},{"enum", QJsonArray{"play","pause","stop","seek","status"}}}},
            {"index",  QJsonObject{{"type","integer"},{"description","Docelowy indeks ramki (tylko dla action=seek)"}}}
        }, {"action"})));

    return arr;
}

// ── Dispatch narzędzi ─────────────────────────────────────────────────────────

void McpServer::callTool(const QString &name, const QJsonObject &args, QJsonArray &contentOut, bool &isError) {
    isError = false;
    QJsonObject result;

    if      (name == "get_status")            result = toolGetStatus();
    else if (name == "get_frames")            result = toolGetFrames(args);
    else if (name == "get_ids")               result = toolGetIds();
    else if (name == "get_alerts")            result = toolGetAlerts();
    else if (name == "send_frame")            result = toolSendFrame(args);
    else if (name == "start_sniffing")        result = toolStartSniffing();
    else if (name == "stop_sniffing")         result = toolStopSniffing();
    else if (name == "load_dbc_file")         result = toolLoadDbcFile(args);
    else if (name == "load_arxml_file")       result = toolLoadArxmlFile(args);
    else if (name == "list_dbc_messages")     result = toolListDbcMessages();
    else if (name == "decode_signals")        result = toolDecodeSignals(args);
    else if (name == "run_lua_snippet")       result = toolRunLuaSnippet(args);
    else if (name == "list_sim_nodes")        result = toolListSimNodes();
    else if (name == "set_sim_node_enabled")  result = toolSetSimNodeEnabled(args);
    else if (name == "load_sim_config")       result = toolLoadSimConfig(args);
    else if (name == "load_replay_file")      result = toolLoadReplayFile(args);
    else if (name == "replay_control")        result = toolReplayControl(args);
    else {
        isError = true;
        result["error"] = QString("nieznane narzędzie: %1").arg(name);
    }

    if (result.contains("error")) isError = true;

    QJsonObject item;
    item["type"] = "text";
    item["text"] = QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    contentOut.append(item);
}

namespace {

// Pomocnicze: parsuje ID podane jako string dziesiętny/hex albo liczbę.
uint32_t parseIdValue(const QJsonValue &v, bool *ok = nullptr) {
    if (ok) *ok = true;
    if (v.isString()) return v.toString().toUInt(ok, 0);
    if (v.isDouble()) return static_cast<uint32_t>(v.toInt());
    if (ok) *ok = false;
    return 0;
}

} // namespace

QJsonObject McpServer::toolGetStatus() const {
    QJsonObject o;
    o["sniffingActive"] = sniffingActive;
    o["frameCount"]  = m_model ? m_model->rowCount() : 0;
    o["fps"]         = fps;
    o["uniqueIds"]   = uniqueIds;
    o["busLoadPct"]  = busLoadPct;
    o["alertCount"]  = m_alertEngine ? m_alertEngine->totalAlerts() : 0;
    return o;
}

QJsonObject McpServer::toolGetFrames(const QJsonObject &args) const {
    int limit = std::clamp(args.value("limit").toInt(50), 1, 2000);

    bool filterById = false;
    uint32_t filterId = 0;
    if (args.contains("id")) {
        filterId = parseIdValue(args.value("id"), &filterById);
    }

    QJsonArray arr;
    if (m_model) {
        int total = m_model->rowCount();
        int added = 0;
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

            QJsonArray dataArr;
            for (int b = 0; b < frame.dlc && b < 64; ++b)
                dataArr.append(frame.data[b]);
            f["dataBytes"] = dataArr;

            arr.append(f);
            ++added;
        }
    }
    QJsonObject o;
    o["frames"] = arr;
    o["count"]  = arr.size();
    return o;
}

QJsonObject McpServer::toolGetIds() const {
    QHash<uint32_t, int> freq;
    if (m_model) {
        int total = m_model->rowCount();
        for (int i = 0; i < total; ++i)
            freq[m_model->frameAt(i).id]++;
    }
    QVector<QPair<int, uint32_t>> sorted;
    sorted.reserve(freq.size());
    for (auto it = freq.begin(); it != freq.end(); ++it)
        sorted.append({it.value(), it.key()});
    std::sort(sorted.begin(), sorted.end(),
              [](const auto &a, const auto &b) { return a.first > b.first; });

    QJsonArray arr;
    for (const auto &[count, id] : sorted) {
        QJsonObject o;
        o["id"]    = QString("0x%1").arg(id, 0, 16);
        o["idDec"] = static_cast<qint64>(id);
        o["count"] = count;
        arr.append(o);
    }
    QJsonObject o;
    o["ids"] = arr;
    return o;
}

QJsonObject McpServer::toolGetAlerts() const {
    QJsonArray arr;
    {
        std::lock_guard<std::mutex> lock(m_alertMutex);
        for (auto it = m_recentAlerts.rbegin(); it != m_recentAlerts.rend(); ++it)
            arr.append(*it);
    }
    QJsonObject o;
    o["alerts"] = arr;
    return o;
}

QJsonObject McpServer::toolSendFrame(const QJsonObject &args) {
    QJsonObject o;
    if (!args.contains("id")) { o["error"] = "brak wymaganego pola 'id'"; return o; }

    CanFrame frame{};
    bool ok = true;
    frame.id = parseIdValue(args.value("id"), &ok);
    if (!ok) { o["error"] = "nieprawidłowy format 'id'"; return o; }

    frame.dlc = static_cast<uint8_t>(std::min(args.value("dlc").toInt(8), 64));
    frame.extended = args.value("extended").toBool(frame.id > 0x7FF);

    QJsonArray dataArr = args.value("data").toArray();
    for (int i = 0; i < dataArr.size() && i < 64; ++i)
        frame.data[i] = static_cast<uint8_t>(dataArr[i].toInt());
    if (frame.dlc == 8 && dataArr.isEmpty()) frame.dlc = 0;

    if (!sniffingActive) {
        o["error"] = "sniffing nieaktywny — brak otwartego sterownika CAN (użyj start_sniffing)";
        return o;
    }
    emit sendFrameRequested(frame);

    o["status"] = "queued";
    o["id"]     = QString("0x%1").arg(frame.id, 0, 16);
    o["dlc"]    = frame.dlc;
    return o;
}

QJsonObject McpServer::toolStartSniffing() {
    emit startRequested();
    return QJsonObject{{"status", "ok"}};
}

QJsonObject McpServer::toolStopSniffing() {
    emit stopRequested();
    return QJsonObject{{"status", "ok"}};
}

namespace {

QJsonObject dbcMessagesToJson(const QVector<DbcMessage> &messages) {
    QJsonArray arr;
    for (const DbcMessage &msg : messages) {
        QJsonObject m;
        m["id"]   = QString("0x%1").arg(msg.id, 0, 16);
        m["name"] = msg.name;
        m["dlc"]  = msg.dlc;
        QJsonArray sigs;
        for (const DbcSignal &s : msg.sigList) {
            QJsonObject so;
            so["name"]      = s.name;
            so["startBit"]  = s.startBit;
            so["length"]    = s.length;
            so["scale"]     = s.scale;
            so["offset"]    = s.offset;
            so["unit"]      = s.unit;
            sigs.append(so);
        }
        m["signals"] = sigs;
        arr.append(m);
    }
    QJsonObject o;
    o["messages"] = arr;
    o["count"]    = arr.size();
    return o;
}

} // namespace

QJsonObject McpServer::toolLoadDbcFile(const QJsonObject &args) const {
    QJsonObject o;
    if (!m_dbcParser) { o["error"] = "DbcParser niedostępny"; return o; }
    QString path = args.value("path").toString();
    if (path.isEmpty()) { o["error"] = "brak wymaganego pola 'path'"; return o; }

    if (!m_dbcParser->load(path)) { o["error"] = QString("nie udało się wczytać pliku DBC: %1").arg(path); return o; }
    return dbcMessagesToJson(m_dbcParser->messages());
}

QJsonObject McpServer::toolLoadArxmlFile(const QJsonObject &args) const {
    QJsonObject o;
    QString path = args.value("path").toString();
    if (path.isEmpty()) { o["error"] = "brak wymaganego pola 'path'"; return o; }

    ArxmlParser parser;
    QVector<DbcMessage> messages = parser.load(path);
    if (messages.isEmpty() && !parser.lastError().isEmpty()) {
        o["error"] = parser.lastError();
        return o;
    }
    if (m_dbcParser) m_dbcParser->setMessages(messages);
    return dbcMessagesToJson(messages);
}

QJsonObject McpServer::toolListDbcMessages() const {
    if (!m_dbcParser) return QJsonObject{{"error", "DbcParser niedostępny"}};
    return dbcMessagesToJson(m_dbcParser->messages());
}

QJsonObject McpServer::toolDecodeSignals(const QJsonObject &args) const {
    QJsonObject o;
    if (!m_dbcParser) { o["error"] = "DbcParser niedostępny"; return o; }
    if (!args.contains("id") || !args.contains("data")) {
        o["error"] = "wymagane pola: 'id', 'data'";
        return o;
    }
    bool ok = true;
    uint32_t id = parseIdValue(args.value("id"), &ok);
    if (!ok) { o["error"] = "nieprawidłowy format 'id'"; return o; }

    QJsonArray dataArr = args.value("data").toArray();
    uint8_t data[64] = {};
    int dlc = std::min(static_cast<int>(dataArr.size()), 64);
    for (int i = 0; i < dlc; ++i) data[i] = static_cast<uint8_t>(dataArr[i].toInt());

    QHash<QString, double> decoded = m_dbcParser->decodeSignals(id, data, dlc);
    QJsonObject signalsObj;
    for (auto it = decoded.begin(); it != decoded.end(); ++it)
        signalsObj[it.key()] = it.value();

    o["id"]      = QString("0x%1").arg(id, 0, 16);
    o["signals"] = signalsObj;
    return o;
}

QJsonObject McpServer::toolRunLuaSnippet(const QJsonObject &args) const {
    QJsonObject o;
    if (!m_lua) { o["error"] = "LuaScriptEngine niedostępny"; return o; }
    QString code = args.value("code").toString();
    if (code.isEmpty()) { o["error"] = "brak wymaganego pola 'code'"; return o; }

    QStringList logLines;
    QString errorText;
    QMetaObject::Connection logConn = connect(m_lua, &LuaScriptEngine::logMessage,
        [&logLines](const QString &msg) { logLines.append(msg); });
    QMetaObject::Connection errConn = connect(m_lua, &LuaScriptEngine::errorOccurred,
        [&errorText](const QString &err) { errorText = err; });

    bool ok = m_lua->loadScriptFromString(code, "<mcp>");

    disconnect(logConn);
    disconnect(errConn);

    o["success"] = ok && errorText.isEmpty();
    o["log"]     = QJsonArray::fromStringList(logLines);
    if (!errorText.isEmpty()) o["error"] = errorText;
    return o;
}

namespace {

QJsonObject simNodeToJson(const CanNodeDefinition &n) {
    QJsonObject o;
    o["name"]          = n.name;
    o["triggerId"]     = QString("0x%1").arg(n.triggerId, 0, 16);
    o["responseId"]    = QString("0x%1").arg(n.responseId, 0, 16);
    o["enabled"]       = n.enabled;
    o["matchCount"]    = static_cast<qint64>(n.matchCount);
    o["responseCount"] = static_cast<qint64>(n.responseCount);
    return o;
}

} // namespace

QJsonObject McpServer::toolListSimNodes() const {
    if (!m_sim) return QJsonObject{{"error", "CanNodeSimulator niedostępny"}};
    QJsonArray arr;
    for (const CanNodeDefinition &n : m_sim->nodes())
        arr.append(simNodeToJson(n));
    QJsonObject o;
    o["nodes"] = arr;
    return o;
}

QJsonObject McpServer::toolSetSimNodeEnabled(const QJsonObject &args) const {
    QJsonObject o;
    if (!m_sim) { o["error"] = "CanNodeSimulator niedostępny"; return o; }
    QString name = args.value("name").toString();
    if (name.isEmpty()) { o["error"] = "brak wymaganego pola 'name'"; return o; }
    bool enabled = args.value("enabled").toBool();

    for (CanNodeDefinition &n : m_sim->nodes()) {
        if (n.name == name) {
            n.enabled = enabled;
            o["status"] = "ok";
            o["name"]    = name;
            o["enabled"] = enabled;
            return o;
        }
    }
    o["error"] = QString("nie znaleziono węzła o nazwie: %1").arg(name);
    return o;
}

QJsonObject McpServer::toolLoadSimConfig(const QJsonObject &args) const {
    QJsonObject o;
    if (!m_sim) { o["error"] = "CanNodeSimulator niedostępny"; return o; }
    QString path = args.value("path").toString();
    if (path.isEmpty()) { o["error"] = "brak wymaganego pola 'path'"; return o; }
    if (!m_sim->loadConfig(path)) { o["error"] = QString("nie udało się wczytać konfiguracji: %1").arg(path); return o; }
    o["status"]    = "ok";
    o["nodeCount"] = m_sim->nodes().size();
    return o;
}

QJsonObject McpServer::toolLoadReplayFile(const QJsonObject &args) const {
    QJsonObject o;
    if (!m_player) { o["error"] = "CanPlayer niedostępny"; return o; }
    QString path = args.value("path").toString();
    if (path.isEmpty()) { o["error"] = "brak wymaganego pola 'path'"; return o; }

    int count = m_player->loadFile(path);
    if (count <= 0) { o["error"] = QString("nie udało się wczytać nagrania: %1").arg(path); return o; }
    o["status"]     = "ok";
    o["frameCount"] = count;
    return o;
}

QJsonObject McpServer::toolReplayControl(const QJsonObject &args) const {
    QJsonObject o;
    if (!m_player) { o["error"] = "CanPlayer niedostępny"; return o; }
    QString action = args.value("action").toString();

    if      (action == "play")  m_player->play();
    else if (action == "pause") m_player->pause();
    else if (action == "stop")  m_player->stop();
    else if (action == "seek") {
        if (!args.contains("index")) { o["error"] = "action=seek wymaga pola 'index'"; return o; }
        m_player->seekTo(args.value("index").toInt());
    } else if (action != "status") {
        o["error"] = QString("nieznana akcja: %1 (dozwolone: play, pause, stop, seek, status)").arg(action);
        return o;
    }

    o["isPlaying"]    = m_player->isPlaying();
    o["isPaused"]     = m_player->isPaused();
    o["currentFrame"] = m_player->currentFrame();
    o["totalFrames"]  = m_player->totalFrames();
    return o;
}

// ── Odpowiedzi JSON-RPC / HTTP ────────────────────────────────────────────────

void McpServer::sendJsonRpcResult(QTcpSocket *sock, const QJsonValue &id, const QJsonValue &result) {
    QJsonObject rpc;
    rpc["jsonrpc"] = "2.0";
    rpc["id"]      = id;
    rpc["result"]  = result;
    sendHttp(sock, 200, "OK", QJsonDocument(rpc).toJson(QJsonDocument::Compact));
}

void McpServer::sendJsonRpcError(QTcpSocket *sock, const QJsonValue &id, int code, const QString &message) {
    QJsonObject error;
    error["code"]    = code;
    error["message"] = message;

    QJsonObject rpc;
    rpc["jsonrpc"] = "2.0";
    rpc["id"]      = id.isUndefined() ? QJsonValue() : id;
    rpc["error"]   = error;

    int httpCode = (code == -32700) ? 400 : 200;
    sendHttp(sock, httpCode, httpCode == 400 ? "Bad Request" : "OK",
             QJsonDocument(rpc).toJson(QJsonDocument::Compact));
}

void McpServer::sendAccepted(QTcpSocket *sock) {
    sendHttp(sock, 202, "Accepted", QByteArray());
}

void McpServer::sendHttp(QTcpSocket *sock, int code, const char *statusText,
                          const QByteArray &body, const char *contentType) {
    QString resp = QString("HTTP/1.1 %1 %2\r\n"
                            "Content-Type: %3; charset=utf-8\r\n"
                            "Content-Length: %4\r\n"
                            "\r\n")
                       .arg(code).arg(statusText).arg(contentType).arg(body.size());
    sock->write(resp.toUtf8());
    if (!body.isEmpty()) sock->write(body);
    sock->flush();
    sock->close();
}
