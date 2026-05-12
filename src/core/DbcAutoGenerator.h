#pragma once
#include "CanFrame.h"
#include "DbcParser.h"
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

// ── Byte role classification ──────────────────────────────────────────────────

enum class ByteRole {
    Counter,    // monotonically incrementing, wraps (frame counter, timestamp)
    Checksum,   // XOR or sum of other bytes in frame
    Toggle,     // alternates between exactly two values
    State,      // small discrete set (<= 8 unique values), low entropy
    Value,      // continuous measurement — can form multi-byte signals
    Constant,   // never changes
    Unknown,
};

struct AutoByteStats {
    uint8_t     minVal   = 0xFF;
    uint8_t     maxVal   = 0x00;
    double      entropy  = 0.0;   // Shannon entropy [0..8]
    ByteRole    role     = ByteRole::Unknown;
    std::set<uint8_t> uniqueValues;
};

struct AutoSignal {
    int         startBit = 0;     // LSB position (little-endian within frame)
    int         bitLength= 8;
    bool        isSigned = false;
    double      scale    = 1.0;
    double      offset   = 0.0;
    double      minimum  = 0.0;
    double      maximum  = 0.0;
    std::string unit;
    std::string name;
    ByteRole    role     = ByteRole::Unknown;
};

struct AutoMessage {
    uint32_t              id          = 0;
    int                   dlc         = 0;
    double                periodMs    = 0.0; // 0 = event-triggered / sporadic
    std::string           timing;            // "periodic" | "event" | "sporadic"
    uint64_t              frameCount  = 0;
    std::vector<AutoSignal>  sigs;
    std::vector<AutoByteStats> byteStats;
};

// ── Pure-C++ reverse-engineering engine ──────────────────────────────────────

class DbcAutoGenerator {
public:
    // Minimum frames per ID to attempt classification
    static constexpr int kMinFrames = 5;

    // Analyze a stream of frames and produce estimated DBC messages.
    // observationTimeUs: total capture duration (for periodicity estimate).
    std::vector<AutoMessage> analyze(const std::vector<CanFrame> &frames,
                                     uint64_t observationTimeUs = 0);

    // Convert AutoMessage results into the existing DbcMessage format
    // (for export via DbcParser::serialize or DbcEditorWidget::setMessages).
    static std::vector<DbcMessage> toDbcMessages(const std::vector<AutoMessage> &msgs);

    // Classify a single byte's stats
    static ByteRole classifyRole(const AutoByteStats &bs,
                                  bool xorMatchesOtherBytes,
                                  double counterFraction);

    // Calculate Shannon entropy from value histogram
    static double computeEntropy(const std::vector<uint8_t> &values);

    // Fraction of consecutive pairs where diff == +1 (mod 256)
    static double counterFraction(const std::vector<uint8_t> &values);

    // True if XOR of all other bytes in each frame ≈ this byte's value
    static bool isChecksumByte(int byteIdx, const std::vector<CanFrame> &frames, int dlc);

    // Median inter-frame interval in ms; returns 0 if fewer than 2 frames
    static double estimatePeriodMs(const std::vector<CanFrame> &frames);

    // Human-readable role name
    static std::string roleName(ByteRole r);

private:
    AutoByteStats analyzeOneByte(int byteIdx,
                                  const std::vector<CanFrame> &frames,
                                  int dlc);

    std::vector<AutoSignal> buildSignals(const std::vector<AutoByteStats> &stats,
                                          uint32_t id, int dlc,
                                          const std::vector<CanFrame> &frames);
};
