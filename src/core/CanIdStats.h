#pragma once
#include "CanFrame.h"
#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <unordered_map>

/// Per-ID statistical profile accumulated from live traffic or a log file.
struct IdProfile {
    uint32_t id          = 0;
    bool     extended    = false;
    uint64_t frameCount  = 0;
    uint64_t firstTs     = 0;   // µs
    uint64_t lastTs      = 0;   // µs

    // Inter-frame interval stats (µs), meaningful when frameCount >= 2
    uint64_t intervalMin = UINT64_MAX;
    uint64_t intervalMax = 0;
    double   intervalSum = 0.0; // used for mean; avoids overflow for long runs

    // CAN FD tracking
    uint64_t fdFrameCount = 0;
    uint64_t brsCount     = 0;
    uint64_t esiCount     = 0;
    uint8_t  maxFdDlc     = 0;  // max FD DLC seen (can be >8)

    // Per-byte stats (positions 0–7; tracked up to maxDlc seen)
    uint8_t maxDlc = 0;
    struct ByteStat {
        uint8_t  minVal = 0xFF;
        uint8_t  maxVal = 0x00;
        double   sum    = 0.0;
    };
    std::array<ByteStat, 8> bytes{};

    double avgIntervalUs() const {
        if (frameCount < 2) return 0.0;
        return intervalSum / static_cast<double>(frameCount - 1);
    }
    double avgIntervalMs() const { return avgIntervalUs() / 1000.0; }
    double freqHz() const {
        double avg = avgIntervalUs();
        return avg > 0.0 ? 1'000'000.0 / avg : 0.0;
    }
    double byteAvg(int pos) const {
        return frameCount > 0 ? bytes[pos].sum / static_cast<double>(frameCount) : 0.0;
    }
};

/// Accumulates per-ID statistics for all CAN IDs seen in a session.
/// Thread-unsafe by design — call from one thread (or guard externally).
class CanIdStats {
public:
    void feed(const CanFrame &frame);

    void reset();

    /// All profiles, sorted by descending frame count.
    std::vector<IdProfile> profiles() const;

    /// Profile for a specific ID (nullptr if not seen).
    const IdProfile *profileFor(uint32_t id) const;

    std::size_t uniqueIdCount() const { return m_map.size(); }
    uint64_t    totalFrameCount() const { return m_totalFrames; }

    /// Export to CSV string.
    /// Columns: id,extended,frames,first_ts_us,last_ts_us,avg_interval_ms,freq_hz,
    ///          b0_min,b0_max,b0_avg,...,b7_min,b7_max,b7_avg
    std::string toCsv() const;

private:
    std::unordered_map<uint32_t, IdProfile> m_map;
    uint64_t m_totalFrames = 0;
};
