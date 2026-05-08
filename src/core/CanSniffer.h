#pragma once
#include <QObject>
#include <QThread>
#include <QString>
#include <atomic>
#include "CanFrame.h"

class ICanDriver;

class CanSniffer : public QObject {
    Q_OBJECT
public:
    explicit CanSniffer(QObject *parent = nullptr);
    ~CanSniffer() override;

    /// Ustawia sterownik CAN (SocketCanDriver, PcanDriver, ...).
    /// Musi być wywołane przed start().
    void setDriver(ICanDriver *driver) { m_driver = driver; }

    /// Czy sterownik jest podłączony do magistrali.
    bool isSocketValid() const;

    /// Wysyła ramkę CAN przez sterownik.
    void writeFrame(const CanFrame &frame);

signals:
    void newFrame(const CanFrame &frame);
    void statusChanged(bool running);
    void errorOccurred(const QString &msg);

public slots:
    void start(const QString &interface);
    void stop();

private:
    void doWork();

    ICanDriver *m_driver = nullptr;
    std::atomic<bool> m_running{false};
    QString m_interface;
};
