#include "LearningEngine.h"
#include <shared_mutex>
#include <unordered_map>
// ── Auto-increment byte filter ──────────────────────────────

std::unordered_set<uint64_t> LearningEngine::detectAutoIncrementBytes() const {
    std::shared_lock lock(m_mutex);
    std::unordered_set<uint64_t> result;
    if (m_frameHistory.size() < 10) return result;

    // Group frames by ID
    std::unordered_map<uint32_t, std::vector<CanFrame>> byId;
    for (const auto &f : m_frameHistory)
        byId[f.id].push_back(f);

    for (const auto &kv : byId) {
        uint32_t id = kv.first;
        const auto &frames = kv.second;
        if (frames.size() < 5) continue;

        for (int b = 0; b < 8; ++b) {
            // Check if byte always changes by +1 (with wrap)
            int changes = 0;
            int incByOne = 0;
            int totalPairs = 0;
            for (size_t i = 1; i < frames.size(); ++i) {
                if (b >= frames[i].dlc || b >= frames[i-1].dlc) continue;
                int prev = frames[i-1].data[b];
                int curr = frames[i].data[b];
                totalPairs++;
                if (prev != curr) {
                    changes++;
                    int diff = (curr - prev) & 0xFF;
                    if (diff == 1 || diff == 0xFF)  // +1 or wrap (255→0)
                        incByOne++;
                }
            }
            // Auto-increment: >90% of changes are +1, and >80% of pairs change
            if (totalPairs > 4 && changes > totalPairs * 0.8 && incByOne > changes * 0.9) {
                result.insert((static_cast<uint64_t>(id) << 8) | b);
            }
        }
    }
    return result;
}

// ── Cyclic noise filter ────────────────────────────────────

std::unordered_set<uint64_t> LearningEngine::detectCyclicNoiseBytes() const {
    std::shared_lock lock(m_mutex);
    std::unordered_set<uint64_t> result;
    if (m_frameHistory.size() < 10) return result;

    std::unordered_map<uint32_t, std::vector<CanFrame>> byId;
    for (const auto &f : m_frameHistory)
        byId[f.id].push_back(f);

    for (const auto &kv : byId) {
        uint32_t id = kv.first;
        const auto &frames = kv.second;
        if (frames.size() < 5) continue;

        for (int b = 0; b < 8; ++b) {
            int bitToggles[8] = {};
            int totalPairs = 0;
            for (size_t i = 1; i < frames.size(); ++i) {
                if (b >= (int)frames[i].dlc || b >= (int)frames[i-1].dlc) continue;
                uint8_t changed = frames[i-1].data[b] ^ frames[i].data[b];
                totalPairs++;
                for (int bit = 0; bit < 8; ++bit)
                    if (changed & (1u << bit))
                        bitToggles[bit]++;
            }
            if (totalPairs < 4) continue;
            // Any single bit toggling in >40% of consecutive pairs = cyclic noise
            for (int bit = 0; bit < 8; ++bit) {
                if (bitToggles[bit] > totalPairs * 0.4) {
                    result.insert((static_cast<uint64_t>(id) << 8) | b);
                    break;
                }
            }
        }
    }
    return result;
}

