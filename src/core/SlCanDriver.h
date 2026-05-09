#pragma once
#include "ICanDriver.h"
#include <QStringList>
#include <QSerialPort>

/**
 * @brief Backend SLCAN – protokół szeregowy Lawicel/Canable.
 *
 * Obsługuje adaptery USB-CAN komunikujące się przez protokół SLCAN
 * (np. Canable, USBtin, Lawicel CAN232/CANUSB, Arduino CAN).
 *
 * Komendy:
 *   O/C     – otwarcie/zamknięcie kanału
 *   t/T     – ramka standard/extended (TX)
 *   r/R     – ramka standard/extended (RX)
 *   S0..S8  – prędkość CAN (0=10k … 8=1M)
 *   V       – wersja firmware
 *   N       – numer seryjny
 *
 * Auto-detekcja: przeszukuje dostępne porty szeregowe, wysyła 'V\r',
 * sprawdza odpowiedź (numer wersji SLCAN).
 */
class SlCanDriver : public ICanDriver {
public:
    SlCanDriver();
    ~SlCanDriver() override;

    bool open(const QString &device) override;
    void close() override;
    CanFrame readFrame() override;
    bool isValid() const override;
    void writeFrame(const CanFrame &frame) override;
    QStringList availableDevices() const override;
    QString backendName() const override { return QStringLiteral("SLCAN"); }

    /// Auto-detekcja: skanuje porty szeregowe i zwraca listę znalezionych urządzeń SLCAN.
    /// Każda nazwa ma format "COM3 [Canable v1.0]" – część po spacji jest ignorowana przy open().
    static QStringList detectDevices(int timeoutMs = 200);

    /// Ustawia prędkość transmisji UART (domyślnie 921600).
    void setBaudRate(qint32 baud) { m_baudRate = baud; }

    /// Interfejs ICanDriver: akceptuje etykiety "1M", "500K" itp.
    void setBaudRate(const QString &baudStr) override;

private:
    CanFrame parseIncoming(const QByteArray &line) const;
    QString sendCommand(const QString &cmd, int waitMs = 100);

    QSerialPort *m_port = nullptr;
    qint32       m_baudRate = 921600;  // domyślnie jak Canable / candleLight
    QByteArray   m_rxBuffer;
};
