#pragma once
#include "CanFrame.h"
#include <QObject>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstdint>

/**
 * @brief Wykrywa pierwszy kontakt z nieznaną ramką CAN (Cold Start).
 *
 * Sygnalizuje coldStartDetected, gdy pojawi się:
 *   - CAN ID nigdy wcześniej niewidziany w tej sesji, LUB
 *   - znany CAN ID, ale payload znacząco odbiega od zapamiętanego wzorca
 *     (odległość Hamminga > threshold).
 *
 * Znaczniki czasu są brane z CanFrame::timestamp (mikrosekundy od epoch).
 * t_det = timestamp ramki wyzwalającej — jest przekazywany do sygnału.
 */
class ColdStartDetector : public QObject {
    Q_OBJECT
public:
    explicit ColdStartDetector(QObject *parent = nullptr);

    /// Ustawia próg odległości Hamminga dla znanych ID (0..64, domyślnie 16).
    void setHammingThreshold(int threshold) { m_hammingThreshold = threshold; }

    /// Oznacza CAN ID jako znany (np. po zakończeniu adaptacji).
    void markKnown(uint32_t canId);

    /// Oznacza CAN ID jako znany + zapamiętuje bieżący payload jako wzorzec.
    void markKnownWithPattern(uint32_t canId, const CanFrame &frame);

    /// Resetuje całą wiedzę o znanych ID.
    void reset();

    /// Zwraca liczbę znanych CAN ID.
    [[nodiscard]] int knownIdCount() const { return static_cast<int>(m_knownIds.size()); }

    /// Sprawdza czy dany CAN ID jest już znany.
    [[nodiscard]] bool isKnown(uint32_t canId) const;

signals:
    /// Emitowany przy pierwszym kontakcie z nieznaną ramką.
    /// @param canId       — CAN ID ramki
    /// @param frame        — pełna ramka CAN
    /// @param timestampUs  — znacznik czasu (μs) ramki wyzwalającej (= t_det)
    /// @param reason       — przyczyna: "new_id" lub "hamming_distance"
    void coldStartDetected(uint32_t canId, const CanFrame &frame,
                           uint64_t timestampUs, const QString &reason);

public slots:
    /// Główna metoda: przetwarza ramkę CAN i decyduje czy to Cold Start.
    void evaluate(const CanFrame &frame);

private:
    [[nodiscard]] static int hammingDistance(const uint8_t *a, const uint8_t *b, int len);

    std::unordered_set<uint32_t> m_knownIds;
    // Dla każdego znanego ID przechowujemy pierwszy widziany payload (do 64 bajtów)
    std::unordered_map<uint32_t, std::vector<uint8_t>> m_knownPatterns;
    int m_hammingThreshold = 16;
};
