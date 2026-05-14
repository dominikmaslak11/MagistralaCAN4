#pragma once
#include "CanFrame.h"
#include "DbcParser.h"
#include <QVector>
#include <QHash>
#include <QString>
#include <cstdint>
#include <cmath>
#include <limits>

/**
 * @brief Statistical summary for one DBC signal over a batch of frames.
 */
struct SignalStats {
    QString  signalName;
    QString  unit;
    uint64_t sampleCount = 0;

    double   minVal  = std::numeric_limits<double>::max();
    double   maxVal  = std::numeric_limits<double>::lowest();
    double   mean    = 0.0;
    double   stdDev  = 0.0;

    // 10-bucket histogram (values bucketed into [minVal, maxVal] range)
    static constexpr int BUCKET_COUNT = 10;
    std::array<uint64_t, BUCKET_COUNT> histogram{};

    /// True if enough samples exist for meaningful statistics.
    bool valid() const { return sampleCount >= 2; }

    /// Which histogram bucket a value falls in (clamped to [0, BUCKET_COUNT-1]).
    int bucketFor(double val) const;

    /// Human-readable single-line summary.
    QString summary() const;
};

/**
 * @brief Computes per-signal statistics from a batch of CAN frames.
 *
 * Decodes signals using a DbcParser, then accumulates min/max/mean/stdDev
 * and a histogram for each signal across all matching frames.
 *
 * Usage (two-pass — first to collect range, second to build histogram):
 *   CanSignalStatistics stats;
 *   stats.setDbc(&parser);
 *   stats.addAll(frames);   // single pass: mean/stdDev via Welford
 *   auto s = stats.statsFor("EngineSpeed");
 */
class CanSignalStatistics {
public:
    void setDbc(const DbcParser *dbc) { m_dbc = dbc; }

    /// Process one CAN frame. Call setDbc() first.
    void add(const CanFrame &frame);

    /// Process a batch of frames.
    void addAll(const QVector<CanFrame> &frames);

    /// Reset all accumulated state (retains DBC reference).
    void reset();

    /// Statistics for all signals seen so far, sorted by signal name.
    QVector<SignalStats> allStats() const;

    /// Statistics for one signal name, or empty struct if not seen.
    SignalStats statsFor(const QString &name) const;

    /// Human-readable report.
    QString report() const;

private:
    struct Accumulator {
        QString  unit;
        uint64_t count  = 0;
        double   wMean  = 0.0;
        double   wM2    = 0.0;
        double   minVal = std::numeric_limits<double>::max();
        double   maxVal = std::numeric_limits<double>::lowest();
        QVector<double> samples;  // kept for histogram (capped at MAX_SAMPLES)
        static constexpr int MAX_SAMPLES = 10000;
    };

    SignalStats finalise(const QString &name, const Accumulator &acc) const;

    const DbcParser *m_dbc = nullptr;
    QHash<QString, Accumulator> m_accum;
};
