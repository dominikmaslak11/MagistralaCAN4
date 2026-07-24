#pragma once
#include "CanFrame.h"
#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <vector>
#include <cmath>
#include <cstdint>

/**
 * @brief Pojedyncza próbka pomiarowa dla Eksperymentu 1.1.
 *
 * Wszystkie czasy w mikrosekundach (μs). Konwersja na ms przez /1000.
 */
struct LatencySample {
    QString  modelName;           // np. "gpt-4o", "claude-3-5-sonnet", "deepseek-chat"
    uint32_t canId       = 0;    // CAN ID ramki wyzwalającej
    uint64_t tDetUs      = 0;    // czas detekcji (μs) — timestamp ramki
    uint64_t tTxUpUs     = 0;    // czas transmisji ESP32→serwer (μs) — strona serwerowa
    uint64_t tLlmUs      = 0;    // czas wnioskowania LLM (μs)
    uint64_t tCompUs     = 0;    // czas kompilacji/przygotowania reguły (μs)
    uint64_t tOtaUs      = 0;    // czas wysłania OTA do ESP32 (μs) — strona serwerowa
    uint64_t trialIndex  = 0;    // numer próby (0..N-1)
    bool     success     = true;
    QString  errorMsg;

    // Treść wejścia/wyjścia (do plików "input_data" — reprezentatywne przykłady
    // zapytań i odpowiedzi, nie tylko surowe czasy).
    QString  frameDataHex;      // payload ramki wyzwalającej (hex)
    QString  llmResponseText;   // surowa odpowiedź/reguła zwrócona przez LLM
};

/**
 * @brief Statystyki zagregowane dla jednego modelu LLM.
 */
struct LatencyStats {
    QString modelName;
    int     sampleCount = 0;

    // Średnie [ms]
    double meanDetMs  = 0.0;
    double meanTxUpMs = 0.0;
    double meanLlmMs  = 0.0;
    double meanCompMs = 0.0;
    double meanOtaMs  = 0.0;
    double meanTotalMs = 0.0;

    // Odchylenia standardowe [ms]
    double sigmaDetMs  = 0.0;
    double sigmaTxUpMs = 0.0;
    double sigmaLlmMs  = 0.0;
    double sigmaCompMs = 0.0;
    double sigmaOtaMs  = 0.0;
    double sigmaTotalMs = 0.0;
};

/**
 * @brief Zbiera próbki czasowe Eksperymentu 1.1 i oblicza statystyki.
 *
 * Obsługuje wiele modeli LLM jednocześnie (osobne zestawy próbek).
 * Każdy model wymaga N próbek (domyślnie 30).
 * Eksportuje wyniki do JSON dla dalszej analizy (Python/Matplotlib).
 */
class LatencyProfiler : public QObject {
    Q_OBJECT
public:
    explicit LatencyProfiler(QObject *parent = nullptr);

    /// Ustawia liczbę próbek na model (domyślnie 30).
    void setTrialsPerModel(int n) { m_trialsPerModel = n; }

    /// Dodaje pojedynczą próbkę pomiarową.
    void addSample(const LatencySample &sample);

    /// Zwraca wszystkie próbki dla danego modelu.
    [[nodiscard]] std::vector<LatencySample> samplesForModel(const QString &model) const;

    /// Zwraca nazwy wszystkich modeli, dla których zebrano próbki.
    [[nodiscard]] QStringList modelNames() const;

    /// Oblicza statystyki (μ, σ) dla danego modelu.
    [[nodiscard]] LatencyStats computeStats(const QString &model) const;

    /// Sprawdza czy zebrano wymaganą liczbę próbek dla modelu.
    [[nodiscard]] bool isModelComplete(const QString &model) const;

    /// Zwraca liczbę zebranych próbek dla modelu.
    [[nodiscard]] int sampleCount(const QString &model) const;

    /// Resetuje wszystkie dane.
    void reset();

    /// Eksportuje wszystkie próbki do JSON (tablica).
    [[nodiscard]] QJsonArray toJsonArray() const;

    /// Eksportuje zagregowane statystyki dla wszystkich modeli do JSON.
    [[nodiscard]] QJsonObject statsToJson() const;

    /// Eksportuje kompletny raport (próbki + statystyki) do JSON.
    [[nodiscard]] QJsonObject fullReport() const;

    /// Zapisuje raport do pliku.
    bool saveReport(const QString &filePath) const;

signals:
    /// Emitowany gdy model osiągnął wymaganą liczbę próbek.
    void modelComplete(const QString &modelName);

    /// Emitowany po dodaniu każdej próbki (do podglądu live).
    void sampleAdded(const QString &modelName, int currentCount, int targetCount);

private:
    [[nodiscard]] static double computeMean(const std::vector<double> &values);
    [[nodiscard]] static double computeStdDev(const std::vector<double> &values, double mean);

    QHash<QString, std::vector<LatencySample>> m_samples; // key = modelName
    int m_trialsPerModel = 30;
};
