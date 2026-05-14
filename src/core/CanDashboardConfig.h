#pragma once
#include <QString>
#include <QVector>
#include <QJsonObject>
#include <cstdint>

struct GaugeConfig {
    QString  signalName;
    uint32_t canId       = 0;
    int      style       = 0;     // 0=Bar, 1=Digital, 2=Compact
    bool     useDbcRange = true;
    double   rangeMin    = 0.0;
    double   rangeMax    = 100.0;
    QString  unit;

    QJsonObject        toJson()                         const;
    static GaugeConfig fromJson(const QJsonObject &obj);
};

struct DashboardLayout {
    int                  columns = 3;
    QVector<GaugeConfig> gauges;

    void addGauge(const GaugeConfig &cfg)  { gauges.append(cfg); }
    void removeGauge(int index)            { if (index >= 0 && index < gauges.size()) gauges.remove(index); }
    void clear()                           { gauges.clear(); }

    QJsonObject            toJson()                             const;
    static DashboardLayout fromJson(const QJsonObject &obj);
};
