#include "CanByteHeatmapModel.h"
#include <algorithm>

CanByteHeatmapModel::CanByteHeatmapModel(int maxFramesPerid)
    : m_maxFramesPerid(maxFramesPerid)
{}

void CanByteHeatmapModel::addFrame(const CanFrame &f, uint64_t tsUs) {
    auto &state = m_ids[f.id];

    // Track insertion order
    if (state.timestamps.empty())
        m_idOrder.push_back(f.id);

    // Build byte snapshot — only first min(dlc,8) bytes are meaningful
    std::array<uint8_t, 8> bytes{};
    int toCopy = std::min(static_cast<int>(f.dlc), 8);
    for (int i = 0; i < toCopy; ++i)
        bytes[i] = f.data[i];

    state.timestamps.push_back(tsUs);
    state.byteValues.push_back(bytes);

    // Cap ring buffer
    while (static_cast<int>(state.timestamps.size()) > m_maxFramesPerid) {
        state.timestamps.pop_front();
        state.byteValues.pop_front();
    }

    if (tsUs > m_maxTs) m_maxTs = tsUs;
}

void CanByteHeatmapModel::clear() {
    m_ids.clear();
    m_idOrder.clear();
    m_maxTs = 0;
}

int CanByteHeatmapModel::frameCount(uint32_t id) const {
    auto it = m_ids.find(id);
    if (it == m_ids.end()) return 0;
    return static_cast<int>(it->second.timestamps.size());
}

std::vector<std::array<double, 8>> CanByteHeatmapModel::heatmapData(
    uint32_t id, uint64_t windowUs, int bucketCount) const
{
    const std::array<double, 8> kNoData = {-1,-1,-1,-1,-1,-1,-1,-1};
    std::vector<std::array<double, 8>> result(bucketCount, kNoData);

    if (bucketCount <= 0 || windowUs == 0) return result;

    auto it = m_ids.find(id);
    if (it == m_ids.end() || it->second.timestamps.empty()) return result;

    const auto &state = it->second;
    uint64_t viewEnd   = m_maxTs;
    uint64_t viewStart = (viewEnd > windowUs) ? (viewEnd - windowUs) : 0;
    uint64_t bucketUs  = windowUs / static_cast<uint64_t>(bucketCount);
    if (bucketUs == 0) bucketUs = 1;

    // Accumulators
    std::array<double, 8> zeroD{}; zeroD.fill(0.0);
    std::array<int, 8>    zeroI{}; zeroI.fill(0);
    std::vector<std::array<double, 8>> sums(bucketCount, zeroD);
    std::vector<std::array<int, 8>>   counts(bucketCount, zeroI);

    for (size_t i = 0; i < state.timestamps.size(); ++i) {
        uint64_t ts = state.timestamps[i];
        if (ts < viewStart || ts > viewEnd) continue;

        int bucket = static_cast<int>((ts - viewStart) / bucketUs);
        bucket = std::clamp(bucket, 0, bucketCount - 1);

        const auto &bv = state.byteValues[i];
        for (int b = 0; b < 8; ++b) {
            sums[bucket][b]  += bv[b];
            counts[bucket][b]++;
        }
    }

    // Normalize
    for (int r = 0; r < bucketCount; ++r) {
        for (int b = 0; b < 8; ++b) {
            result[r][b] = (counts[r][b] > 0)
                ? sums[r][b] / counts[r][b]
                : -1.0;
        }
    }

    return result;
}
