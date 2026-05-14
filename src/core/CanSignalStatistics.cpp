#include "CanSignalStatistics.h"
#include <algorithm>
#include <cmath>

void CanSignalStatistics::add(const CanFrame &frame) {
    if (!m_dbc) return;

    auto decoded = m_dbc->decodeSignals(frame.id, frame.data.data(), frame.dlc);
    if (decoded.isEmpty()) return;

    auto msg = m_dbc->messageForId(frame.id);

    for (auto it = decoded.cbegin(); it != decoded.cend(); ++it) {
        const QString &name = it.key();
        double val = it.value();

        auto &acc = m_accum[name];
        if (acc.count == 0) {
            // Find unit from DBC definition
            for (const auto &sig : msg.sigList) {
                if (sig.name == name) {
                    acc.unit = sig.unit;
                    break;
                }
            }
        }

        ++acc.count;

        // Welford online mean + variance
        double delta  = val - acc.wMean;
        acc.wMean    += delta / static_cast<double>(acc.count);
        double delta2 = val - acc.wMean;
        acc.wM2      += delta * delta2;

        if (val < acc.minVal) acc.minVal = val;
        if (val > acc.maxVal) acc.maxVal = val;

        if (acc.count <= static_cast<uint64_t>(Accumulator::MAX_SAMPLES))
            acc.samples.append(val);
    }
}

void CanSignalStatistics::addAll(const QVector<CanFrame> &frames) {
    for (const auto &f : frames) add(f);
}

void CanSignalStatistics::reset() {
    m_accum.clear();
}

int SignalStats::bucketFor(double val) const {
    if (sampleCount < 2 || maxVal <= minVal) return 0;
    double range = maxVal - minVal;
    int b = static_cast<int>((val - minVal) / range * BUCKET_COUNT);
    return std::clamp(b, 0, BUCKET_COUNT - 1);
}

SignalStats CanSignalStatistics::finalise(const QString &name,
                                           const Accumulator &acc) const {
    SignalStats s;
    s.signalName  = name;
    s.unit        = acc.unit;
    s.sampleCount = acc.count;
    if (acc.count == 0) return s;

    s.minVal = acc.minVal;
    s.maxVal = acc.maxVal;
    s.mean   = acc.wMean;
    s.stdDev = (acc.count >= 2) ? std::sqrt(acc.wM2 / static_cast<double>(acc.count - 1)) : 0.0;

    // Build histogram from stored samples
    s.histogram.fill(0);
    for (double v : acc.samples)
        ++s.histogram[s.bucketFor(v)];

    return s;
}

QVector<SignalStats> CanSignalStatistics::allStats() const {
    QVector<SignalStats> result;
    for (auto it = m_accum.cbegin(); it != m_accum.cend(); ++it)
        result.append(finalise(it.key(), it.value()));
    std::sort(result.begin(), result.end(),
              [](const SignalStats &a, const SignalStats &b) {
                  return a.signalName < b.signalName;
              });
    return result;
}

SignalStats CanSignalStatistics::statsFor(const QString &name) const {
    auto it = m_accum.find(name);
    if (it == m_accum.end()) return {};
    return finalise(name, it.value());
}

QString SignalStats::summary() const {
    if (sampleCount == 0)
        return signalName + ": no samples";
    return QString("%1: n=%2 | min=%3 | max=%4 | mean=%5 | std=%6 %7")
           .arg(signalName)
           .arg(sampleCount)
           .arg(minVal, 0, 'f', 3)
           .arg(maxVal, 0, 'f', 3)
           .arg(mean,   0, 'f', 3)
           .arg(stdDev, 0, 'f', 3)
           .arg(unit.isEmpty() ? "" : unit);
}

QString CanSignalStatistics::report() const {
    auto all = allStats();
    if (all.isEmpty()) return "No signal data.";
    QString out;
    for (const auto &s : all)
        out += s.summary() + "\n";
    return out;
}
