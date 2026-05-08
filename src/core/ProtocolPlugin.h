#pragma once
#include <QWidget>
#include <QString>
#include "CanFrame.h"

/**
 * @brief Interfejs wtyczki analizatora protokołu CAN.
 *
 * Każda wtyczka implementuje ten interfejs i jest ładowana
 * z katalogu plugins/ jako biblioteka współdzielona (.so).
 *
 * MagistralaCAN4 wywołuje processFrame() dla każdej ramki CAN,
 * a widget() zwraca zakładkę do interfejsu użytkownika.
 */
class ProtocolPlugin {
public:
    virtual ~ProtocolPlugin() = default;

    /// Unikalna nazwa wtyczki (np. "CANopen", "J1939").
    [[nodiscard]] virtual QString name() const = 0;

    /// Krótki opis.
    [[nodiscard]] virtual QString description() const = 0;

    /// Przetwarza ramkę CAN.
    virtual void processFrame(const CanFrame &frame) = 0;

    /// Zwraca widget UI dla zakładki (nullptr = brak UI).
    [[nodiscard]] virtual QWidget *widget() = 0;

    /// Czy ramka CAN jest obsługiwana przez tę wtyczkę.
    [[nodiscard]] virtual bool isRelevant(const CanFrame &frame) = 0;
};

/// Fabryka wtyczek (musi być zdefiniowana w każdej .so).
extern "C" ProtocolPlugin *createPlugin();
