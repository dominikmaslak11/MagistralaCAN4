#pragma once
#include "CanFrame.h"
#include <cstdint>
#include <unordered_map>

/// Validates rolling alive/message counters in CAN frames.
///
/// Many AUTOSAR and OEM protocols embed a counter in a nibble or byte of each
/// CAN frame that increments by 1 (wrapping at a configurable modulus) with
/// every transmission. This class tracks the expected next counter value per
/// CAN ID and detects mismatches.
class CanCounterValidator {
public:
    enum class Result {
        Ok,          ///< Counter matches expected value
        Skip,        ///< First frame seen — no prior value to compare against
        Mismatch,    ///< Counter value differs from expected
        OutOfRange,  ///< Byte/nibble index out of frame bounds
    };

    struct Config {
        uint32_t canId      = 0;
        int      byteIndex  = 0;    ///< Which data byte holds the counter
        bool     upperNibble = false; ///< true = upper nibble (bits 7:4), false = full byte
        uint8_t  modulus    = 16;   ///< Counter wraps at this value (e.g. 16 for 4-bit, 256 for 8-bit)
    };

    struct Stats {
        uint64_t okCount       = 0;
        uint64_t mismatchCount = 0;
        uint64_t totalFrames   = 0;
    };

    void addConfig(const Config &cfg);
    void removeConfig(uint32_t canId);
    void reset();

    /// Feed a frame; returns Ok/Skip/Mismatch/OutOfRange.
    Result update(const CanFrame &frame);

    Stats statsFor(uint32_t canId) const;
    int   activeConfigCount() const { return static_cast<int>(m_states.size()); }

private:
    struct State {
        Config   cfg;
        bool     hasFirst  = false;
        uint8_t  expected  = 0;
        Stats    stats;
    };

    uint8_t extractCounter(const CanFrame &frame, const Config &cfg) const;

    std::unordered_map<uint32_t, State> m_states;
};
