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
