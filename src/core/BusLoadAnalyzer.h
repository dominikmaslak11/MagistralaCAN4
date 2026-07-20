#pragma once
#include "CanFrame.h"
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Pure-C++ CAN bus load calculator.
// Computes bandwidth utilization % from a sliding window of observed frames.
// Thread-safe: not required — call from a single thread.

class BusLoadAnalyzer {
public:
    explicit BusLoadAnalyzer(uint32_t baudRateBps = 500000);

    void setBaudRate(uint32_t bps);
    void setWindowSec(double sec);
    void processFrame(const CanFrame &frame);
    void reset();

    // [0.0, 1.0] — fraction of bandwidth consumed in the current window
    [[nodiscard]] double currentLoad() const;

    // Peak load ever seen (since last reset)
    [[nodiscard]] double peakLoad() const { return m_peakLoad; }

    // Frames per second in the current window
    [[nodiscard]] double framesPerSec() const;

    // Total unique CAN IDs seen since last reset
    [[nodiscard]] size_t uniqueIdCount() const { return m_seenIds.size(); }

    // Per-ID load breakdown — sorted by load descending
    struct IdLoad {
        uint32_t id;
        uint64_t framesInWindow;
        uint64_t bitsInWindow;
        double   loadFraction;   // [0.0, 1.0]
        double   fps;
    };
    [[nodiscard]] std::vector<IdLoad> topLoaders(size_t n = 10) const;

    // Bits required for a single CAN frame (nominal, no bit-stuffing overhead)
    // extended = 29-bit arbitration field; fd = CAN FD (variable DLC up to 64)
    [[nodiscard]] static uint32_t frameBits(bool extended, uint8_t dlc, bool fd = false);

private:
    void pruneOld(uint64_t nowUs);

    struct Record {
        uint64_t tsUs;
        uint32_t id;
        uint32_t bits;
    };

    uint32_t m_baudRate  = 500000;
    double   m_windowSec = 1.0;
    double   m_peakLoad  = 0.0;

    std::deque<Record>                     m_window;
    uint64_t                               m_windowBits = 0;
    std::unordered_map<uint32_t, uint64_t> m_idBits;    // id → bits in window
    std::unordered_map<uint32_t, uint64_t> m_idFrames;  // id → frames in window
    std::unordered_set<uint32_t>           m_seenIds;
};
