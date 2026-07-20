#pragma once
#include "CanFrame.h"
#include "CanAlertEngine.h"
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <deque>
#include <mutex>

class CanFrameModel;
class DbcParser;
class LuaScriptEngine;
class CanNodeSimulator;
class CanPlayer;

/**
 * Wbudowany serwer MCP (Model Context Protocol) — transport Streamable HTTP,
 * tryb bezstanowy (jedna odpowiedź JSON na żądanie, bez SSE/sesji).
 *
 * Pozwala asystentom AI (Claude Code, Codex CLI, CodeWhale/DeepSeek TUI)
 * sterować żywą sesją MagistralaCAN4 przez JSON-RPC 2.0 pod jednym
 * endpointem POST /mcp. Zobacz MCP_SERVER.md po pełny opis narzędzi
 * i konfigurację klientów.
 */
class McpServer : public QObject {
    Q_OBJECT
public:
    explicit McpServer(QObject *parent = nullptr);
    ~McpServer() override;

    void setModel(CanFrameModel *model) { m_model = model; }
    void setAlertEngine(CanAlertEngine *engine);
    void setDbcParser(DbcParser *parser) { m_dbcParser = parser; }
    void setLuaEngine(LuaScriptEngine *lua) { m_lua = lua; }
    void setNodeSimulator(CanNodeSimulator *sim) { m_sim = sim; }
    void setPlayer(CanPlayer *player) { m_player = player; }

    bool start(quint16 port = 8790);
    void stop();
    bool isRunning() const { return m_running; }

    // Statystyki ustawiane z zewnątrz (jak w HttpRestServer, z CanStatsPanel)
    double fps        = 0.0;
    int    uniqueIds  = 0;
    double busLoadPct = 0.0;
    // Czy sniffing jest aktywny — ustawiane z MainWindow przy każdej zmianie m_sniffing.
    // send_frame odmawia wysyłki gdy false: bez otwartego sterownika CanSniffer::writeFrame
    // emituje errorOccurred, które w MainWindow otwiera modalny QMessageBox::warning — to
    // zablokowałoby cały wątek GUI (i serwer MCP) do czasu ręcznego kliknięcia OK.
    bool sniffingActive = false;

signals:
    void startRequested();
    void stopRequested();
    void sendFrameRequested(const CanFrame &frame);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onAlertTriggered(const CanAlert &alert);

private:
    struct ParsedRequest {
        QString method;
        QString path;
        QByteArray headerBlock;
        QByteArray body;
    };
    ParsedRequest parseRawRequest(const QByteArray &raw) const;
    QString headerValue(const QByteArray &headerBlock, const QString &name) const;

    void handleRequest(QTcpSocket *sock, const ParsedRequest &req);

    // JSON-RPC dispatch
    void dispatchRpc(QTcpSocket *sock, const QJsonObject &rpc, const QString &protocolVersionHeader);
    QJsonObject handleInitialize(const QJsonObject &params) const;
    QJsonArray  toolDefinitions() const;
    void        callTool(const QString &name, const QJsonObject &args,
                          QJsonArray &contentOut, bool &isError);

    // Poszczególne narzędzia (const jeśli tylko czytają model; niektóre emitują sygnały, więc nie mogą być const)
    QJsonObject toolGetStatus() const;
    QJsonObject toolGetFrames(const QJsonObject &args) const;
    QJsonObject toolGetIds() const;
    QJsonObject toolGetAlerts() const;
    QJsonObject toolSendFrame(const QJsonObject &args);
    QJsonObject toolStartSniffing();
    QJsonObject toolStopSniffing();
    QJsonObject toolLoadDbcFile(const QJsonObject &args) const;
    QJsonObject toolLoadArxmlFile(const QJsonObject &args) const;
    QJsonObject toolListDbcMessages() const;
    QJsonObject toolDecodeSignals(const QJsonObject &args) const;
    QJsonObject toolRunLuaSnippet(const QJsonObject &args) const;
    QJsonObject toolListSimNodes() const;
    QJsonObject toolSetSimNodeEnabled(const QJsonObject &args) const;
    QJsonObject toolLoadSimConfig(const QJsonObject &args) const;
    QJsonObject toolLoadReplayFile(const QJsonObject &args) const;
    QJsonObject toolReplayControl(const QJsonObject &args) const;

    // Odpowiedzi JSON-RPC
    void sendJsonRpcResult(QTcpSocket *sock, const QJsonValue &id, const QJsonValue &result);
    void sendJsonRpcError(QTcpSocket *sock, const QJsonValue &id, int code, const QString &message);
    void sendHttp(QTcpSocket *sock, int code, const char *statusText,
                  const QByteArray &body, const char *contentType = "application/json");
    void sendAccepted(QTcpSocket *sock);

    QTcpServer       *m_server     = nullptr;
    CanFrameModel    *m_model      = nullptr;
    CanAlertEngine   *m_alertEngine = nullptr;
    DbcParser        *m_dbcParser  = nullptr;
    LuaScriptEngine  *m_lua        = nullptr;
    CanNodeSimulator *m_sim        = nullptr;
    CanPlayer        *m_player     = nullptr;
    bool              m_running    = false;

    mutable std::mutex       m_alertMutex;
    std::deque<QJsonObject>  m_recentAlerts;
    static constexpr int     kMaxAlerts = 100;

    static constexpr const char *kProtocolVersion = "2025-06-18";
};
