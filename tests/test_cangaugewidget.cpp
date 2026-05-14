#include <gtest/gtest.h>
#include "core/CanGaugeWidget.h"
#include <QApplication>
#include <QDateTime>
#include <thread>
#include <chrono>

class GaugeTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        if (!QApplication::instance()) {
            static int   argc   = 1;
            static char *argv[] = { (char*)"test", nullptr };
            new QApplication(argc, argv);
        }
    }
};

TEST_F(GaugeTest, DefaultValueIsZero) {
    CanGaugeWidget g(CanGaugeWidget::Bar);
    EXPECT_DOUBLE_EQ(g.currentValue(), 0.0);
}

TEST_F(GaugeTest, SetValueUpdatesCurrentValue) {
    CanGaugeWidget g(CanGaugeWidget::Bar);
    g.setRange(0, 100);
    g.setValue(42.5);
    EXPECT_DOUBLE_EQ(g.currentValue(), 42.5);
}

TEST_F(GaugeTest, IsOutOfRangeWhenAboveMax) {
    CanGaugeWidget g(CanGaugeWidget::Digital);
    g.setRange(0.0, 100.0);
    g.setValue(101.0);
    EXPECT_TRUE(g.isOutOfRange());
}

TEST_F(GaugeTest, IsOutOfRangeWhenBelowMin) {
    CanGaugeWidget g(CanGaugeWidget::Digital);
    g.setRange(10.0, 100.0);
    g.setValue(5.0);
    EXPECT_TRUE(g.isOutOfRange());
}

TEST_F(GaugeTest, InRangeWhenValueInBounds) {
    CanGaugeWidget g(CanGaugeWidget::Compact);
    g.setRange(0.0, 200.0);
    g.setValue(100.0);
    EXPECT_FALSE(g.isOutOfRange());
}

TEST_F(GaugeTest, MinMaxObservedTracked) {
    CanGaugeWidget g(CanGaugeWidget::Bar);
    g.setRange(0.0, 1000.0);
    g.setValue(50.0);
    g.setValue(300.0);
    g.setValue(10.0);
    EXPECT_DOUBLE_EQ(g.minObserved(), 10.0);
    EXPECT_DOUBLE_EQ(g.maxObserved(), 300.0);
}

TEST_F(GaugeTest, StalenessCheckDoesNotCrash) {
    CanGaugeWidget g(CanGaugeWidget::Bar);
    g.setRange(0, 100);
    g.setStaleTimeout(5);
    g.setValue(50.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    g.checkStaleness();
    g.checkStaleness();
}

TEST_F(GaugeTest, CheckStalenessWithoutPriorValue) {
    CanGaugeWidget g(CanGaugeWidget::Bar);
    g.checkStaleness();
}

TEST_F(GaugeTest, RangeAtEquality) {
    CanGaugeWidget g(CanGaugeWidget::Bar);
    g.setRange(50.0, 50.0);
    g.setValue(50.0);
    EXPECT_FALSE(g.isOutOfRange());
}

TEST_F(GaugeTest, FirstValueSeedsMinMaxObserved) {
    CanGaugeWidget g(CanGaugeWidget::Bar);
    g.setRange(0.0, 1000.0);
    g.setValue(75.0);
    EXPECT_DOUBLE_EQ(g.minObserved(), 75.0);
    EXPECT_DOUBLE_EQ(g.maxObserved(), 75.0);
}

TEST_F(GaugeTest, RecordValueUpdatesMinMax) {
    CanGaugeWidget g(CanGaugeWidget::Bar);
    g.setRange(0.0, 1000.0);
    g.recordValue(200.0);
    g.recordValue(50.0);
    g.recordValue(800.0);
    EXPECT_DOUBLE_EQ(g.minObserved(), 50.0);
    EXPECT_DOUBLE_EQ(g.maxObserved(), 800.0);
}

TEST_F(GaugeTest, RecordValueDoesNotChangeCurrentValue) {
    CanGaugeWidget g(CanGaugeWidget::Bar);
    g.setRange(0.0, 100.0);
    g.setValue(10.0);
    g.recordValue(99.0); // should not update m_value
    EXPECT_DOUBLE_EQ(g.currentValue(), 10.0);
}

TEST_F(GaugeTest, SetRangeInitializesObservedWhenNoValues) {
    CanGaugeWidget g(CanGaugeWidget::Bar);
    g.setRange(20.0, 80.0);
    // No values recorded yet → minObserved/maxObserved seeded from range
    EXPECT_DOUBLE_EQ(g.minObserved(), 20.0);
    EXPECT_DOUBLE_EQ(g.maxObserved(), 80.0);
}

TEST_F(GaugeTest, DigitalStyleConstructsAndTracksValue) {
    CanGaugeWidget g(CanGaugeWidget::Digital);
    g.setRange(0.0, 5000.0);
    g.setValue(3000.0);
    EXPECT_DOUBLE_EQ(g.currentValue(), 3000.0);
    EXPECT_FALSE(g.isOutOfRange());
}

TEST_F(GaugeTest, CompactStyleConstructsAndTracksValue) {
    CanGaugeWidget g(CanGaugeWidget::Compact);
    g.setRange(-10.0, 10.0);
    g.setValue(-5.0);
    EXPECT_DOUBLE_EQ(g.currentValue(), -5.0);
    EXPECT_FALSE(g.isOutOfRange());
}

TEST_F(GaugeTest, NegativeRangeOutOfRange) {
    CanGaugeWidget g(CanGaugeWidget::Bar);
    g.setRange(-100.0, -10.0);
    g.setValue(-50.0);
    EXPECT_FALSE(g.isOutOfRange());
    g.setValue(-5.0); // above max
    EXPECT_TRUE(g.isOutOfRange());
}
