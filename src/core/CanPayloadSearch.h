#pragma once
#include "CanFrame.h"
#include <cstdint>
#include <vector>
#include <QString>

/// Searches CAN frame recordings for frames whose payload contains a byte pattern.
///
/// Supports:
///   - Exact byte-sequence match at any position
///   - Masked match (byte & mask == value & mask) for don't-care bits
///   - Optional CAN ID filter
///   - Maximum number of results
class CanPayloadSearch {
public:
    struct Query {
        std::vector<uint8_t> pattern;   ///< Bytes to search for
        std::vector<uint8_t> mask;      ///< Per-byte mask (empty → all 0xFF, i.e. exact match)
        uint32_t             canId     = 0;
        bool                 filterById = false;  ///< If true, only search frames with canId
        int                  maxResults = 0;      ///< 0 = unlimited
    };

    struct Match {
        size_t   frameIndex = 0;   ///< Index in the input vector
        uint32_t canId      = 0;
        int      byteOffset = 0;   ///< Start offset of the match within the payload
    };

    /// Search a vector of frames; returns all matches.
    static std::vector<Match> search(const std::vector<CanFrame> &frames, const Query &query);

    /// Returns true if a single frame contains the query pattern.
    static bool matches(const CanFrame &frame, const Query &query);

    /// Search and return matching frames (copies).
    static std::vector<CanFrame> filterFrames(const std::vector<CanFrame> &frames,
                                               const Query &query);
};
