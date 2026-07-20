#include "CanIntervalAnalyzer.h"
#include <algorithm>
#include <cmath>

void CanIntervalAnalyzer::add(const CanFrame &frame) {
    auto &acc = m_accum[frame.id];
    acc.count++;

    if (!acc.hasFirst) {
        acc.lastTs   = frame.timestamp;
        acc.hasFirst = true;
        return;
    }

    if (frame.timestamp <= acc.lastTs) {
        // Out-of-order or duplicate timestamp — skip interval, keep lastTs unchanged
        return;
    }

    uint64_t interval = frame.timestamp - acc.lastTs;
    acc.lastTs = frame.timestamp;

    // Gap detection BEFORE updating the mean (uses pre-interval mean)
    if (acc.wMean > 0 && static_cast<double>(interval) > m_gapFactor * acc.wMean)
        ++acc.gaps;

    ++acc.intervalCount;

    // Welford online algorithm — intervalCount is the number of valid intervals
    double delta = static_cast<double>(interval) - acc.wMean;
    acc.wMean   += delta / static_cast<double>(acc.intervalCount);
    double delta2 = static_cast<double>(interval) - acc.wMean;
    acc.wM2     += delta * delta2;

    if (interval < acc.minInterval) acc.minInterval = interval;
    if (interval > acc.maxInterval) acc.maxInterval = interval;
}

void CanIntervalAnalyzer::addAll(const QVector<CanFrame> &frames) {
    for (const auto &f : frames) add(f);
}

void CanIntervalAnalyzer::reset() {
    m_accum.clear();
}

CanIntervalStats CanIntervalAnalyzer::finalise(uint32_t id, const Accumulator &acc) const {
    CanIntervalStats s;
    s.id         = id;
    s.frameCount = acc.count;

    uint64_t nIntervals = acc.intervalCount;
    if (nIntervals == 0) {
        s.meanUs   = 0.0;
        s.stdDevUs = 0.0;
        s.minUs    = 0;
        s.maxUs    = 0;
        s.gapCount = 0;
        return s;
    }

    s.meanUs   = acc.wMean;
    s.stdDevUs = (nIntervals >= 2) ? std::sqrt(acc.wM2 / static_cast<double>(nIntervals - 1)) : 0.0;
    s.minUs    = acc.minInterval;
    s.maxUs    = acc.maxInterval;
    s.gapCount = acc.gaps;
    return s;
}

QVector<CanIntervalStats> CanIntervalAnalyzer::stats() const {
    QVector<CanIntervalStats> result;
    for (auto it = m_accum.cbegin(); it != m_accum.cend(); ++it)
        result.append(finalise(it.key(), it.value()));
    std::sort(result.begin(), result.end(),
              [](const CanIntervalStats &a, const CanIntervalStats &b) {
                  return a.id < b.id;
              });
    return result;
}

CanIntervalStats CanIntervalAnalyzer::statsFor(uint32_t id) const {
    auto it = m_accum.find(id);
    if (it == m_accum.end()) return {};
    return finalise(id, it.value());
}

QString CanIntervalStats::summary() const {
    if (frameCount < 2)
        return QString("0x%1: %2 frame(s) — not enough for interval analysis")
               .arg(id, 3, 16, QChar('0')).arg(frameCount);

    return QString("0x%1: %2 frames | mean=%3µs | std=%4µs | min=%5µs | max=%6µs | gaps=%7 | %8")
           .arg(id, 3, 16, QChar('0'))
           .arg(frameCount)
           .arg(meanUs, 0, 'f', 1)
           .arg(stdDevUs, 0, 'f', 1)
           .arg(minUs)
           .arg(maxUs)
           .arg(gapCount)
           .arg(isPeriodic() ? "PERIODIC" : "irregular");
}

QString CanIntervalAnalyzer::report() const {
    auto all = stats();
    if (all.isEmpty())
        return "No frames analysed.";
    QString out;
    for (const auto &s : all)
        out += s.summary() + "\n";
    return out;
}
