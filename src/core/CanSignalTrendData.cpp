#include "CanSignalTrendData.h"
#include <algorithm>
#include <limits>

void CanSignalTrendData::addSample(double timeS, double value) {
    m_pts.append({timeS, value});
}

void CanSignalTrendData::trimToWindow(double windowS) {
    if (m_pts.isEmpty()) return;
    double cutoff = m_pts.last().timeS - windowS;
    while (!m_pts.isEmpty() && m_pts.first().timeS < cutoff)
        m_pts.removeFirst();
}

void CanSignalTrendData::clear() {
    m_pts.clear();
}

double CanSignalTrendData::minValue() const {
    if (m_pts.isEmpty()) return 0;
    double v = std::numeric_limits<double>::max();
    for (const auto &p : m_pts) v = std::min(v, p.value);
    return v;
}

double CanSignalTrendData::maxValue() const {
    if (m_pts.isEmpty()) return 1;
    double v = std::numeric_limits<double>::lowest();
    for (const auto &p : m_pts) v = std::max(v, p.value);
    return v;
}
