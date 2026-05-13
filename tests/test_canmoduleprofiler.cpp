#include <gtest/gtest.h>
#include "core/CanModuleProfiler.h"
#include "core/CanFrame.h"
#include <QJsonDocument>
#include <thread>
#include <chrono>

namespace {
static CanFrame makeFrame(uint32_t id, bool extended = false) {
    CanFrame f{};
    f.id       = id;
    f.dlc      = 8;
    f.extended = extended;
    return f;
}

// Feed N frames for `id` at `intervalUs` spacing, starting at `startUs`.
void feedPeriodic(CanModuleProfiler &p, uint32_t id, int n, uint64_t intervalUs,
                  uint64_t startUs = 0) {
    CanFrame f = makeFrame(id);
    for (int i = 0; i < n; ++i)
        p.feed(f, startUs + static_cast<uint64_t>(i) * intervalUs);
}
} // namespace

// ── State tests ───────────────────────────────────────────────────────────────

TEST(CanModuleProfiler, InitialStateIsIdle) {
    CanModuleProfiler p;
    EXPECT_EQ(p.state(), CanModuleProfiler::Idle);
}

TEST(CanModuleProfiler, StartLearningTransitionsState) {
    CanModuleProfiler p;
    p.startLearning("Test", 500);
    EXPECT_EQ(p.state(), CanModuleProfiler::Learning);
    p.cancelLearning();
    EXPECT_EQ(p.state(), CanModuleProfiler::Idle);
}

TEST(CanModuleProfiler, FeedIgnoredWhenIdle) {
    CanModuleProfiler p;
    p.feed(makeFrame(0x100), 0);   // must not crash
    EXPECT_TRUE(p.lastLearnedProfile().isEmpty());
}

// ── Learning: periodic ID detection ──────────────────────────────────────────

TEST(CanModuleProfiler, PeriodicIdDetected) {
    CanModuleProfiler p;
    p.startLearning("ABS", 100);

    feedPeriodic(p, 0x1A0, 20, 10'000);  // 20 frames at 10ms intervals

    bool gotSignal = false;
    ModuleProfile result;
    QObject::connect(&p, &CanModuleProfiler::learningFinished,
                     [&](const ModuleProfile &prof) { gotSignal = true; result = prof; });
    p.finalizeLearning();

    ASSERT_TRUE(gotSignal);
    ASSERT_EQ(result.periodicIds.size(), 1);
    EXPECT_EQ(result.periodicIds[0].id, 0x1A0u);
    EXPECT_NEAR(result.periodicIds[0].avgIntervalMs, 10.0, 0.5);
    EXPECT_LT(result.periodicIds[0].jitterMs, 1.0);
}

TEST(CanModuleProfiler, AperiodicIdNotDetected) {
    CanModuleProfiler p;
    p.startLearning("Noisy", 100);

    // Feed frames with highly variable intervals
    CanFrame f = makeFrame(0x200);
    uint64_t ts = 0;
    const uint64_t intervals[] = {5'000, 50'000, 3'000, 200'000, 7'000, 80'000,
                                   2'000, 120'000, 15'000, 60'000};
    for (uint64_t iv : intervals) {
        ts += iv;
        p.feed(f, ts);
    }

    ModuleProfile result;
    QObject::connect(&p, &CanModuleProfiler::learningFinished,
                     [&](const ModuleProfile &prof) { result = prof; });
    p.finalizeLearning();

    EXPECT_TRUE(result.periodicIds.isEmpty());
}

TEST(CanModuleProfiler, TwoPeriodicIdsBothDetected) {
    CanModuleProfiler p;
    p.startLearning("Multi", 100);

    feedPeriodic(p, 0x100, 20, 10'000);  // 10ms
    feedPeriodic(p, 0x200, 20, 20'000);  // 20ms

    ModuleProfile result;
    QObject::connect(&p, &CanModuleProfiler::learningFinished,
                     [&](const ModuleProfile &prof) { result = prof; });
    p.finalizeLearning();

    ASSERT_EQ(result.periodicIds.size(), 2);
    // profiles are sorted by ID
    EXPECT_EQ(result.periodicIds[0].id, 0x100u);
    EXPECT_EQ(result.periodicIds[1].id, 0x200u);
}

TEST(CanModuleProfiler, TooFewFramesNotPromoted) {
    CanModuleProfiler p;
    p.startLearning("Sparse", 100);

    feedPeriodic(p, 0x300, 3, 10'000);  // only 3 frames — below kMinPeriodicFrames

    ModuleProfile result;
    QObject::connect(&p, &CanModuleProfiler::learningFinished,
                     [&](const ModuleProfile &prof) { result = prof; });
    p.finalizeLearning();

    EXPECT_TRUE(result.periodicIds.isEmpty());
}

// ── Learning: req/resp detection ──────────────────────────────────────────────

TEST(CanModuleProfiler, ReqRespPairDetected) {
    CanModuleProfiler p;
    p.startLearning("Diag", 100);

    // Simulate 10 request/response pairs: 0x7E0 then 0x7E8 ~50ms later
    for (int i = 0; i < 10; ++i) {
        uint64_t base = static_cast<uint64_t>(i) * 200'000;  // 200ms between pairs
        p.feed(makeFrame(0x7E0), base);
        p.feed(makeFrame(0x7E8), base + 50'000);             // 50ms latency
    }

    ModuleProfile result;
    QObject::connect(&p, &CanModuleProfiler::learningFinished,
                     [&](const ModuleProfile &prof) { result = prof; });
    p.finalizeLearning();

    bool found = false;
    for (const auto &rr : result.reqResPairs) {
        if (rr.reqId == 0x7E0u && rr.respId == 0x7E8u) {
            found = true;
            EXPECT_NEAR(rr.avgLatencyMs, 50.0, 5.0);
            EXPECT_GE(rr.occurrences, 5);
        }
    }
    EXPECT_TRUE(found);
}

// ── Detection ─────────────────────────────────────────────────────────────────

TEST(CanModuleProfiler, DetectionOfflineWhenIdMissing) {
    CanModuleProfiler p;
    p.startLearning("Node", 100);
    feedPeriodic(p, 0x100, 20, 10'000);
    ModuleProfile prof;
    QObject::connect(&p, &CanModuleProfiler::learningFinished,
                     [&](const ModuleProfile &pr) { prof = pr; });
    p.finalizeLearning();
    ASSERT_EQ(prof.periodicIds.size(), 1);

    p.startDetecting(prof, 0);  // base=0 so deadline is small (windowMs*1000 ≈ 60000 µs)
    EXPECT_EQ(p.state(), CanModuleProfiler::Detecting);

    uint32_t offlineId = 0;
    QObject::connect(&p, &CanModuleProfiler::moduleOffline,
                     [&](const QString &, uint32_t id) { offlineId = id; });

    // nowUs=500000 µs >> deadline≈60000 µs → offline triggered
    p.checkDetection(500'000);

    EXPECT_EQ(offlineId, 0x100u);
}

TEST(CanModuleProfiler, DetectionOnlineWhenIdPresent) {
    CanModuleProfiler p;
    p.startLearning("Node", 100);
    feedPeriodic(p, 0x100, 20, 10'000);
    ModuleProfile prof;
    QObject::connect(&p, &CanModuleProfiler::learningFinished,
                     [&](const ModuleProfile &pr) { prof = pr; });
    p.finalizeLearning();

    p.startDetecting(prof);

    bool fired = false;
    QObject::connect(&p, &CanModuleProfiler::moduleOffline,
                     [&](const QString &, uint32_t) { fired = true; });

    // Feed within the expected interval
    p.feed(makeFrame(0x100), 5'000);

    // Check with a time that is still within the deadline
    p.checkDetection(10'000);

    EXPECT_FALSE(fired);
}

TEST(CanModuleProfiler, ModuleOnlineAfterRecovery) {
    CanModuleProfiler p;
    p.startLearning("Node", 100);
    feedPeriodic(p, 0x100, 20, 10'000);
    ModuleProfile prof;
    QObject::connect(&p, &CanModuleProfiler::learningFinished,
                     [&](const ModuleProfile &pr) { prof = pr; });
    p.finalizeLearning();

    p.startDetecting(prof, 0);

    int offlineCount = 0;
    int onlineCount  = 0;
    QObject::connect(&p, &CanModuleProfiler::moduleOffline,
                     [&](const QString &, uint32_t) { ++offlineCount; });
    QObject::connect(&p, &CanModuleProfiler::moduleOnline,
                     [&](const QString &) { ++onlineCount; });

    // Trigger offline
    p.checkDetection(500'000);
    EXPECT_EQ(offlineCount, 1);

    // Feed a frame — ID is now present
    p.feed(makeFrame(0x100), 510'000);

    // Check again — should go back online
    p.checkDetection(515'000);
    EXPECT_EQ(onlineCount, 1);
}

TEST(CanModuleProfiler, StopDetectingResetsState) {
    CanModuleProfiler p;
    p.startLearning("N", 100);
    feedPeriodic(p, 0x1, 20, 10'000);
    ModuleProfile prof;
    QObject::connect(&p, &CanModuleProfiler::learningFinished,
                     [&](const ModuleProfile &pr) { prof = pr; });
    p.finalizeLearning();

    p.startDetecting(prof);
    EXPECT_EQ(p.state(), CanModuleProfiler::Detecting);

    p.stopDetecting();
    EXPECT_EQ(p.state(), CanModuleProfiler::Idle);
}

// ── Serialization ─────────────────────────────────────────────────────────────

TEST(CanModuleProfiler, JsonRoundtrip) {
    ModuleProfile orig;
    orig.name            = "ECU_ABS";
    orig.learnDurationMs = 8000;
    orig.learnedAtUs     = 1234567890;

    ModulePeriodicId pid;
    pid.id            = 0x1A0;
    pid.extended      = false;
    pid.avgIntervalMs = 10.0;
    pid.jitterMs      = 0.5;
    pid.frameCount    = 800;
    orig.periodicIds.append(pid);

    ModuleReqResPair rr;
    rr.reqId        = 0x7E0;
    rr.respId       = 0x7E8;
    rr.avgLatencyMs = 45.0;
    rr.occurrences  = 12;
    orig.reqResPairs.append(rr);

    ModuleProfile restored = ModuleProfile::fromJson(orig.toJson());

    EXPECT_EQ(restored.name, orig.name);
    EXPECT_EQ(restored.learnDurationMs, orig.learnDurationMs);
    EXPECT_EQ(restored.learnedAtUs, orig.learnedAtUs);

    ASSERT_EQ(restored.periodicIds.size(), 1);
    EXPECT_EQ(restored.periodicIds[0].id, 0x1A0u);
    EXPECT_NEAR(restored.periodicIds[0].avgIntervalMs, 10.0, 1e-9);
    EXPECT_EQ(restored.periodicIds[0].frameCount, 800u);

    ASSERT_EQ(restored.reqResPairs.size(), 1);
    EXPECT_EQ(restored.reqResPairs[0].reqId, 0x7E0u);
    EXPECT_EQ(restored.reqResPairs[0].respId, 0x7E8u);
    EXPECT_EQ(restored.reqResPairs[0].occurrences, 12);
}

TEST(CanModuleProfiler, EmptyProfileIsEmpty) {
    ModuleProfile p;
    EXPECT_TRUE(p.isEmpty());
    QJsonObject obj = p.toJson();
    ModuleProfile r = ModuleProfile::fromJson(obj);
    EXPECT_TRUE(r.isEmpty());
}

// ── Differential (2-phase) learning ──────────────────────────────────────────

// Helper: build a profile with a single periodic ID
static ModuleProfile makePhase1(uint32_t id, double intervalMs = 10.0) {
    CanModuleProfiler p;
    p.startLearning("Test", 100);
    feedPeriodic(p, id, 20, static_cast<uint64_t>(intervalMs * 1000));
    ModuleProfile prof;
    QObject::connect(&p, &CanModuleProfiler::learningFinished,
                     [&](const ModuleProfile &pr) { prof = pr; });
    p.finalizeLearning();
    return prof;
}

TEST(CanModuleProfiler, Differential_BackgroundIdRemoved) {
    // Phase 1: 0x100 + 0x200 both periodic
    CanModuleProfiler p;
    p.startLearning("ECU", 100);
    feedPeriodic(p, 0x100, 20, 10'000);
    feedPeriodic(p, 0x200, 20, 20'000);
    ModuleProfile phase1;
    QObject::connect(&p, &CanModuleProfiler::learningFinished,
                     [&](const ModuleProfile &pr) { phase1 = pr; });
    p.finalizeLearning();
    ASSERT_EQ(phase1.periodicIds.size(), 2);

    // Phase 2 (background): only 0x200 seen (belongs to another module)
    ModuleProfile diff;
    QObject::connect(&p, &CanModuleProfiler::backgroundLearningFinished,
                     [&](const ModuleProfile &pr) { diff = pr; });
    p.startLearningBackground(phase1, "ECU", 100);
    feedPeriodic(p, 0x200, 20, 20'000);  // background traffic
    p.finalizeBackgroundLearning();

    // 0x200 was in background → removed from differential profile
    ASSERT_EQ(diff.periodicIds.size(), 1);
    EXPECT_EQ(diff.periodicIds[0].id, 0x100u);
    EXPECT_TRUE(diff.isDifferential);
}

TEST(CanModuleProfiler, Differential_UniqueIdKept) {
    // Phase 1: only 0x300
    ModuleProfile phase1 = makePhase1(0x300);
    ASSERT_EQ(phase1.periodicIds.size(), 1);

    // Phase 2 (background): different ID on the bus
    CanModuleProfiler p;
    ModuleProfile diff;
    QObject::connect(&p, &CanModuleProfiler::backgroundLearningFinished,
                     [&](const ModuleProfile &pr) { diff = pr; });
    p.startLearningBackground(phase1, "ECU", 100);
    feedPeriodic(p, 0x400, 20, 10'000);  // unrelated background ID
    p.finalizeBackgroundLearning();

    // 0x300 not in background → kept in differential profile
    ASSERT_EQ(diff.periodicIds.size(), 1);
    EXPECT_EQ(diff.periodicIds[0].id, 0x300u);
    EXPECT_TRUE(diff.isDifferential);
}

TEST(CanModuleProfiler, Differential_EmptyBackgroundKeepsAll) {
    // Phase 1 with two periodic IDs; background is silent
    ModuleProfile phase1;
    {
        CanModuleProfiler p;
        p.startLearning("ECU", 100);
        feedPeriodic(p, 0x1, 20, 10'000);
        feedPeriodic(p, 0x2, 20, 20'000);
        QObject::connect(&p, &CanModuleProfiler::learningFinished,
                         [&](const ModuleProfile &pr) { phase1 = pr; });
        p.finalizeLearning();
    }
    ASSERT_EQ(phase1.periodicIds.size(), 2);

    CanModuleProfiler p;
    ModuleProfile diff;
    QObject::connect(&p, &CanModuleProfiler::backgroundLearningFinished,
                     [&](const ModuleProfile &pr) { diff = pr; });
    p.startLearningBackground(phase1, "ECU", 100);
    // No frames fed → empty background
    p.finalizeBackgroundLearning();

    EXPECT_EQ(diff.periodicIds.size(), 2);
    EXPECT_TRUE(diff.isDifferential);
}

TEST(CanModuleProfiler, Differential_JsonRoundtrip) {
    ModuleProfile orig = makePhase1(0x1A0);
    orig.isDifferential = true;

    ModuleProfile restored = ModuleProfile::fromJson(orig.toJson());
    EXPECT_TRUE(restored.isDifferential);
    ASSERT_EQ(restored.periodicIds.size(), orig.periodicIds.size());
    EXPECT_EQ(restored.periodicIds[0].id, 0x1A0u);
}

TEST(CanModuleProfiler, Differential_StateTransitions) {
    ModuleProfile phase1 = makePhase1(0x10);
    CanModuleProfiler p;
    p.startLearningBackground(phase1, "ECU", 100);
    EXPECT_EQ(p.state(), CanModuleProfiler::LearningBackground);

    p.cancelBackgroundLearning();
    EXPECT_EQ(p.state(), CanModuleProfiler::Idle);
}
