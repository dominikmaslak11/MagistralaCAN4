/**
 * @file test_cansignalstatistics.cpp
 * @brief Unit tests for CanSignalStatistics.
 */
#include <gtest/gtest.h>
#include "core/CanSignalStatistics.h"
#include "core/DbcParser.h"
#include <QCoreApplication>
#include <QTemporaryFile>
#include <QTextStream>
#include <cmath>

// ── DBC fixture ──────────────────────────────────────────────────────────────

static DbcParser *makeDbc(const QString &content) {
    QTemporaryFile tmp;
    tmp.setAutoRemove(false);
    if (!tmp.open()) return nullptr;
    QTextStream ts(&tmp);
    ts << content;
    ts.flush();
    QString path = tmp.fileName();
    tmp.close();

    auto *p = new DbcParser;
    if (!p->load(path)) { delete p; return nullptr; }
    QFile::remove(path);
    return p;
}

// DBC: single message, two signals
static const QString k_Dbc = QStringLiteral(
    "VERSION \"\"\n\n"
    "BO_ 256 Eng: 8 ECU\n"
    " SG_ RPM    : 0|16@1+ (0.125,0)   [0|8031.875] \"rpm\" ECU\n"
    " SG_ Throttle : 16|8@1+ (0.5,0) [0|100] \"%\" ECU\n"
);

// ── Helpers ──────────────────────────────────────────────────────────────────

static CanFrame makeEngFrame(uint16_t rpm_raw, uint8_t throttle_raw) {
    CanFrame f{};
    f.id  = 256;
    f.dlc = 4;
    // RPM at bytes 0-1 little-endian
    f.data[0] = static_cast<uint8_t>(rpm_raw & 0xFF);
    f.data[1] = static_cast<uint8_t>(rpm_raw >> 8);
    f.data[2] = throttle_raw;
    return f;
}

// ── Empty ─────────────────────────────────────────────────────────────────

TEST(CanSignalStatistics, Empty_AllStatsEmpty) {
    CanSignalStatistics s;
    EXPECT_TRUE(s.allStats().isEmpty());
}

TEST(CanSignalStatistics, Empty_Report) {
    CanSignalStatistics s;
    EXPECT_TRUE(s.report().contains("No signal data"));
}

TEST(CanSignalStatistics, NoDbc_NoStats) {
    CanSignalStatistics s;
    // No DBC set — add() should be a no-op
    CanFrame f{};
    f.id = 256; f.dlc = 4;
    s.add(f);
    EXPECT_TRUE(s.allStats().isEmpty());
}

// ── Single value ──────────────────────────────────────────────────────────

TEST(CanSignalStatistics, SingleFrame_SampleCount1) {
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);

    CanSignalStatistics stats;
    stats.setDbc(dbc);
    stats.add(makeEngFrame(8000, 100));  // RPM=8000*0.125=1000, Throttle=100*0.5=50

    auto s = stats.statsFor("RPM");
    EXPECT_EQ(s.sampleCount, 1u);
    EXPECT_NEAR(s.minVal, 1000.0, 0.001);
    EXPECT_NEAR(s.maxVal, 1000.0, 0.001);
    EXPECT_NEAR(s.mean,   1000.0, 0.001);
    EXPECT_DOUBLE_EQ(s.stdDev, 0.0);

    delete dbc;
}

// ── Mean of identical values ──────────────────────────────────────────────

TEST(CanSignalStatistics, IdenticalValues_ZeroStdDev) {
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);

    CanSignalStatistics stats;
    stats.setDbc(dbc);
    for (int i = 0; i < 5; ++i)
        stats.add(makeEngFrame(8000, 100));

    auto s = stats.statsFor("RPM");
    EXPECT_EQ(s.sampleCount, 5u);
    EXPECT_NEAR(s.mean, 1000.0, 0.001);
    EXPECT_NEAR(s.stdDev, 0.0, 0.001);

    delete dbc;
}

// ── Two values — mean and stdDev correct ─────────────────────────────────

TEST(CanSignalStatistics, TwoValues_MeanAndStdDev) {
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);

    CanSignalStatistics stats;
    stats.setDbc(dbc);
    // RPM values: 1000, 2000 → mean=1500, stdDev=707.1...
    stats.add(makeEngFrame(8000,  0));   // 1000 rpm
    stats.add(makeEngFrame(16000, 0));   // 2000 rpm

    auto s = stats.statsFor("RPM");
    EXPECT_EQ(s.sampleCount, 2u);
    EXPECT_NEAR(s.mean,   1500.0, 0.1);
    EXPECT_NEAR(s.stdDev, std::sqrt(500000.0), 1.0);  // sqrt(var) where var = (1000-1500)^2 + (2000-1500)^2 / 1

    delete dbc;
}

// ── min/max tracked ───────────────────────────────────────────────────────

TEST(CanSignalStatistics, MinMaxTracked) {
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);

    CanSignalStatistics stats;
    stats.setDbc(dbc);
    stats.add(makeEngFrame(4000, 0));   // 500 rpm
    stats.add(makeEngFrame(8000, 0));   // 1000 rpm
    stats.add(makeEngFrame(40000, 0));  // 5000 rpm

    auto s = stats.statsFor("RPM");
    EXPECT_NEAR(s.minVal, 500.0, 0.1);
    EXPECT_NEAR(s.maxVal, 5000.0, 0.1);

    delete dbc;
}

// ── Both signals decoded ──────────────────────────────────────────────────

TEST(CanSignalStatistics, BothSignals_Decoded) {
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);

    CanSignalStatistics stats;
    stats.setDbc(dbc);
    stats.add(makeEngFrame(8000, 100));  // 1000 rpm, 50%

    auto rpm = stats.statsFor("RPM");
    auto thr = stats.statsFor("Throttle");
    EXPECT_EQ(rpm.sampleCount, 1u);
    EXPECT_EQ(thr.sampleCount, 1u);
    EXPECT_NEAR(thr.mean, 50.0, 0.01);

    delete dbc;
}

// ── allStats() sorted by signal name ─────────────────────────────────────

TEST(CanSignalStatistics, AllStats_SortedByName) {
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);

    CanSignalStatistics stats;
    stats.setDbc(dbc);
    stats.add(makeEngFrame(8000, 100));

    auto all = stats.allStats();
    ASSERT_GE(all.size(), 2);
    EXPECT_LT(all[0].signalName, all[1].signalName);

    delete dbc;
}

// ── unit preserved ────────────────────────────────────────────────────────

TEST(CanSignalStatistics, Unit_Preserved) {
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);

    CanSignalStatistics stats;
    stats.setDbc(dbc);
    stats.add(makeEngFrame(8000, 100));

    EXPECT_EQ(stats.statsFor("RPM").unit, "rpm");
    EXPECT_EQ(stats.statsFor("Throttle").unit, "%");

    delete dbc;
}

// ── Unknown signal returns empty ──────────────────────────────────────────

TEST(CanSignalStatistics, UnknownSignal_Empty) {
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);

    CanSignalStatistics stats;
    stats.setDbc(dbc);
    auto s = stats.statsFor("NoSuchSignal");
    EXPECT_EQ(s.sampleCount, 0u);

    delete dbc;
}

// ── reset() clears state ──────────────────────────────────────────────────

TEST(CanSignalStatistics, Reset_ClearsAll) {
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);

    CanSignalStatistics stats;
    stats.setDbc(dbc);
    stats.add(makeEngFrame(8000, 100));
    EXPECT_FALSE(stats.allStats().isEmpty());

    stats.reset();
    EXPECT_TRUE(stats.allStats().isEmpty());

    delete dbc;
}

// ── valid() flag ──────────────────────────────────────────────────────────

TEST(CanSignalStatistics, Valid_FalseForOneSample) {
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);

    CanSignalStatistics stats;
    stats.setDbc(dbc);
    stats.add(makeEngFrame(8000, 100));
    EXPECT_FALSE(stats.statsFor("RPM").valid());

    stats.add(makeEngFrame(8000, 100));
    EXPECT_TRUE(stats.statsFor("RPM").valid());

    delete dbc;
}

// ── histogram buckets filled ──────────────────────────────────────────────

TEST(CanSignalStatistics, Histogram_BucketsFilled) {
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);

    CanSignalStatistics stats;
    stats.setDbc(dbc);
    // Add RPM values spread across range
    for (int i = 0; i <= 10; ++i)
        stats.add(makeEngFrame(static_cast<uint16_t>(i * 800), 0));

    auto s = stats.statsFor("RPM");
    uint64_t total = 0;
    for (auto b : s.histogram) total += b;
    EXPECT_EQ(total, s.sampleCount);

    delete dbc;
}

// ── bucketFor edge cases ──────────────────────────────────────────────────

TEST(CanSignalStatistics, BucketFor_MinMapsTo0_MaxMapsToLast) {
    SignalStats s;
    s.sampleCount = 100;
    s.minVal = 0.0;
    s.maxVal = 100.0;

    EXPECT_EQ(s.bucketFor(0.0),   0);
    EXPECT_EQ(s.bucketFor(100.0), SignalStats::BUCKET_COUNT - 1);
    EXPECT_EQ(s.bucketFor(50.0),  SignalStats::BUCKET_COUNT / 2);
}

TEST(CanSignalStatistics, BucketFor_SingleValueRange_Returns0) {
    SignalStats s;
    s.sampleCount = 1;
    s.minVal = 42.0;
    s.maxVal = 42.0;  // zero range
    EXPECT_EQ(s.bucketFor(42.0), 0);
}

// ── summary() output ──────────────────────────────────────────────────────

TEST(CanSignalStatistics, Summary_ContainsNameAndCount) {
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);

    CanSignalStatistics stats;
    stats.setDbc(dbc);
    for (int i = 0; i < 3; ++i)
        stats.add(makeEngFrame(8000, 100));

    auto s = stats.statsFor("RPM");
    QString sum = s.summary();
    EXPECT_TRUE(sum.contains("RPM"));
    EXPECT_TRUE(sum.contains("3"));

    delete dbc;
}

// ── addAll equivalent to loop ─────────────────────────────────────────────

TEST(CanSignalStatistics, AddAll_EquivalentToLoop) {
    DbcParser *dbc = makeDbc(k_Dbc);
    ASSERT_NE(dbc, nullptr);

    QVector<CanFrame> frames;
    for (int i = 1; i <= 5; ++i)
        frames.append(makeEngFrame(static_cast<uint16_t>(i * 1600), 0));

    CanSignalStatistics s1, s2;
    s1.setDbc(dbc);
    s2.setDbc(dbc);
    s1.addAll(frames);
    for (const auto &f : frames) s2.add(f);

    auto r1 = s1.statsFor("RPM");
    auto r2 = s2.statsFor("RPM");
    EXPECT_EQ(r1.sampleCount, r2.sampleCount);
    EXPECT_NEAR(r1.mean,   r2.mean,   0.001);
    EXPECT_NEAR(r1.stdDev, r2.stdDev, 0.001);

    delete dbc;
}
