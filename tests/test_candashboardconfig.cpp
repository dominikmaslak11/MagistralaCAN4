#include <gtest/gtest.h>
#include "core/CanDashboardConfig.h"
#include <QJsonDocument>
#include <QJsonArray>

// ── GaugeConfig ─────────────────────────────────────────────────────────────

TEST(GaugeConfig, DefaultValues) {
    GaugeConfig c;
    EXPECT_EQ(c.signalName, QString());
    EXPECT_EQ(c.canId, 0u);
    EXPECT_EQ(c.style, 0);
    EXPECT_TRUE(c.useDbcRange);
    EXPECT_DOUBLE_EQ(c.rangeMin, 0.0);
    EXPECT_DOUBLE_EQ(c.rangeMax, 100.0);
    EXPECT_EQ(c.unit, QString());
}

TEST(GaugeConfig, RoundTripBasic) {
    GaugeConfig c;
    c.signalName  = "EngineSpeed";
    c.canId       = 0x100;
    c.style       = 1;
    c.useDbcRange = true;
    c.rangeMin    = 0.0;
    c.rangeMax    = 8000.0;
    c.unit        = "rpm";

    const GaugeConfig c2 = GaugeConfig::fromJson(c.toJson());
    EXPECT_EQ(c2.signalName, c.signalName);
    EXPECT_EQ(c2.canId,      c.canId);
    EXPECT_EQ(c2.style,      c.style);
    EXPECT_EQ(c2.useDbcRange, c.useDbcRange);
    EXPECT_DOUBLE_EQ(c2.rangeMin, c.rangeMin);
    EXPECT_DOUBLE_EQ(c2.rangeMax, c.rangeMax);
    EXPECT_EQ(c2.unit, c.unit);
}

TEST(GaugeConfig, StyleCompact) {
    GaugeConfig c;
    c.style = 2;
    EXPECT_EQ(GaugeConfig::fromJson(c.toJson()).style, 2);
}

TEST(GaugeConfig, UseDbcRangeFalse) {
    GaugeConfig c;
    c.useDbcRange = false;
    c.rangeMin = -100.0;
    c.rangeMax = 100.0;
    const auto c2 = GaugeConfig::fromJson(c.toJson());
    EXPECT_FALSE(c2.useDbcRange);
    EXPECT_DOUBLE_EQ(c2.rangeMin, -100.0);
    EXPECT_DOUBLE_EQ(c2.rangeMax,  100.0);
}

TEST(GaugeConfig, NegativeRange) {
    GaugeConfig c;
    c.rangeMin = -273.15;
    c.rangeMax = 0.0;
    const auto c2 = GaugeConfig::fromJson(c.toJson());
    EXPECT_DOUBLE_EQ(c2.rangeMin, -273.15);
    EXPECT_DOUBLE_EQ(c2.rangeMax,    0.0);
}

TEST(GaugeConfig, UnitPreserved) {
    GaugeConfig c;
    c.unit = "km/h";
    EXPECT_EQ(GaugeConfig::fromJson(c.toJson()).unit, QString("km/h"));
}

TEST(GaugeConfig, CanIdExtended) {
    GaugeConfig c;
    c.canId = 0x1FFFFFFF;
    EXPECT_EQ(GaugeConfig::fromJson(c.toJson()).canId, 0x1FFFFFFFu);
}

TEST(GaugeConfig, EmptySignalName) {
    GaugeConfig c;
    EXPECT_EQ(GaugeConfig::fromJson(c.toJson()).signalName, QString());
}

// ── DashboardLayout ──────────────────────────────────────────────────────────

TEST(DashboardLayout, DefaultValues) {
    DashboardLayout d;
    EXPECT_EQ(d.columns, 3);
    EXPECT_TRUE(d.gauges.isEmpty());
}

TEST(DashboardLayout, EmptyRoundTrip) {
    DashboardLayout d;
    const auto d2 = DashboardLayout::fromJson(d.toJson());
    EXPECT_EQ(d2.columns, 3);
    EXPECT_TRUE(d2.gauges.isEmpty());
}

TEST(DashboardLayout, ColumnsPreserved) {
    DashboardLayout d;
    d.columns = 4;
    EXPECT_EQ(DashboardLayout::fromJson(d.toJson()).columns, 4);
}

TEST(DashboardLayout, ColumnsClampedOnLoad) {
    QJsonObject obj;
    obj["columns"] = 99;
    obj["gauges"]  = QJsonArray{};
    const auto d = DashboardLayout::fromJson(obj);
    EXPECT_EQ(d.columns, 3);
}

TEST(DashboardLayout, ColumnsTooSmallClamped) {
    QJsonObject obj;
    obj["columns"] = 0;
    obj["gauges"]  = QJsonArray{};
    const auto d = DashboardLayout::fromJson(obj);
    EXPECT_EQ(d.columns, 3);
}

TEST(DashboardLayout, WithGaugesRoundTrip) {
    DashboardLayout d;
    d.columns = 2;

    GaugeConfig g1;
    g1.signalName = "Speed";   g1.canId = 0x100; g1.style = 0; g1.rangeMax = 200.0; g1.unit = "km/h";
    GaugeConfig g2;
    g2.signalName = "RPM";     g2.canId = 0x200; g2.style = 1; g2.rangeMax = 8000.0;
    GaugeConfig g3;
    g3.signalName = "BattVolt"; g3.canId = 0x300; g3.style = 2;
    g3.useDbcRange = false; g3.rangeMin = 10.0; g3.rangeMax = 15.0; g3.unit = "V";

    d.gauges = {g1, g2, g3};

    const auto d2 = DashboardLayout::fromJson(d.toJson());
    ASSERT_EQ(d2.gauges.size(), 3);
    EXPECT_EQ(d2.columns,                    2);
    EXPECT_EQ(d2.gauges[0].signalName,       "Speed");
    EXPECT_EQ(d2.gauges[0].canId,            0x100u);
    EXPECT_EQ(d2.gauges[0].style,            0);
    EXPECT_DOUBLE_EQ(d2.gauges[0].rangeMax,  200.0);
    EXPECT_EQ(d2.gauges[0].unit,             QString("km/h"));
    EXPECT_EQ(d2.gauges[1].signalName,       "RPM");
    EXPECT_EQ(d2.gauges[1].style,            1);
    EXPECT_DOUBLE_EQ(d2.gauges[1].rangeMax,  8000.0);
    EXPECT_EQ(d2.gauges[2].signalName,       "BattVolt");
    EXPECT_FALSE(d2.gauges[2].useDbcRange);
    EXPECT_DOUBLE_EQ(d2.gauges[2].rangeMin,  10.0);
    EXPECT_EQ(d2.gauges[2].unit,             QString("V"));
}

TEST(DashboardLayout, AddGauge) {
    DashboardLayout d;
    GaugeConfig g; g.signalName = "X";
    d.addGauge(g);
    EXPECT_EQ(d.gauges.size(), 1);
    d.addGauge(g);
    EXPECT_EQ(d.gauges.size(), 2);
}

TEST(DashboardLayout, RemoveGauge) {
    DashboardLayout d;
    GaugeConfig ga; ga.signalName = "A";
    GaugeConfig gb; gb.signalName = "B";
    GaugeConfig gc; gc.signalName = "C";
    d.gauges = {ga, gb, gc};

    d.removeGauge(1); // remove "B"
    ASSERT_EQ(d.gauges.size(), 2);
    EXPECT_EQ(d.gauges[0].signalName, "A");
    EXPECT_EQ(d.gauges[1].signalName, "C");
}

TEST(DashboardLayout, RemoveGaugeOutOfBoundsIsNoop) {
    DashboardLayout d;
    GaugeConfig g; g.signalName = "X";
    d.addGauge(g);
    d.removeGauge(5);   // out of bounds
    d.removeGauge(-1);  // negative
    EXPECT_EQ(d.gauges.size(), 1);
}

TEST(DashboardLayout, ClearEmpties) {
    DashboardLayout d;
    GaugeConfig g; g.signalName = "X";
    d.addGauge(g); d.addGauge(g);
    d.clear();
    EXPECT_TRUE(d.gauges.isEmpty());
    EXPECT_EQ(d.columns, 3); // columns unchanged
}

TEST(DashboardLayout, MissingGaugesFieldDefaultsEmpty) {
    QJsonObject obj;
    obj["columns"] = 3;
    // no "gauges" key
    const auto d = DashboardLayout::fromJson(obj);
    EXPECT_TRUE(d.gauges.isEmpty());
}

TEST(DashboardLayout, UnknownJsonFieldsIgnored) {
    QJsonObject obj;
    obj["columns"]     = 3;
    obj["gauges"]      = QJsonArray{};
    obj["unknownKey"]  = "someValue";
    EXPECT_NO_FATAL_FAILURE(DashboardLayout::fromJson(obj));
}

TEST(DashboardLayout, JsonDocumentRoundTrip) {
    DashboardLayout d;
    d.columns = 5;
    GaugeConfig g; g.signalName = "Torque"; g.canId = 0x555; g.style = 2;
    d.addGauge(g);

    const QByteArray bytes = QJsonDocument(d.toJson()).toJson(QJsonDocument::Compact);
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    ASSERT_TRUE(doc.isObject());
    const auto d2 = DashboardLayout::fromJson(doc.object());
    EXPECT_EQ(d2.columns, 5);
    ASSERT_EQ(d2.gauges.size(), 1);
    EXPECT_EQ(d2.gauges[0].signalName, "Torque");
    EXPECT_EQ(d2.gauges[0].canId, 0x555u);
}
