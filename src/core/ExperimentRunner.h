#pragma once
#include "CanFrame.h"
#include "ColdStartDetector.h"
#include "LlmQueryClient.h"
#include "LatencyProfiler.h"
#include "WebSocketServer.h"
#include <QObject>
#include <QTimer>
#include <QString>
#include <QHash>
#include <QElapsedTimer>
#include <vector>
#include <deque>
#include <cstdint>
#include <random>

/**
 * @brief Automat badawczy Eksperymentu 1.1: Profilowanie czasu fazy adaptacji.
 *
 * Orkiestruje pełny pipeline Cold Start:
 *   Idle → Detecting → QueryingLlm → Compiling → SendingOta → Done
 *
 * Mierzy wszystkie 5 składowych czasu T_total:
 *   t_det, t_tx_up, t_llm, t_comp, t_ota
 *
 * Obsługuje wiele modeli LLM w trybie sekwencyjnym — dla każdego modelu
 * wykonuje m_trialsPerModel (domyślnie 30) prób, a następnie przechodzi
 * do kolejnego modelu. Po zakończeniu wszystkich modeli emituje
 * experimentFinished() i automatycznie zapisuje raport JSON.
 */
class ExperimentRunner : public QObject {
    Q_OBJECT
public:
    explicit ExperimentRunner(QObject *parent = nullptr);

    /// Ustawia komponenty (przekazanie własności przez parent).
    void setDetector(ColdStartDetector *detector);
    void setLlmClient(LlmQueryClient *client);
    void setProfiler(LatencyProfiler *profiler);

    /// Konfiguracja eksperymentu.
    void setTrialsPerModel(int n) { m_trialsPerModel = n; }
    void setModelList(const QStringList &models) { m_modelList = models; }
    void setReportPath(const QString &path) { m_reportPath = path; }

    /// Dodaje pojedynczy model LLM do testowania.
    void addModel(const QString &modelName);

    /// Ustawia klucz API dla danego modelu (wymagane przed start() — startNextModel()
    /// buduje świeży LlmConfig dla każdego modelu i musi znać jego klucz).
    void setApiKey(const QString &modelName, const QString &apiKey);

    /// Rozpoczyna eksperyment (testuje wszystkie modele po kolei).
    void start();

    /// Zatrzymuje eksperyment.
    void stop();

    /// Zwraca postęp [0.0-1.0].
    [[nodiscard]] double progress() const;

    /// Status eksperymentu.
    [[nodiscard]] bool isRunning() const { return m_running; }
    [[nodiscard]] QString currentModel() const { return m_currentModel; }
    [[nodiscard]] int currentTrial() const { return m_currentTrial; }

    /// Ręczne wyzwolenie Cold Start (zamiast z magistrali CAN).
    /// Przydatne do testów bez fizycznego sprzętu — w tym trybie t_det/t_tx_up/t_ota
    /// są symulowane (patrz hardwareSimulated()), t_llm i t_comp pozostają realnymi pomiarami.
    void injectManualColdStart(uint32_t canId, const CanFrame &frame);

    /// Czy bieżący eksperyment działa bez fizycznego ESP32 (symulacja t_det/t_tx_up/t_ota).
    [[nodiscard]] bool hardwareSimulated() const { return m_hardwareSimulated; }

    /// Włącza tryb prawdziwego sprzętu: ramki CAN przychodzą z realnego ESP32
    /// (esp_experiment_1_1.ino) przez WebSocket (server->frameReceivedFromClient),
    /// t_tx_up/t_det mierzone realnie (zegar serwera + korekta przesunięcia zegara
    /// ESP32 wykonana w firmware), t_ota mierzone jako realny czas od wysłania
    /// reguły (sendRuleUpdate) do otrzymania rule_ack od ESP32 (z timeoutem).
    void useRealHardware(WebSocketServer *server, int otaTimeoutMs = 5000);

signals:
    /// Postęp ogólny (0.0-1.0) i tekst statusu.
    void progressChanged(double fraction, const QString &statusText);

    /// Pojedyncza próbka została zebrana.
    void trialCompleted(int trialIndex, const QString &modelName,
                        uint64_t tTotalMs);

    /// Model zakończony (N próbek zebrane).
    void modelCompleted(const QString &modelName, const LatencyStats &stats);

    /// Cały eksperyment zakończony.
    void experimentFinished(const QString &reportPath);

    /// Błąd krytyczny.
    void experimentError(const QString &msg);

private slots:
    void onColdStartDetected(uint32_t canId, const CanFrame &frame,
                              uint64_t timestampUs, const QString &reason);
    void onLlmResponse(const LlmResponse &resp);
    void onLlmError(const QString &errorMsg);
    void advanceTrial();

    /// Ramka odebrana z realnego ESP32 (WebSocketServer::frameReceivedFromClient).
    void onRealFrameReceived(const CanFrame &frame);
    /// ESP32 potwierdził zastosowanie reguły OTA (realny pomiar t_ota).
    void onRuleAckReceived(uint32_t canId);
    /// Brak potwierdzenia OTA od ESP32 w zadanym czasie — próba liczy się jako failed.
    void onOtaAckTimeout();

private:
    enum class State {
        Idle,
        WaitingForDetection,
        QueryingLlm,
        Compiling,
        SendingOta,
        Done
    };

    void setState(State s);
    void startNextTrial();
    void startNextModel();
    void finishExperiment();

    [[nodiscard]] static uint64_t nowUs();

    /// Konwertuje payload ramki na string hex (do plików "input_data").
    [[nodiscard]] static QString frameDataHex(const CanFrame &frame);

    /// Losuje wartość z rozkładu normalnego (obciętego od dołu do minUs), w mikrosekundach.
    /// Używane WYŁĄCZNIE gdy brak fizycznego ESP32 — patrz m_hardwareSimulated.
    [[nodiscard]] uint64_t sampleSimulatedUs(double meanUs, double sigmaUs, double minUs);

    /// Zapisuje próbkę (sukces, m_tOtaUs już ustawione) i przechodzi do kolejnej próby.
    void finalizeSample(bool success, const QString &errorMsg = {});

    // Komponenty
    ColdStartDetector *m_detector = nullptr;
    LlmQueryClient    *m_llmClient = nullptr;
    LatencyProfiler   *m_profiler = nullptr;

    // Konfiguracja
    QStringList m_modelList;
    QHash<QString, QString> m_apiKeys; // key = nazwa modelu (jak w m_modelList)
    int         m_trialsPerModel = 30;
    QString     m_reportPath;  // domyślnie: "latency_report_<timestamp>.json"

    // Stan eksperymentu
    State       m_state = State::Idle;
    bool        m_running = false;
    int         m_modelIndex = 0;
    int         m_currentTrial = 0;
    QString     m_currentModel;

    // Timing per-trial
    uint64_t    m_tDetUs     = 0;
    uint64_t    m_tTxUpUs    = 0;
    uint64_t    m_tLlmStartUs = 0;
    uint64_t    m_tLlmEndUs   = 0;
    uint64_t    m_tCompUs    = 0;
    uint64_t    m_tOtaUs     = 0;
    uint64_t    m_tLlmUs     = 0;
    QString     m_pendingLlmResponseText;
    uint32_t    m_currentCanId = 0;
    CanFrame    m_triggerFrame;

    // Ramki dla kontekstu LLM (ostatnie N ramek dla aktywnego CAN ID)
    std::deque<CanFrame> m_frameHistory;
    static constexpr int kMaxHistory = 50;

    QElapsedTimer m_trialTimer;  // timer dla t_comp, t_ota

    // Symulacja sprzętowa (brak fizycznego ESP32/magistrali CAN) — patrz hardwareSimulated()
    bool             m_hardwareSimulated = false;
    std::mt19937     m_rng{std::random_device{}()};

    // Tryb realnego sprzętu (ESP32 przez WebSocketServer) — patrz useRealHardware()
    WebSocketServer *m_wsServer = nullptr;
    bool             m_realHardwareMode = false;
    uint64_t         m_pendingFrameArrivalUs = 0;
    uint64_t         m_otaSendTimeUs = 0;
    QTimer          *m_otaTimeoutTimer = nullptr;
    int              m_otaTimeoutMs = 5000;
};
