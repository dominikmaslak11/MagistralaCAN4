#pragma once
#include <QObject>
#include <QThread>
#include <QString>
#include <linux/can.h>
#include <linux/can/raw.h>
#include "CanFrame.h"

class CanSniffer : public QObject {
    Q_OBJECT
public:
    explicit CanSniffer(QObject *parent = nullptr);
    ~CanSniffer() override;

signals:
    void newFrame(const CanFrame &frame);
    void statusChanged(bool running);
    void errorOccurred(const QString &msg);

public slots:
    void start(const QString &interface);
    void stop();

private slots:
    void doWork();               // uruchamiana w wątku roboczym

private:
    bool openSocket(const QString &ifname);
    void closeSocket();
    CanFrame parseFrame(const struct can_frame &rawFrame, uint64_t timestamp) const;
    uint64_t systemTimestamp() const;

    std::atomic<bool> m_running{false};
    int m_socket{-1};
    QString m_interface;
};
