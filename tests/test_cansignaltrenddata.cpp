#include <gtest/gtest.h>
#include "core/CanSignalTrendWidget.h"

// ── CanSignalTrendData ────────────────────────────────────────────────────────

TEST(CanSignalTrendDataTest, InitiallyEmpty) {
    CanSignalTrendData d;
    EXPECT_TRUE(d.isEmpty());
    EXPECT_EQ(d.size(), 0);
}

TEST(CanSignalTrendDataTest, AddSample_GrowsSize) {
    CanSignalTrendData d;
    d.addSample(0.0, 100.0);
    d.addSample(1.0, 200.0);
    EXPECT_EQ(d.size(), 2);
    EXPECT_FALSE(d.isEmpty());
}

TEST(CanSignalTrendDataTest, AddSample_CorrectValues) {
    CanSignalTrendData d;
    d.addSample(1.5, 42.0);
    ASSERT_EQ(d.size(), 1);
    EXPECT_DOUBLE_EQ(d.points().first().timeS, 1.5);
    EXPECT_DOUBLE_EQ(d.points().first().value, 42.0);
}

TEST(CanSignalTrendDataTest, MinValue_Single) {
    CanSignalTrendData d;
    d.addSample(0.0, 55.0);
    EXPECT_DOUBLE_EQ(d.minValue(), 55.0);
}

TEST(CanSignalTrendDataTest, MaxValue_Single) {
    CanSignalTrendData d;
    d.addSample(0.0, 77.0);
    EXPECT_DOUBLE_EQ(d.maxValue(), 77.0);
}

TEST(CanSignalTrendDataTest, MinMaxValue_Multiple) {
    CanSignalTrendData d;
    d.addSample(0.0, 10.0);
    d.addSample(1.0, 50.0);
    d.addSample(2.0, 30.0);
    EXPECT_DOUBLE_EQ(d.minValue(), 10.0);
    EXPECT_DOUBLE_EQ(d.maxValue(), 50.0);
}

TEST(CanSignalTrendDataTest, MinValue_Empty_ReturnsZero) {
    CanSignalTrendData d;
    EXPECT_DOUBLE_EQ(d.minValue(), 0.0);
}

TEST(CanSignalTrendDataTest, MaxValue_Empty_ReturnsOne) {
    CanSignalTrendData d;
    EXPECT_DOUBLE_EQ(d.maxValue(), 1.0);
}

TEST(CanSignalTrendDataTest, TrimToWindow_RemovesOldPoints) {
    CanSignalTrendData d;
    // Add samples from t=0 to t=60
    for (int i = 0; i <= 60; ++i)
        d.addSample(static_cast<double>(i), static_cast<double>(i));

    d.trimToWindow(30.0);  // keep only last 30s

    EXPECT_GE(d.size(), 1);
    // After trim, the oldest point should be >= t=30
    EXPECT_GE(d.points().first().timeS, 30.0);
}

TEST(CanSignalTrendDataTest, TrimToWindow_KeepsRecentPoints) {
    CanSignalTrendData d;
    d.addSample(0.0,  10.0);
    d.addSample(10.0, 20.0);
    d.addSample(20.0, 30.0);
    d.addSample(30.0, 40.0);

    d.trimToWindow(15.0);  // keep last 15s: t >= 30 - 15 = 15

    // Points at t=20 and t=30 should remain
    EXPECT_GE(d.size(), 1);
    for (const auto &p : d.points())
        EXPECT_GE(p.timeS, 15.0);
}

TEST(CanSignalTrendDataTest, TrimToWindow_Empty_NoOp) {
    CanSignalTrendData d;
    EXPECT_NO_FATAL_FAILURE(d.trimToWindow(30.0));
    EXPECT_TRUE(d.isEmpty());
}

TEST(CanSignalTrendDataTest, TrimToWindow_AllWithinWindow_NothingRemoved) {
    CanSignalTrendData d;
    d.addSample(1.0, 10.0);
    d.addSample(2.0, 20.0);
    d.addSample(3.0, 30.0);

    d.trimToWindow(60.0);  // window larger than all data

    EXPECT_EQ(d.size(), 3);
}

TEST(CanSignalTrendDataTest, Clear_RemovesAll) {
    CanSignalTrendData d;
    d.addSample(0.0, 1.0);
    d.addSample(1.0, 2.0);
    d.clear();
    EXPECT_TRUE(d.isEmpty());
    EXPECT_EQ(d.size(), 0);
}

TEST(CanSignalTrendDataTest, PointsOrderPreserved) {
    CanSignalTrendData d;
    d.addSample(0.0,  100.0);
    d.addSample(0.5,  200.0);
    d.addSample(1.0,  150.0);

    const auto &pts = d.points();
    ASSERT_EQ(pts.size(), 3);
    EXPECT_DOUBLE_EQ(pts[0].value, 100.0);
    EXPECT_DOUBLE_EQ(pts[1].value, 200.0);
    EXPECT_DOUBLE_EQ(pts[2].value, 150.0);
}

TEST(CanSignalTrendDataTest, NegativeValues_MinMaxCorrect) {
    CanSignalTrendData d;
    d.addSample(0.0, -40.0);  // e.g. temperature -40°C
    d.addSample(1.0,  85.0);
    EXPECT_DOUBLE_EQ(d.minValue(), -40.0);
    EXPECT_DOUBLE_EQ(d.maxValue(),  85.0);
}

TEST(CanSignalTrendDataTest, LargeDataSet_TrimEfficient) {
    CanSignalTrendData d;
    // Simulate 1 hour of data at 10 Hz = 36000 points
    for (int i = 0; i < 36000; ++i)
        d.addSample(i * 0.1, static_cast<double>(i % 1000));

    d.trimToWindow(30.0);  // keep last 30s = 300 points

    EXPECT_LE(d.size(), 310);  // at most ~300 + rounding
    EXPECT_GE(d.points().first().timeS, 3600.0 - 30.0 - 0.1);
}
