#include "CanRateFilter.h"
#include <algorithm>
#include <cmath>
#include <limits>

void CanRateFilter::setGlobalMaxHz(double hz) {
    m_globalHz = (hz < 0.0) ? 0.0 : hz;
}

void CanRateFilter::setIdMaxHz(uint32_t id, double hz) {
    m_overrides[id] = (hz < 0.0) ? 0.0 : hz;
}

void CanRateFilter::clearIdOverride(uint32_t id) {
    m_overrides.erase(id);
}

uint64_t CanRateFilter::minIntervalUs(uint32_t id) const {
    double hz = m_globalHz;
    auto it = m_overrides.find(id);
    if (it != m_overrides.end())
        hz = it->second;

    if (hz <= 0.0) return 0;  // unlimited or negative: always pass
    // hz == effectively zero but positive → block: return max interval
    return static_cast<uint64_t>(1'000'000.0 / hz);
}

bool CanRateFilter::shouldPass(const CanFrame &frame, uint64_t nowUs) {
    uint64_t interval = minIntervalUs(frame.id);

    if (interval == 0) {
        // Check if this is because hz=0 (block) or globalHz=0 (unlimited)
        auto it = m_overrides.find(frame.id);
        if (it != m_overrides.end() && it->second == 0.0) {
            ++m_rejected;
            return false;
        }
        // Unlimited — always pass
        m_lastPassUs[frame.id] = nowUs;
        ++m_accepted;
        return true;
    }

    auto it = m_lastPassUs.find(frame.id);
    if (it == m_lastPassUs.end()) {
        // First frame for this ID: always pass
        m_lastPassUs[frame.id] = nowUs;
        ++m_accepted;
        return true;
    }

    uint64_t elapsed = (nowUs >= it->second) ? (nowUs - it->second) : 0;
    if (elapsed >= interval) {
        it->second = nowUs;
        ++m_accepted;
        return true;
    }

    ++m_rejected;
    return false;
}

void CanRateFilter::resetTimestamps() {
    m_lastPassUs.clear();
}

void CanRateFilter::reset() {
    m_lastPassUs.clear();
    m_accepted = 0;
    m_rejected = 0;
}
