#pragma once
#include "ICanDriver.h"
#include <QByteArray>
#include <QSerialPort>
#include <QStringList>

/**
 * @brief Backend ESP32+MCP2515 – linia ASCII przez USB-Serial (115200 baud).
 *
 * Protokół wejściowy (ESP → PC):
 *   RX <EXT|STD> <ID_HEX> <DLC> [B0 B1 … BN]
 *
 * Protokół wyjściowy (PC → ESP):
 *   TX <EXT|STD> <ID_HEX> <DLC> [B0 B1 … BN]
 *   BAUD <kbps>
 *
 * Auto-detekcja: wysyła "STATUS\n", szuka odpowiedzi zawierającej "relays".
 */
class EspMcpDriver : public ICanDriver {
public:
    EspMcpDriver();
    ~EspMcpDriver() override;

    bool open(const QString &device) override;
    void close() override;
    CanFrame readFrame() override;
    bool isValid() const override;
    void writeFrame(const CanFrame &frame) override;
    QStringList availableDevices() const override;
    QString backendName() const override { return QStringLiteral("ESP-MCP2515"); }

    /// Skanuje porty szeregowe w poszukiwaniu urządzeń ESP+MCP2515.
    static QStringList detectDevices(int timeoutMs = 300);

private:
    CanFrame parseLine(const QByteArray &line) const;

    QSerialPort *m_port = nullptr;
    QByteArray   m_rxBuffer;
};
