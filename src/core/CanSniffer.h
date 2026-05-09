#pragma once
#include <QObject>
#include <QThread>
#include <QString>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>
#include "CanFrame.h"
#include "RingBuffer.h"

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

    /// Drains the ring buffer and emits newFrame for each buffered frame.
    /// Called by the GUI thread periodically.
    void drainAndEmit();

    /// Size of the ring buffer.
    int bufferSize() const { return m_ringBuffer.size(); }

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
    RingBuffer<CanFrame> m_ringBuffer;
    QMutex m_waitMutex;              // used only with m_bufferNotFull
    QWaitCondition m_bufferNotFull;  // wake producer when consumer drains
};
