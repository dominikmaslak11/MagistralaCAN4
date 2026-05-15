#pragma once
#include "ICanDriver.h"
#include <QByteArray>
#include <QSerialPort>
#include <QStringList>

/**
 * @brief Backend GVRET — protokół binarny EVTV (SavvyCAN "Serial Connection").
 *
 * Protokół: magic 0xF1 + komenda 1B + payload zależny od komendy.
 *   Odbiór ramki (ESP→PC): F1 00 [ts:4LE] [id:4LE, bit31=ext] [(bus<<4)|dlc] [data:dlc]
 *   Wysyłanie (PC→ESP):    F1 01 [id:4LE, bit31=ext] [(0<<4)|dlc]             [data:dlc]
 *
 * Auto-detekcja: wysyła {F1 08}, sprawdza czy odpowiedź zawiera {F1 08}.
 */
class GvretDriver : public ICanDriver {
public:
    GvretDriver();
    ~GvretDriver() override;

    bool open(const QString &device) override;
    void close() override;
    CanFrame readFrame() override;
    bool isValid() const override;
    void writeFrame(const CanFrame &frame) override;
    void setBaudRate(const QString &baudStr) override;
    QStringList availableDevices() const override;
    QString backendName() const override { return QStringLiteral("GVRET"); }

    static QStringList detectDevices(int timeoutMs = 400);

private:
    void sendSetupBus();
    CanFrame tryParseFrame();

    QSerialPort *m_port    = nullptr;
    QByteArray   m_rxBuffer;
    uint32_t     m_canBps  = 250000;   // CAN speed in bps (default 250K)
};
