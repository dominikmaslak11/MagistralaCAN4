#include "CanNodeSimulator.h"
#include "CanSniffer.h"
#include "LuaScriptEngine.h"
#include "Logger.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

// ═══════════════════════════════════════════════════════════════
// CanNodeDefinition – JSON serializacja
// ═══════════════════════════════════════════════════════════════

QJsonObject CanNodeDefinition::toJson() const {
    QJsonObject obj;
    obj["name"]             = name;
    obj["triggerId"]        = static_cast<qint64>(triggerId);
    obj["triggerIdMask"]    = static_cast<qint64>(triggerIdMask);
    QJsonArray tdata, tmask;
    for (auto b : triggerData)     tdata.append(static_cast<int>(b));
    for (auto b : triggerDataMask) tmask.append(static_cast<int>(b));
    obj["triggerData"]      = tdata;
    obj["triggerDataMask"]  = tmask;
    obj["responseId"]       = static_cast<qint64>(responseId);
    QJsonArray resp;
    for (auto b : staticResponse)  resp.append(static_cast<int>(b));
    obj["staticResponse"]   = resp;
    obj["responseLuaFunc"]  = responseLuaFunc;
    obj["responseDelayMs"]  = responseDelayMs;
    obj["enabled"]          = enabled;
    return obj;
}

CanNodeDefinition CanNodeDefinition::fromJson(const QJsonObject &obj) {
    CanNodeDefinition n;
    n.name            = obj["name"].toString();
    n.triggerId       = static_cast<uint32_t>(obj["triggerId"].toVariant().toLongLong());
    n.triggerIdMask   = static_cast<uint32_t>(obj["triggerIdMask"].toVariant().toLongLong());
    n.responseId      = static_cast<uint32_t>(obj["responseId"].toVariant().toLongLong());
    n.responseLuaFunc = obj["responseLuaFunc"].toString();
    n.responseDelayMs = obj["responseDelayMs"].toInt();
    n.enabled         = obj["enabled"].toBool(true);

    for (const auto &v : obj["triggerData"].toArray())
        n.triggerData.append(static_cast<uint8_t>(v.toInt()));
    for (const auto &v : obj["triggerDataMask"].toArray())
        n.triggerDataMask.append(static_cast<uint8_t>(v.toInt()));
    for (const auto &v : obj["staticResponse"].toArray())
        n.staticResponse.append(static_cast<uint8_t>(v.toInt()));

    return n;
}

// ═══════════════════════════════════════════════════════════════
// CanNodeSimulator
// ═══════════════════════════════════════════════════════════════

CanNodeSimulator::CanNodeSimulator(QObject *parent) : QObject(parent) {
    m_responseTimer.setSingleShot(true);
    connect(&m_responseTimer, &QTimer::timeout, this, &CanNodeSimulator::sendPendingResponse);
}

void CanNodeSimulator::onNewFrame(const CanFrame &frame) {
    for (auto &node : m_nodes) {
        if (!node.enabled) continue;
        if (!matchTrigger(node, frame)) continue;

        node.matchCount++;
        scheduleResponse(node);
    }
}

bool CanNodeSimulator::matchTrigger(const CanNodeDefinition &node, const CanFrame &frame) const {
    // Dopasowanie ID z maską
    if ((frame.id & node.triggerIdMask) != (node.triggerId & node.triggerIdMask))
        return false;

    // Dopasowanie danych (jeśli zdefiniowane)
    if (!node.triggerData.isEmpty()) {
        int len = node.triggerData.size();
        if (frame.dlc < len) return false;

        for (int i = 0; i < len; ++i) {
            uint8_t mask = (i < node.triggerDataMask.size()) ? node.triggerDataMask[i] : 0xFF;
            if ((frame.data[i] & mask) != (node.triggerData[i] & mask))
                return false;
        }
    }

    return true;
}

void CanNodeSimulator::scheduleResponse(const CanNodeDefinition &node) {
    QVector<uint8_t> respData;

    // Odpowiedź przez Lua?
    if (!node.responseLuaFunc.isEmpty() && m_lua) {
        // Wywołaj funkcję Lua: responseLuaFunc(triggerId, triggerData, timestamp)
        // Funkcja powinna zwrócić tabelę bajtów lub nil
        // To jest uproszczone – w praktyce Lua wołamy z poziomu LuaScriptEngine
        // Na razie fallback do statycznej odpowiedzi
        respData = node.staticResponse;
    } else {
        respData = node.staticResponse;
    }

    if (respData.isEmpty()) return;

    if (node.responseDelayMs > 0) {
        PendingResponse pending;
        pending.id   = node.responseId;
        pending.data = respData;
        m_pendingResponses.append(pending);
        if (!m_responseTimer.isActive())
            m_responseTimer.start(node.responseDelayMs);
    } else {
        // Wyślij natychmiast
        CanFrame out;
        out.id  = node.responseId;
        out.dlc = static_cast<uint8_t>(respData.size());
        for (int i = 0; i < respData.size() && i < 64; ++i)
            out.data[i] = respData[i];
        emit frameReady(out);
        if (m_sniffer && m_sniffer->isSocketValid()) {
            m_sniffer->writeFrame(out);
            const_cast<CanNodeDefinition&>(node).responseCount++;
        }
    }
}

void CanNodeSimulator::sendPendingResponse() {
    if (m_pendingResponses.isEmpty()) return;
    bool canSend = m_sniffer && m_sniffer->isSocketValid();

    for (const auto &pending : m_pendingResponses) {
        CanFrame out;
        out.id  = pending.id;
        out.dlc = static_cast<uint8_t>(pending.data.size());
        for (int i = 0; i < pending.data.size() && i < 64; ++i)
            out.data[i] = pending.data[i];
        emit frameReady(out);
        if (canSend) m_sniffer->writeFrame(out);
    }

    m_pendingResponses.clear();
}

// ── Konfiguracja (JSON) ──────────────────────────────────────

bool CanNodeSimulator::saveConfig(const QString &path) const {
    QJsonArray arr;
    for (const auto &node : m_nodes)
        arr.append(node.toJson());

    QJsonObject root;
    root["version"] = 1;
    root["nodes"]   = arr;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(root).toJson());
    file.close();
    Logger::log(QString("Symulator: zapisano %1 węzłów do %2").arg(m_nodes.size()).arg(path));
    return true;
}

bool CanNodeSimulator::loadConfig(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) return false;

    QJsonObject root = doc.object();
    QJsonArray arr = root["nodes"].toArray();

    m_nodes.clear();
    for (const auto &val : arr) {
        m_nodes.append(CanNodeDefinition::fromJson(val.toObject()));
    }
    Logger::log(QString("Symulator: wczytano %1 węzłów z %2").arg(m_nodes.size()).arg(path));
    return true;
}
