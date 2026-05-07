#pragma once
#include <QWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QSpinBox>
#include <QCheckBox>
#include <QTimer>
#include "WebSocketServer.h"
#include "RemoteCanClient.h"

class CanSniffer;

/**
 * @brief Zakładka "Zdalny CAN" – łączy serwer WSS i klienta zdalnego.
 *
 * Panel serwera: start/stop WSS, wyświetla token, port.
 * Panel klienta: URL serwera, token, Połącz/Rozłącz.
 * Odebrane ramki są wstrzykiwane do CanSniffer → CanFrameModel pipeline.
 */
class RemoteCanWidget : public QWidget {
    Q_OBJECT
public:
    explicit RemoteCanWidget(CanSniffer *sniffer, QWidget *parent = nullptr);

    WebSocketServer *server() { return &m_server; }
    RemoteCanClient *client() { return &m_client; }

private slots:
    void toggleServer();
    void toggleClient();
    void updateServerStatus(bool running);
    void updateClientStatus(bool connected, const QString &info);
    void onRemoteFrame(const CanFrame &frame);

private:
    void setupUi();

    // Serwer
    WebSocketServer m_server;
    QPushButton *m_serverBtn;
    QSpinBox    *m_serverPort;
    QLabel      *m_tokenLabel;
    QLabel      *m_serverStatus;
    QPushButton *m_copyTokenBtn;

    // Klient
    RemoteCanClient m_client;
    QLineEdit   *m_clientUrl;
    QLineEdit   *m_clientToken;
    QPushButton *m_clientBtn;
    QLabel      *m_clientStatus;
    QLabel      *m_frameCountLabel;

    CanSniffer  *m_sniffer;
    QTimer       m_statsTimer;
};
