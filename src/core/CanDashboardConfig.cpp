#include "CanDashboardConfig.h"
#include <QJsonArray>

QJsonObject GaugeConfig::toJson() const {
    QJsonObject o;
    o["signal"]      = signalName;
    o["canId"]       = static_cast<int>(canId);
    o["style"]       = style;
    o["useDbcRange"] = useDbcRange;
    o["rangeMin"]    = rangeMin;
    o["rangeMax"]    = rangeMax;
    o["unit"]        = unit;
    return o;
}

GaugeConfig GaugeConfig::fromJson(const QJsonObject &obj) {
    GaugeConfig c;
    c.signalName  = obj.value("signal").toString();
    c.canId       = static_cast<uint32_t>(obj.value("canId").toInt(0));
    c.style       = obj.value("style").toInt(0);
    c.useDbcRange = obj.value("useDbcRange").toBool(true);
    c.rangeMin    = obj.value("rangeMin").toDouble(0.0);
    c.rangeMax    = obj.value("rangeMax").toDouble(100.0);
    c.unit        = obj.value("unit").toString();
    return c;
}

QJsonObject DashboardLayout::toJson() const {
    QJsonObject o;
    o["columns"] = columns;
    QJsonArray arr;
    for (const auto &g : gauges)
        arr.append(g.toJson());
    o["gauges"] = arr;
    return o;
}

DashboardLayout DashboardLayout::fromJson(const QJsonObject &obj) {
    DashboardLayout d;
    d.columns = obj.value("columns").toInt(3);
    if (d.columns < 2 || d.columns > 6) d.columns = 3;
    const auto arr = obj.value("gauges").toArray();
    for (const auto &val : arr)
        d.gauges.append(GaugeConfig::fromJson(val.toObject()));
    return d;
}
