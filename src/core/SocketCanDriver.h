#pragma once
#include "ICanDriver.h"

class SocketCanDriver : public ICanDriver {
public:
    SocketCanDriver();
    ~SocketCanDriver() override;

    bool open(const QString &device) override;
    void close() override;
    CanFrame readFrame() override;
    bool isValid() const override;
    void writeFrame(const CanFrame &frame) override;
    QStringList availableDevices() const override;
    QString backendName() const override { return QStringLiteral("SocketCAN"); }

private:
    CanFrame parseFrame(const struct can_frame &raw, uint64_t timestamp) const;

    int m_socket = -1;
};
