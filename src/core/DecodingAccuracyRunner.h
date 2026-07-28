#pragma once
#include "CanFrame.h"
#include "ColdStartDetector.h"
#include "LlmQueryClient.h"
#include "WebSocketServer.h"
#include <QObject>
#include <QString>
#include <QJsonArray>
#include <deque>
#include <vector>
#include <cstdint>

/**
 * @brief Automat badawczy Eksperymentu 4.1: Skuteczność identyfikacji sygnałów
 * (Decoding Accuracy vs Ground Truth).
 *
 * W odróżnieniu od ExperimentRunner (Eksperyment 1.1, mierzy WYŁĄCZNIE czas),
 * ta klasa mierzy POPRAWNOŚĆ reguły zaproponowanej przez LLM: po wykryciu
 * Cold Start dla danego CAN ID, pyta LLM o interpretację bajtów (schemat
 * rozszerzony o sygnały wielobajtowe — patrz buildSystemPrompt()), a następnie
 * stosuje zwróconą regułę do kolejnych ramek tego ID i porównuje wynik z
 * ground truth (znanym, bo to MY generujemy ruch CAN wg zdefiniowanego niżej
 * układu 10 syntetycznych sygnałów na 3 CAN ID — patrz groundTruthFor()).
 *
 * Wymaga fizycznego ESP32 (esp_experiment_1_1.ino, NIEZMIENIONE — przekazuje
 * surowe ramki przez WebSocket niezależnie od ich treści) i generatora ruchu
 * (PEAK PCAN-USB) kodującego te same 10 sygnałów zgodnie z groundTruthFor().
 */
class DecodingAccuracyRunner : public QObject {
    Q_OBJECT
public:
    explicit DecodingAccuracyRunner(QObject *parent = nullptr);

    void setDetector(ColdStartDetector *detector);
    void setLlmClient(LlmQueryClient *client);
    void useRealHardware(WebSocketServer *server);

    /// Warianty promptu systemowego wyslanego do LLM - patrz build*() w .cpp.
    enum class PromptVariant {
        ZeroShot,         ///< oryginalny prompt, bez przykladow/dodatkowych instrukcji
        FewShot,          ///< + 2 w pelni rozwiazane przyklady (WYNIK: negatywny, patrz pamiec/raport)
        EntropyAnalysis,  ///< + wymuszona procedura krok-po-kroku: sprawdz kazdy bajt
                          ///< pod katem "upakowane flagi bitowe" vs "skalar" PRZED odpowiedzia
    };

    void setModel(const QString &modelName, const QString &apiKey);
    void setTotalTrials(int n) { m_totalTrials = n; }
    void setFramesToEvaluatePerTrial(int n) { m_framesToEvaluate = n; }
    void setReportPath(const QString &path) { m_reportPath = path; }
    void setPromptVariant(PromptVariant v) { m_promptVariant = v; }

    void start();

    [[nodiscard]] bool isRunning() const { return m_running; }
    [[nodiscard]] int currentTrial() const { return m_currentTrial; }

signals:
    void progressChanged(double fraction, const QString &statusText);
    void trialCompleted(int trialIndex, uint32_t canId);
    void experimentFinished(const QString &reportPath);
    void experimentError(const QString &msg);

private slots:
    void onColdStartDetected(uint32_t canId, const CanFrame &frame,
                              uint64_t timestampUs, const QString &reason);
    void onLlmResponse(const LlmResponse &resp);
    void onLlmError(const QString &errorMsg);
    void onRealFrameReceived(const CanFrame &frame);

private:
    enum class State { Idle, WaitingForColdStart, QueryingLlm, CollectingFrames, Done };

    // ── Definicje sygnałów (ground truth + reguła zaproponowana przez LLM) ──
    struct GroundTruthSignal {
        QString name;
        int     byteIdx = 0;
        int     byteLen = 1;      // 1 lub 2
        bool    littleEndian = true;
        bool    isSigned = false;
        double  scale = 1.0;
        double  offset = 0.0;
        bool    isDiscrete = false;
        int     bitIndex = -1;    // tylko gdy isDiscrete

        [[nodiscard]] double decode(const CanFrame &frame) const;
    };

    struct LlmSignalRule {
        QString  name;
        int      byteIdx = -1;
        int      byteLen = 1;
        bool     littleEndian = true;
        bool     isSigned = false;
        uint32_t bitMask = 0xFFFFFFFFu;
        double   scale = 1.0;
        double   offset = 0.0;

        [[nodiscard]] double decode(const CanFrame &frame) const;
    };

    struct SignalAccum {
        QString name;
        bool    isDiscrete = false;
        int     trialsSeen = 0;
        int     trialsDetected = 0; // LLM zaproponował regułę na właściwym byteIdx
        // ciągłe:
        std::vector<double> squaredErrors;
        // dyskretne: macierz pomyłek 2x2 (tylko wśród wykrytych)
        int tp = 0, tn = 0, fp = 0, fn = 0;

        // Metryki RÓWNOLEGŁE, z zastosowanym hybrydowym override'em klasycznym
        // (Kierunek B — patrz Eksperyment_4.2_Propozycja_Dalszej_Optymalizacji_LLM).
        // Osobny, dodatkowy zestaw liczników — NIE zastępuje powyższych "surowych"
        // metryk LLM, żeby dotychczasowe raporty/porównania (4 modele, warianty
        // promptu) pozostały ważne i porównywalne bez zmian.
        int trialsDetectedOverride = 0;
        std::vector<double> squaredErrorsOverride;
        int tpOverride = 0, tnOverride = 0, fpOverride = 0, fnOverride = 0;
    };

    void setState(State s);
    void startNextTrial();
    void finishExperiment();
    void finalizeCurrentTrial();
    void evaluateFrameAgainstRules(const CanFrame &frame);

    [[nodiscard]] static QString buildSystemPrompt();
    [[nodiscard]] static QString buildSystemPromptFewShot();
    [[nodiscard]] static QString buildSystemPromptEntropyAnalysis();
    [[nodiscard]] static std::vector<LlmSignalRule> parseRulesFromResponseText(const QString &text);
    [[nodiscard]] static std::vector<GroundTruthSignal> groundTruthFor(uint32_t canId);
    [[nodiscard]] static uint32_t extractRaw(const CanFrame &frame, int byteIdx, int byteLen,
                                              bool littleEndian);
    [[nodiscard]] static int singleBitPosition(uint32_t mask);
    [[nodiscard]] static const LlmSignalRule *findMatchingRule(
        const GroundTruthSignal &gt, const std::vector<LlmSignalRule> &rules);

    // ── Hybrydowy override klasyczny (Kierunek B) ──────────────────────────────
    // Deterministyczna klasyfikacja bajtu na podstawie OBSERWOWANYCH wartości w
    // historii ramek (NIE na podstawie ground truth — to musi być uczciwa
    // heurystyka, nie podglądanie odpowiedzi). Jeśli LLM zaproponuje pojedynczy
    // skalar dla bajtu, który wygląda jak niezależne flagi bitowe, override
    // programowo zastępuje tę regułę zestawem reguł per-bit.
    [[nodiscard]] static uint8_t independentBitMask(const std::vector<CanFrame> &frames, int byteIdx);
    [[nodiscard]] static bool looksLikeBitFlags(const std::vector<CanFrame> &frames, int byteIdx);
    [[nodiscard]] static std::vector<LlmSignalRule> applyBitFlagOverride(
        const std::vector<LlmSignalRule> &rules, const std::vector<CanFrame> &frames);

    ColdStartDetector *m_detector = nullptr;
    LlmQueryClient    *m_llmClient = nullptr;
    WebSocketServer   *m_wsServer  = nullptr;

    QString m_modelName;
    QString m_apiKey;

    int     m_totalTrials = 100;
    int     m_framesToEvaluate = 10;
    QString m_reportPath;
    PromptVariant m_promptVariant = PromptVariant::ZeroShot;

    State   m_state = State::Idle;
    bool    m_running = false;
    int     m_currentTrial = 0;

    // Round-robin po 3 CAN ID — patrz kMessageIds w .cpp
    int      m_targetIdCycle = 0;
    uint32_t m_targetCanId = 0;
    uint32_t m_currentCanId = 0;

    // Osobny bufor historii NA KAZDE CAN ID (naprawa zanieczyszczenia kontekstu:
    // przed ta zmiana wspolny bufor FIFO mieszal ramki wszystkich 3 ID, wiec po
    // ustabilizowaniu (round-robin) tylko ~10/30=33% recentFrames dotyczylo
    // faktycznie badanego ID - reszta byla nieistotnym szumem z pozostalych
    // wiadomosci, mimo ze prompt jawnie obiecuje modelowi "recent frames FOR
    // THIS ID"). Kazde ID ma teraz wlasny, czysty bufor do 30 ramek.
    QHash<uint32_t, std::deque<CanFrame>> m_frameHistoryByCanId;
    static constexpr int kMaxHistory = 30;

    std::vector<LlmSignalRule>      m_currentRules;
    std::vector<LlmSignalRule>      m_currentRulesOverride; // patrz applyBitFlagOverride()
    std::vector<CanFrame>           m_currentHistoryFrames; // te same ramki, ktore widzial LLM w recentFrames
    std::vector<GroundTruthSignal>  m_currentGroundTruth;
    int m_framesCollected = 0;

    QHash<QString, SignalAccum> m_results; // klucz = nazwa sygnału ground truth
    QJsonArray m_trialLog;                  // surowe odpowiedzi LLM (audyt)
};
