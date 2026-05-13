#pragma once
#include "CanFrame.h"
#include "CanAlertEngine.h"
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <deque>
#include <mutex>

class CanFrameModel;

/**
 * REST API server for MagistralaCAN4.
 *
 * Endpoints:
 *   GET  /api/status          → {"running":bool, "frames":int, "fps":double, "uniqueIds":int}
 *   GET  /api/stats           → {"fps":double, "uniqueIds":int, "totalFrames":int, "busLoadPct":double}
 *   GET  /api/frames          → [{id,dlc,data,timestamp}, ...] (default limit=100)
 *   GET  /api/frames?limit=N  → last N frames
 *   GET  /api/frames?id=0x1A0 → frames filtered by CAN ID
 *   GET  /api/ids             → [{"id":hex,"count":int}, ...] sorted by count desc
 *   GET  /api/alerts          → last 100 alerts [{ruleName,description,id,timestamp}, ...]
 *   POST /api/send            → body: {"id":hex,"dlc":int,"data":[b0..b7]}
 *   POST /api/start           → starts sniffing
 *   POST /api/stop            → stops sniffing
 *   OPTIONS *                 → CORS preflight
 */
class HttpRestServer : public QObject {
    Q_OBJECT
public:
    explicit HttpRestServer(QObject *parent = nullptr);
    ~HttpRestServer() override;

    void setModel(CanFrameModel *model) { m_model = model; }
    void setAlertEngine(CanAlertEngine *engine);

    bool start(quint16 port = 8080);
    void stop();
    bool isRunning() const { return m_running; }

    // Stats set externally from CanStatsPanel
    double fps        = 0.0;
    int    uniqueIds  = 0;
    double busLoadPct = 0.0;

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
        QString path;       // without query string
        QString query;      // raw query string (after '?')
        QByteArray body;
    };

    ParsedRequest parseRawRequest(const QByteArray &raw) const;
    QString queryParam(const QString &query, const QString &key) const;

    void handleRequest(QTcpSocket *sock, const ParsedRequest &req);
    void sendJson(QTcpSocket *sock, int code, const QJsonDocument &doc);
    void sendError(QTcpSocket *sock, int code, const QString &msg);
    void sendCors(QTcpSocket *sock);

    QTcpServer     *m_server      = nullptr;
    CanFrameModel  *m_model       = nullptr;
    CanAlertEngine *m_alertEngine = nullptr;
    bool            m_running     = false;

    mutable std::mutex       m_alertMutex;
    std::deque<QJsonObject>  m_recentAlerts;   // ring buffer, max 100
    static constexpr int     kMaxAlerts = 100;
};
