#pragma once
#include "CanFrame.h"
#include <array>
#include <deque>
#include <unordered_map>
#include <vector>
#include <cstdint>

/**
 * Pure C++ model for the byte-value heatmap.
 *
 * Stores a capped ring-buffer of frames per CAN ID.
 * heatmapData() divides a sliding time window into fixed buckets and
 * computes the mean value of each byte within each bucket.
 */
class CanByteHeatmapModel {
public:
    explicit CanByteHeatmapModel(int maxFramesPerid = 5000);

    void addFrame(const CanFrame &f, uint64_t tsUs);
    void clear();

    /// IDs in insertion (first-seen) order.
    const std::vector<uint32_t> &trackedIds() const { return m_idOrder; }

    /**
     * Compute heatmap rows for @p id over the last @p windowUs microseconds,
     * divided into @p bucketCount equal time buckets.
     *
     * Returns vector of @p bucketCount rows, oldest first.
     * Each row is an array of 8 doubles: mean byte value [0..255],
     * or -1.0 if no frames landed in that bucket.
     */
    std::vector<std::array<double, 8>> heatmapData(
        uint32_t id, uint64_t windowUs, int bucketCount) const;

    uint64_t latestTimestamp() const { return m_maxTs; }
    int frameCount(uint32_t id) const;
    int totalIds() const { return static_cast<int>(m_ids.size()); }

private:
    struct IdState {
        std::deque<uint64_t>               timestamps;
        std::deque<std::array<uint8_t, 8>> byteValues;
    };

    std::unordered_map<uint32_t, IdState> m_ids;
    std::vector<uint32_t>                 m_idOrder;
    uint64_t m_maxTs = 0;
    int      m_maxFramesPerid;
};
