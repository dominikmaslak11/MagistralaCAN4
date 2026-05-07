#pragma once
#include <QObject>
#include <QWebSocket>
#include <QTimer>
#include "CanFrame.h"

/**
 * @brief Klient zdalnego CAN – łączy się z serwerem WSS,
 *        autoryzuje tokenem i emituje odebrane ramki CAN.
 *
 * Użycie:
 *   client.connectToServer("wss://192.168.1.10:9001", "token");
 *   connect(&client, &RemoteCanClient::newFrame, ...);
 *
 * Ramki są odbierane jako JSON (format MagistralaCAN4) i konwertowane
 * na CanFrame. Emitowany sygnał newFrame można podłączyć bezpośrednio
 * do pipeline'u MainWindow (CanFrameModel, AssociativeLearner, itp.).
 */
class RemoteCanClient : public QObject {
    Q_OBJECT
public:
    explicit RemoteCanClient(QObject *parent = nullptr);
    ~RemoteCanClient() override;

    [[nodiscard]] bool isConnected() const { return m_connected && m_authenticated; }
    [[nodiscard]] uint64_t frameCount() const { return m_frameCount; }

public slots:
    void connectToServer(const QString &url, const QString &token);
    void disconnect();

signals:
    void newFrame(const CanFrame &frame);
    void statusChanged(bool connected, const QString &info);
    void errorOccurred(const QString &msg);

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString &message);
    void onError(QAbstractSocket::SocketError error);
    void onSslErrors(const QList<QSslError> &errors);

private:
    CanFrame parseFrameJson(const QJsonObject &obj) const;

    QWebSocket m_socket;
    QString    m_token;
    QString    m_url;
    bool       m_connected      = false;
    bool       m_authenticated  = false;
    uint64_t   m_frameCount     = 0;
    QTimer     m_reconnectTimer;
    bool       m_intentionalDisconnect = false;
};
