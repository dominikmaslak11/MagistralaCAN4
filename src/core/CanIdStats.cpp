#include "CanIdStats.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <format>

void CanIdStats::feed(const CanFrame &frame) {
    ++m_totalFrames;
    auto &p = m_map[frame.id];
    if (p.frameCount == 0) {
        p.id        = frame.id;
        p.extended  = frame.extended;
        p.firstTs   = frame.timestamp;
        p.lastTs    = frame.timestamp;
    } else {
        uint64_t interval = (frame.timestamp > p.lastTs) ? frame.timestamp - p.lastTs : 0;
        if (interval < p.intervalMin) p.intervalMin = interval;
        if (interval > p.intervalMax) p.intervalMax = interval;
        p.intervalSum += static_cast<double>(interval);
        p.lastTs = frame.timestamp;
    }
    ++p.frameCount;

    if (frame.fd) {
        ++p.fdFrameCount;
        if (frame.brs) ++p.brsCount;
        if (frame.esi) ++p.esiCount;
        if (frame.dlc > p.maxFdDlc) p.maxFdDlc = frame.dlc;
    }

    uint8_t dlc = frame.dlc <= 8 ? frame.dlc : 8;
    if (dlc > p.maxDlc) p.maxDlc = dlc;
    for (int i = 0; i < dlc; ++i) {
        uint8_t v = frame.data[i];
        if (v < p.bytes[i].minVal) p.bytes[i].minVal = v;
        if (v > p.bytes[i].maxVal) p.bytes[i].maxVal = v;
        p.bytes[i].sum += v;
    }
}

void CanIdStats::reset() {
    m_map.clear();
    m_totalFrames = 0;
}

std::vector<IdProfile> CanIdStats::profiles() const {
    std::vector<IdProfile> out;
    out.reserve(m_map.size());
    for (auto &[id, p] : m_map) out.push_back(p);
    std::sort(out.begin(), out.end(),
              [](const IdProfile &a, const IdProfile &b) { return a.frameCount > b.frameCount; });
    return out;
}

const IdProfile *CanIdStats::profileFor(uint32_t id) const {
    auto it = m_map.find(id);
    return it != m_map.end() ? &it->second : nullptr;
}

std::string CanIdStats::toCsv() const {
    std::ostringstream ss;
    ss << "id,extended,fd_frames,brs_count,esi_count,frames,first_ts_us,last_ts_us,avg_interval_ms,freq_hz";
    for (int i = 0; i < 8; ++i)
        ss << ",b" << i << "_min,b" << i << "_max,b" << i << "_avg";
    ss << "\n";

    auto sorted = profiles();
    for (auto &p : sorted) {
        ss << std::format("0x{:X}", p.id) << ","
           << (p.extended ? 1 : 0) << ","
           << p.fdFrameCount << ","
           << p.brsCount << ","
           << p.esiCount << ","
           << p.frameCount << ","
           << p.firstTs << ","
           << p.lastTs << ","
           << std::fixed << std::setprecision(3) << p.avgIntervalMs() << ","
           << std::fixed << std::setprecision(2) << p.freqHz();
        for (int i = 0; i < 8; ++i) {
            if (i < p.maxDlc) {
                ss << "," << static_cast<int>(p.bytes[i].minVal)
                   << "," << static_cast<int>(p.bytes[i].maxVal)
                   << "," << std::fixed << std::setprecision(2) << p.byteAvg(i);
            } else {
                ss << ",,,";
            }
        }
        ss << "\n";
    }
    return ss.str();
}
