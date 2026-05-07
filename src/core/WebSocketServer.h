#pragma once
#include <QObject>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QList>
#include "CanFrame.h"

/**
 * @brief Serwer WebSocket streamujący ramki CAN do zdalnych klientów.
 *
 * Nasłuchuje na zadanym porcie (domyślnie 9000), akceptuje połączenia
 * i rozsyła każdą ramkę CAN jako kompaktowy JSON do wszystkich podłączonych
 * klientów. Obsługuje automatyczne czyszczenie po rozłączeniu.
 *
 * Format JSON ramki:
 *   {"type":"frame","id":123,"dlc":8,"data":"A1B2...","dataBytes":[...],"timestamp":...}
 */
class WebSocketServer : public QObject {
    Q_OBJECT
public:
    explicit WebSocketServer(QObject *parent = nullptr);
    ~WebSocketServer() override;

    /// Czy serwer aktualnie nasłuchuje?
    [[nodiscard]] bool isRunning() const { return m_running; }

    /// Liczba aktualnie podłączonych klientów.
    [[nodiscard]] int clientCount() const { return m_clients.size(); }

public slots:
    /// Rozpoczyna nasłuchiwanie na zadanym porcie.
    void start(quint16 port = 9000);

    /// Zatrzymuje serwer i rozłącza wszystkich klientów.
    void stop();

    /// Rozsyła ramkę CAN jako JSON do wszystkich klientów.
    /// Bezpieczne dla wątków – wywoływane przez Qt::QueuedConnection.
    void broadcastFrame(const CanFrame &frame);

signals:
    /// Emitowany przy zmianie stanu serwera (start/stop).
    void statusChanged(bool running);

    /// Emitowany przy podłączeniu/rozłączeniu klienta.
    void clientCountChanged(int count);

    /// Emitowany w przypadku błędu (np. port zajęty).
    void errorOccurred(const QString &msg);

private slots:
    void onNewConnection();
    void onClientDisconnected();
    void onTextMessageReceived(const QString &message);

private:
    /// Serializuje CanFrame do kompaktowego JSON.
    [[nodiscard]] QString frameToJson(const CanFrame &frame) const;

    QWebSocketServer *m_server = nullptr;
    QList<QWebSocket *> m_clients;
    bool m_running = false;
};
