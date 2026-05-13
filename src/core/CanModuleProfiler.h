#pragma once
#include "CanFrame.h"
#include <QObject>
#include <QTimer>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <cstdint>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <deque>

/// One CAN ID identified as periodic (heartbeat) in the module.
struct ModulePeriodicId {
    uint32_t id            = 0;
    bool     extended      = false;
    double   avgIntervalMs = 0.0;
    double   jitterMs      = 0.0;   // std-dev of inter-frame intervals
    uint64_t frameCount    = 0;
};

/// A request/response pair: reqId appears, respId follows within a short window.
struct ModuleReqResPair {
    uint32_t reqId        = 0;
    uint32_t respId       = 0;
    double   avgLatencyMs = 0.0;
    int      occurrences  = 0;
};

/// Behavioral profile of a CAN module learned from live traffic.
struct ModuleProfile {
    QString                   name;
    QVector<ModulePeriodicId> periodicIds;
    QVector<ModuleReqResPair> reqResPairs;
    int      learnDurationMs = 0;
    uint64_t learnedAtUs     = 0;
    bool     isDifferential  = false; // true when background subtraction was applied

    bool isEmpty() const { return periodicIds.isEmpty(); }
    QJsonObject toJson() const;
    static ModuleProfile fromJson(const QJsonObject &obj);
};

/// Learns and detects presence of a CAN module via its heartbeat pattern.
class CanModuleProfiler : public QObject {
    Q_OBJECT
public:
    explicit CanModuleProfiler(QObject *parent = nullptr);

    enum State { Idle, Learning, LearningBackground, Detecting };
    State state() const { return m_state; }

    void startLearning(const QString &name, int durationMs = 8000);
    void cancelLearning();
    void finalizeLearning();            // skips timer — useful for tests

    // Phase 2 differential learning: records background traffic (module absent)
    // and subtracts it from phase1 to isolate module-specific IDs.
    void startLearningBackground(const ModuleProfile &phase1, const QString &name,
                                 int durationMs = 8000);
    void cancelBackgroundLearning();
    void finalizeBackgroundLearning();  // skips timer — useful for tests

    // initNowUs: initial "last-seen" timestamp for all periodic IDs.
    // Default (UINT64_MAX) = use real wall-clock; pass an explicit value in tests.
    void startDetecting(const ModuleProfile &profile,
                        uint64_t initNowUs = std::numeric_limits<uint64_t>::max());
    void stopDetecting();

    void feed(const CanFrame &frame, uint64_t tsUs);
    void checkDetection(uint64_t nowUs); // exposed for tests; called by timer internally

    const ModuleProfile &lastLearnedProfile() const { return m_lastLearned; }

signals:
    void learningProgress(int percentDone);
    void learningFinished(const ModuleProfile &profile);
    void backgroundLearningProgress(int percentDone);
    void backgroundLearningFinished(const ModuleProfile &profile);
    void moduleOffline(const QString &profileName, uint32_t missingId);
    void moduleOnline(const QString &profileName);

private slots:
    void onLearningTimeout();
    void onBackgroundLearningTimeout();
    void onDetectionTick();

private:
    State   m_state              = Idle;
    QString m_learningName;
    int     m_learningDurationMs = 8000;
    uint64_t m_learningStartMs   = 0;

    struct WelfordState {
        int      n        = 0;
        double   mean     = 0.0;   // ms
        double   M2       = 0.0;
        uint64_t lastTs   = 0;     // µs
        uint64_t frames   = 0;
        bool     extended = false;
        double stddev() const {
            return (n > 1) ? std::sqrt(M2 / (n - 1)) : 0.0;
        }
    };
    std::unordered_map<uint32_t, WelfordState> m_learnData;

    // Background (phase 2) learning state
    ModuleProfile m_phase1Profile;
    std::unordered_map<uint32_t, WelfordState> m_bgLearnData;
    uint64_t m_bgLearningStartMs   = 0;
    int      m_bgLearningDurationMs = 8000;
    QTimer  *m_bgLearningTimer  = nullptr;
    QTimer  *m_bgProgressTimer  = nullptr;
    void finalizeBackgroundProfile();

    struct RecentEntry { uint32_t id; uint64_t tsUs; };
    std::deque<RecentEntry> m_recentFrames;

    struct RRCandidate {
        uint32_t reqId  = 0;
        uint32_t respId = 0;
        int      count  = 0;
        double   latSum = 0.0;
    };
    std::unordered_map<uint64_t, RRCandidate> m_rrCandidates;

    QTimer *m_learningTimer  = nullptr;
    QTimer *m_progressTimer  = nullptr;

    void finalizeProfile();

    ModuleProfile m_lastLearned;

    // Detection state
    ModuleProfile                           m_detectProfile;
    std::unordered_map<uint32_t, uint64_t>  m_lastSeenUs;
    std::unordered_set<uint32_t>            m_currentlyMissing;
    QTimer *m_detectionTimer = nullptr;

    static constexpr int     kDetectionTickMs       = 250;
    static constexpr int64_t kReqRespWindowUs        = 150'000;
    static constexpr int     kMinReqRespOccurrences  = 5;
    static constexpr double  kPeriodicJitterRatio    = 0.35;
    static constexpr int     kMinPeriodicFrames      = 5;
};
