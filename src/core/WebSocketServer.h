#pragma once
#include <QObject>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QSslConfiguration>
#include <QList>
#include <QTimer>
#include <QDir>
#include "CanFrame.h"

/**
 * @brief Serwer WebSocket streamujący ramki CAN z opcjonalnym WSS (TLS)
 *        i autoryzacją przez token.
 *
 * Tryby:
 *  - NonSecure (ws://)  – bez szyfrowania i autoryzacji
 *  - Secure (wss://)     – TLS 1.3 + wymagany token
 *
 * Token (256-bit hex) jest generowany przy pierwszym uruchomieniu
 * i przechowywany w ~/.magistrala_can4/token.
 *
 * Autoryzacja: klient po połączeniu wysyła wiadomość:
 *   {"type":"auth","token":"<token>"}
 * Serwer weryfikuje w ciągu 5 s – brak poprawnego tokena → rozłączenie.
 */
class WebSocketServer : public QObject {
    Q_OBJECT
public:
    explicit WebSocketServer(QObject *parent = nullptr);
    ~WebSocketServer() override;

    /// Czy serwer nasłuchuje?
    [[nodiscard]] bool isRunning() const { return m_running; }
    [[nodiscard]] int  clientCount() const { return m_authenticatedClients.size(); }
    [[nodiscard]] bool isSecure() const   { return m_secureMode; }

    /// Token serwera (generowany automatycznie).
    [[nodiscard]] QString token() const;

    /// Ścieżki do certyfikatu i klucza TLS.
    [[nodiscard]] QString certPath() const;
    [[nodiscard]] QString keyPath() const;

    /// Gwarantuje istnienie certyfikatu i klucza (generuje jeśli brak).
    bool ensureCertificate();

    /// Generuje nowy token.
    QString regenerateToken();

public slots:
    /// Uruchamia serwer w trybie plaintext (bez TLS).
    void start(quint16 port = 9000);

    /// Uruchamia serwer w trybie WSS (TLS + token).
    void startSecure(quint16 port = 9001);

    /// Zatrzymuje serwer.
    void stop();

    /// Rozsyła partię ramek CAN jako tablicę JSON (batch — jedno wywołanie zamiast N).
    void broadcastFrameBatch(const QVector<CanFrame> &frames);

signals:
    void statusChanged(bool running);
    void clientCountChanged(int count);
    void errorOccurred(const QString &msg);
    /// Emitted when an authenticated client sends a CAN frame to the server.
    void frameReceivedFromClient(const CanFrame &frame);

private slots:
    void onNewConnection();
    void onClientDisconnected();
    void onTextMessageReceived(const QString &message);
    void checkAuthTimeout();

private:
    void setupDirs();
    QString loadOrGenerateToken();
    static CanFrame parseIncomingFrame(const QJsonObject &obj);

    QWebSocketServer *m_server = nullptr;
    QList<QWebSocket *> m_authenticatedClients;
    QHash<QWebSocket *, QTimer *> m_pendingAuthClients;
    bool m_running = false;
    bool m_secureMode = false;
    QString m_token;
    QString m_configDir;
    QString m_certPath;
    QString m_keyPath;

    static constexpr int AUTH_TIMEOUT_MS = 5000;
};
