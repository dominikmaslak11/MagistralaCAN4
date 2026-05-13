#pragma once
#include "CanFrame.h"
#include <QString>
#include <vector>
#include <cstdint>

// Exports CAN frames to PCAP (Wireshark-compatible, LINKTYPE_CAN_SOCKETCAN = 227)
class PcapExporter {
public:
    // Export a vector of frames. Timestamps in microseconds (0 = use index * 1000us).
    static bool exportFrames(const QString &path,
                             const std::vector<CanFrame> &frames,
                             const std::vector<uint64_t> &timestampsUs = {});

    // Single-frame overload used in streaming scenarios
    static bool writeHeader(const QString &path);  // opens new file, writes global header
};
