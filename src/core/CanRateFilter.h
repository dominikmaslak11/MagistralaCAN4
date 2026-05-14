#pragma once
#include "CanFrame.h"
#include <cstdint>
#include <unordered_map>

// Per-ID frame-rate limiter.
// Passes a frame only if at least minIntervalUs has elapsed since the last
// accepted frame for that CAN ID.  Useful for decimating high-rate recordings
// before display or export without losing the first frame of each ID.
class CanRateFilter {
public:
    // Set the global default maximum rate (Hz).  0 = unlimited (pass all).
    void setGlobalMaxHz(double hz);

    // Override the max rate for a specific ID.  0 = block this ID entirely.
    void setIdMaxHz(uint32_t id, double hz);

    // Remove per-ID override; the ID falls back to the global rate.
    void clearIdOverride(uint32_t id);

    // Returns true if the frame should be passed through at nowUs.
    // Updates internal state regardless of return value.
    bool shouldPass(const CanFrame &frame, uint64_t nowUs);

    // Reset all per-ID last-seen timestamps; counters stay intact.
    void resetTimestamps();

    // Reset everything including counters.
    void reset();

    int accepted() const { return m_accepted; }
    int rejected() const { return m_rejected; }

private:
    uint64_t minIntervalUs(uint32_t id) const;

    double m_globalHz = 0.0;  // 0 = unlimited

    std::unordered_map<uint32_t, double>   m_overrides;   // id → max Hz
    std::unordered_map<uint32_t, uint64_t> m_lastPassUs;  // id → last accepted ts

    int m_accepted = 0;
    int m_rejected = 0;
};
