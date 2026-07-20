#pragma once
#include <QVector>

struct TrendPoint {
    double timeS  = 0;
    double value  = 0;
};

class CanSignalTrendData {
public:
    void addSample(double timeS, double value);
    void trimToWindow(double windowS);
    void clear();

    const QVector<TrendPoint> &points() const { return m_pts; }
    double minValue() const;
    double maxValue() const;
    int    size()     const { return static_cast<int>(m_pts.size()); }
    bool   isEmpty()  const { return m_pts.isEmpty(); }

private:
    QVector<TrendPoint> m_pts;
};
