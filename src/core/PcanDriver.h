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

private:
    struct Impl;
    Impl *d = nullptr; // PIMPL – ukrywa Windows-specific przed kompilacją Linux
};
