/**
 * @file test_canintervalanalyzer.cpp
 * @brief Unit tests for CanIntervalAnalyzer and CanIntervalStats.
 */
#include <gtest/gtest.h>
#include "core/CanIntervalAnalyzer.h"
#include <QCoreApplication>
#include <cmath>

// ── Helpers ──────────────────────────────────────────────────────────────────

static CanFrame makeTimedFrame(uint32_t id, uint64_t timestampUs) {
    CanFrame f{};
    f.id        = id;
    f.dlc       = 1;
    f.timestamp = timestampUs;
    return f;
}

// Generate N periodic frames at 'periodUs' intervals starting from t0.
static QVector<CanFrame> periodicFrames(uint32_t id, uint64_t t0,
                                         uint64_t periodUs, int count) {
    QVector<CanFrame> v;
    for (int i = 0; i < count; ++i)
        v.append(makeTimedFrame(id, t0 + static_cast<uint64_t>(i) * periodUs));
    return v;
}

// ── Empty analyzer ────────────────────────────────────────────────────────

TEST(CanIntervalAnalyzer, Empty_StatsEmpty) {
    CanIntervalAnalyzer a;
    EXPECT_TRUE(a.stats().isEmpty());
}

TEST(CanIntervalAnalyzer, Empty_Report) {
    CanIntervalAnalyzer a;
    EXPECT_TRUE(a.report().contains("No frames"));
}

TEST(CanIntervalAnalyzer, Empty_StatsFor_ZeroFrame) {
    CanIntervalAnalyzer a;
    auto s = a.statsFor(0x100);
    EXPECT_EQ(s.frameCount, 0u);
}

// ── Single frame — no intervals to compute ────────────────────────────────

TEST(CanIntervalAnalyzer, SingleFrame_FrameCountOne) {
    CanIntervalAnalyzer a;
    a.add(makeTimedFrame(0x100, 1000));
    auto s = a.statsFor(0x100);
    EXPECT_EQ(s.frameCount, 1u);
    EXPECT_DOUBLE_EQ(s.meanUs, 0.0);
}

// ── Two frames — one interval ─────────────────────────────────────────────

TEST(CanIntervalAnalyzer, TwoFrames_IntervalComputed) {
    CanIntervalAnalyzer a;
    a.add(makeTimedFrame(0x100, 0));
    a.add(makeTimedFrame(0x100, 10000));  // 10ms interval

    auto s = a.statsFor(0x100);
    EXPECT_EQ(s.frameCount, 2u);
    EXPECT_DOUBLE_EQ(s.meanUs, 10000.0);
    EXPECT_EQ(s.minUs, 10000u);
    EXPECT_EQ(s.maxUs, 10000u);
}

// ── Three perfectly periodic frames ──────────────────────────────────────

TEST(CanIntervalAnalyzer, ThreePeriodicFrames) {
    CanIntervalAnalyzer a;
    a.addAll(periodicFrames(0x100, 0, 10000, 3));  // 0, 10ms, 20ms

    auto s = a.statsFor(0x100);
    EXPECT_EQ(s.frameCount, 3u);
    EXPECT_NEAR(s.meanUs, 10000.0, 1.0);
    // Two identical intervals → stdDev = 0
    EXPECT_NEAR(s.stdDevUs, 0.0, 1.0);
    EXPECT_EQ(s.minUs, 10000u);
    EXPECT_EQ(s.maxUs, 10000u);
}

// ── isPeriodic — ideally periodic messages ────────────────────────────────

TEST(CanIntervalAnalyzer, IsPeriodic_Strict) {
    CanIntervalAnalyzer a;
    a.addAll(periodicFrames(0x100, 0, 10000, 10));  // 10 frames at 10ms

    auto s = a.statsFor(0x100);
    EXPECT_TRUE(s.isPeriodic());
}

// ── isPeriodic — high jitter → not periodic ───────────────────────────────

TEST(CanIntervalAnalyzer, IsNotPeriodic_HighJitter) {
    CanIntervalAnalyzer a;
    // Intervals: 10000, 100000, 10000, 100000 → huge stdDev relative to mean
    a.add(makeTimedFrame(0x100, 0));
    a.add(makeTimedFrame(0x100, 10000));
    a.add(makeTimedFrame(0x100, 110000));
    a.add(makeTimedFrame(0x100, 120000));
    a.add(makeTimedFrame(0x100, 220000));

    auto s = a.statsFor(0x100);
    EXPECT_FALSE(s.isPeriodic());
}

// ── isPeriodic — needs at least 3 frames ─────────────────────────────────

TEST(CanIntervalAnalyzer, IsNotPeriodic_TwoFrames) {
    CanIntervalAnalyzer a;
    a.add(makeTimedFrame(0x100, 0));
    a.add(makeTimedFrame(0x100, 10000));

    auto s = a.statsFor(0x100);
    EXPECT_FALSE(s.isPeriodic());
}

// ── Min and max tracked correctly ────────────────────────────────────────

TEST(CanIntervalAnalyzer, MinMax_Correct) {
    CanIntervalAnalyzer a;
    a.add(makeTimedFrame(0x100, 0));
    a.add(makeTimedFrame(0x100, 5000));   // interval = 5000
    a.add(makeTimedFrame(0x100, 17000));  // interval = 12000
    a.add(makeTimedFrame(0x100, 20000));  // interval = 3000

    auto s = a.statsFor(0x100);
    EXPECT_EQ(s.minUs, 3000u);
    EXPECT_EQ(s.maxUs, 12000u);
}

// ── Multiple IDs — separate statistics ───────────────────────────────────

TEST(CanIntervalAnalyzer, MultipleIds_Separate) {
    CanIntervalAnalyzer a;
    a.addAll(periodicFrames(0x100, 0, 10000, 5));
    a.addAll(periodicFrames(0x200, 0,  5000, 5));

    auto s1 = a.statsFor(0x100);
    auto s2 = a.statsFor(0x200);

    EXPECT_NEAR(s1.meanUs, 10000.0, 1.0);
    EXPECT_NEAR(s2.meanUs,  5000.0, 1.0);
    EXPECT_EQ(a.stats().size(), 2);
}

// ── stats() sorted by ID ─────────────────────────────────────────────────

TEST(CanIntervalAnalyzer, Stats_SortedById) {
    CanIntervalAnalyzer a;
    a.add(makeTimedFrame(0x300, 0));
    a.add(makeTimedFrame(0x100, 0));
    a.add(makeTimedFrame(0x200, 0));

    auto all = a.stats();
    ASSERT_EQ(all.size(), 3);
    EXPECT_LT(all[0].id, all[1].id);
    EXPECT_LT(all[1].id, all[2].id);
}

// ── Out-of-order timestamps → interval skipped ───────────────────────────

TEST(CanIntervalAnalyzer, OutOfOrderTimestamp_Skipped) {
    CanIntervalAnalyzer a;
    a.add(makeTimedFrame(0x100, 10000));
    a.add(makeTimedFrame(0x100,  5000));  // older timestamp → skip
    a.add(makeTimedFrame(0x100, 20000));  // interval = 20000-10000 = 10000

    auto s = a.statsFor(0x100);
    // Only one valid interval (10000)
    EXPECT_NEAR(s.meanUs, 10000.0, 1.0);
}

// ── Gap detection ─────────────────────────────────────────────────────────

TEST(CanIntervalAnalyzer, GapDetected) {
    CanIntervalAnalyzer a;
    // Normal 10ms intervals, then a 100ms gap (10× mean)
    a.add(makeTimedFrame(0x100, 0));
    a.add(makeTimedFrame(0x100, 10000));
    a.add(makeTimedFrame(0x100, 20000));
    a.add(makeTimedFrame(0x100, 120000));  // 100ms gap → gap!

    auto s = a.statsFor(0x100);
    EXPECT_GE(s.gapCount, 1);
}

TEST(CanIntervalAnalyzer, NoGap_AllPeriodic) {
    CanIntervalAnalyzer a;
    a.addAll(periodicFrames(0x100, 0, 10000, 20));

    auto s = a.statsFor(0x100);
    EXPECT_EQ(s.gapCount, 0);
}

// ── reset() clears state ──────────────────────────────────────────────────

TEST(CanIntervalAnalyzer, Reset_ClearsAll) {
    CanIntervalAnalyzer a;
    a.addAll(periodicFrames(0x100, 0, 10000, 5));
    EXPECT_FALSE(a.stats().isEmpty());

    a.reset();
    EXPECT_TRUE(a.stats().isEmpty());
}

// ── addAll() equivalent to loop of add() ─────────────────────────────────

TEST(CanIntervalAnalyzer, AddAll_EquivalentToLoop) {
    auto frames = periodicFrames(0x100, 0, 10000, 8);

    CanIntervalAnalyzer a1, a2;
    a1.addAll(frames);
    for (const auto &f : frames) a2.add(f);

    auto s1 = a1.statsFor(0x100);
    auto s2 = a2.statsFor(0x100);
    EXPECT_EQ(s1.frameCount, s2.frameCount);
    EXPECT_NEAR(s1.meanUs,   s2.meanUs,   1.0);
    EXPECT_NEAR(s1.stdDevUs, s2.stdDevUs, 1.0);
}

// ── summary() contains key fields ────────────────────────────────────────

TEST(CanIntervalAnalyzer, Summary_ContainsId) {
    CanIntervalAnalyzer a;
    a.addAll(periodicFrames(0x1AB, 0, 10000, 5));

    auto s = a.statsFor(0x1AB);
    QString sum = s.summary();
    EXPECT_TRUE(sum.contains("1AB", Qt::CaseInsensitive) ||
                sum.contains("0x1AB", Qt::CaseInsensitive));
    EXPECT_TRUE(sum.contains("5") || sum.contains("frames"));
}

TEST(CanIntervalAnalyzer, Summary_Periodic_ContainsPERIODIC) {
    CanIntervalAnalyzer a;
    a.addAll(periodicFrames(0x100, 0, 10000, 10));

    auto s = a.statsFor(0x100);
    EXPECT_TRUE(s.summary().contains("PERIODIC", Qt::CaseInsensitive));
}

TEST(CanIntervalAnalyzer, Summary_SingleFrame_NotEnough) {
    CanIntervalAnalyzer a;
    a.add(makeTimedFrame(0x100, 0));
    auto s = a.statsFor(0x100);
    EXPECT_FALSE(s.summary().isEmpty());
    EXPECT_TRUE(s.summary().contains("1"));
}

// ── report() contains all IDs ─────────────────────────────────────────────

TEST(CanIntervalAnalyzer, Report_ContainsAllIds) {
    CanIntervalAnalyzer a;
    a.add(makeTimedFrame(0x100, 0));
    a.add(makeTimedFrame(0x200, 0));

    QString r = a.report();
    EXPECT_TRUE(r.contains("100", Qt::CaseInsensitive));
    EXPECT_TRUE(r.contains("200", Qt::CaseInsensitive));
}

// ── Custom gap factor ─────────────────────────────────────────────────────

TEST(CanIntervalAnalyzer, CustomGapFactor) {
    // With gapFactor=2.0, a 2.1× interval should be a gap
    CanIntervalAnalyzer a(2.0);
    a.add(makeTimedFrame(0x100, 0));
    a.add(makeTimedFrame(0x100, 10000));  // establishes mean=10000
    a.add(makeTimedFrame(0x100, 20000));  // +10000
    a.add(makeTimedFrame(0x100, 41000));  // +21000 > 2 * 10000 → gap

    auto s = a.statsFor(0x100);
    EXPECT_GE(s.gapCount, 1);
}
