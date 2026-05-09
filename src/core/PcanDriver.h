#pragma once
#include "ICanDriver.h"
#include <QStringList>

/**
 * @brief Backend PCAN (Peak System) – dynamiczne ładowanie PCANBasic.dll.
 *
 * Na Linux kompiluje się jako stub (brak PCANBasic).
 */
class PcanDriver : public ICanDriver {
public:
    PcanDriver();
    ~PcanDriver() override;

    bool open(const QString &device) override;
    void close() override;
    CanFrame readFrame() override;
    bool isValid() const override;
    void writeFrame(const CanFrame &frame) override;
    QStringList availableDevices() const override;
    QString backendName() const override { return QStringLiteral("PCAN"); }

    /// Ustawia prędkość CAN (domyślnie 500K).
    /// Akceptuje etykiety: "1M", "800K", "500K", "250K", "125K", "100K", "50K", "20K", "10K".
    void setBaudRate(const QString &baudStr);

private:
    struct Impl;
    Impl *d = nullptr; // PIMPL – ukrywa Windows-specific przed kompilacją Linux
};
