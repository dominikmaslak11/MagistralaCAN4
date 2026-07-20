#pragma once
#include "CanFrame.h"
#include <QVector>
#include <QHash>
#include <QString>
#include <cstdint>
#include <cmath>
#include <limits>

/**
 * @brief Timing statistics for a single CAN ID.
 *
 * All intervals are in microseconds (same unit as CanFrame::timestamp).
 */
struct CanIntervalStats {
    uint32_t id         = 0;
    uint64_t frameCount = 0;   // total frames seen

    // Interval statistics (computed from gaps between consecutive frames)
    double   meanUs   = 0.0;
    double   stdDevUs = 0.0;
    uint64_t minUs    = std::numeric_limits<uint64_t>::max();
    uint64_t maxUs    = 0;

    // Gap detection: intervals > gapThresholdFactor * meanUs are gaps
    int      gapCount = 0;

    // Estimated period (0 if fewer than 2 frames)
    double   estimatedPeriodUs() const { return meanUs; }

    /// True if the message appears to be periodic (stdDev < 20% of mean).
    bool isPeriodic() const {
        return frameCount >= 3 && meanUs > 0 && stdDevUs / meanUs < 0.20;
    }

    /// Human-readable one-line description.
    QString summary() const;
};

/**
 * @brief Analyses inter-frame timing for each observed CAN ID.
 *
 * Usage:
 *   CanIntervalAnalyzer a;
 *   a.addAll(frames);
 *   for (const auto &s : a.stats())
 *       qDebug() << s.summary();
 */
class CanIntervalAnalyzer {
public:
    /// Factor above mean that marks a gap (default 3×).
    explicit CanIntervalAnalyzer(double gapFactor = 3.0)
        : m_gapFactor(gapFactor) {}

    /// Feed one frame; timestamps must be in microseconds.
    void add(const CanFrame &frame);

    /// Feed a batch of frames in arrival order.
    void addAll(const QVector<CanFrame> &frames);

    /// Reset all state.
    void reset();

    /// Compute and return statistics for every observed ID, sorted by ID.
    QVector<CanIntervalStats> stats() const;

    /// Stats for one specific ID (computed on demand), or empty if not seen.
    CanIntervalStats statsFor(uint32_t id) const;

    /// Human-readable report.
    QString report() const;

private:
    struct Accumulator {
        uint64_t lastTs        = 0;
        bool     hasFirst      = false;
        uint64_t count         = 0;  // total frames (including skipped)
        uint64_t intervalCount = 0;  // valid intervals accumulated

        // Running Welford for mean + M2 (variance accumulation)
        double   wMean = 0.0;
        double   wM2   = 0.0;

        uint64_t minInterval = std::numeric_limits<uint64_t>::max();
        uint64_t maxInterval = 0;
        int      gaps        = 0;
    };

    CanIntervalStats finalise(uint32_t id, const Accumulator &acc) const;

    QHash<uint32_t, Accumulator> m_accum;
    double m_gapFactor;
};
