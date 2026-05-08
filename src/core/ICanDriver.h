#pragma once
#include <cstdint>
#include <QString>
#include <QStringList>
#include "CanFrame.h"

/**
 * @brief Abstrakcyjny interfejs sterownika CAN.
 *
 * Pozwala MagistralaCAN4 działać na różnych platformach:
 *   - Linux:    SocketCanDriver  (PF_CAN + SocketCAN)
 *   - Windows:  PcanDriver       (PCANBasic.dll)
 *
 * Każdy backend implementuje open/close/read/write + listę dostępnych urządzeń.
 */
class ICanDriver {
public:
    virtual ~ICanDriver() = default;

    /// Otwiera urządzenie CAN. Zwraca true przy sukcesie.
    virtual bool open(const QString &device) = 0;

    /// Zamyka urządzenie.
    virtual void close() = 0;

    /// Odczytuje jedną ramkę CAN. Blokuje do czasu nadejścia danych.
    /// Zwraca pustą ramkę (id=0, dlc=0) gdy zamknięty.
    virtual CanFrame readFrame() = 0;

    /// Sprawdza czy sterownik jest otwarty i sprawny.
    virtual bool isValid() const = 0;

    /// Wysyła ramkę CAN na magistralę.
    virtual void writeFrame(const CanFrame &frame) = 0;

    /// Zwraca listę dostępnych nazw urządzeń CAN.
    virtual QStringList availableDevices() const = 0;

    /// Czytelna nazwa backendu (np. "SocketCAN", "PCAN").
    virtual QString backendName() const = 0;
};
