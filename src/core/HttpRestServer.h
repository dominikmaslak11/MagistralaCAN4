#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>

class CanFrameModel;

/**
 * @brief Minimalny serwer REST API dla MagistralaCAN4.
 *
 * Endpointy:
 *   GET /api/status → {"running":bool, "frames":int, "fps":double}
 *   GET /api/frames  → [{"id":..., "dlc":..., "data":"..."}, ...]
 *   POST /api/start  → uruchamia sniffing
 *   POST /api/stop   → zatrzymuje sniffing
 */
class HttpRestServer : public QObject {
    Q_OBJECT
public:
    explicit HttpRestServer(QObject *parent = nullptr);
    ~HttpRestServer() override;

    void setModel(CanFrameModel *model) { m_model = model; }

    bool start(quint16 port = 8080);
    void stop();
    bool isRunning() const { return m_running; }

    // Statystyki (ustawiane z zewnątrz)
    double fps = 0;
    int uniqueIds = 0;

signals:
    void startRequested();
    void stopRequested();

private slots:
    void onNewConnection();
    void onReadyRead();

private:
    void handleRequest(QTcpSocket *sock, const QString &method, const QString &path, const QByteArray &body);
    void sendJson(QTcpSocket *sock, int code, const QJsonDocument &doc);
    void sendText(QTcpSocket *sock, int code, const QString &text);

    QTcpServer *m_server = nullptr;
    CanFrameModel *m_model = nullptr;
    bool m_running = false;
};
