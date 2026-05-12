#pragma once
#include "CanFrame.h"
#include <cstdint>
#include <vector>
#include <optional>

/// Transformation rule applied to a single CAN frame.
struct ReplayRule {
    uint32_t srcId  = 0;           // Match source CAN ID
    bool     useExt = false;       // Require extended frame flag to match

    // ID remapping (optional)
    bool     remapId  = false;
    uint32_t dstId    = 0;

    // Byte-level transformation per position (up to 8 bytes)
    struct ByteXform {
        bool    active = false;
        double  scale  = 1.0;   // new = clamp(round(old * scale + offset), 0, 255)
        double  offset = 0.0;
    };
    ByteXform byteXforms[8]{};

    bool drop = false;  // Drop this frame entirely (takes priority over all other fields)
};

/// Applies a list of ReplayRules to CAN frames.
/// Rules are evaluated in order; first matching rule wins.
class CanReplayFilter {
public:
    void setRules(std::vector<ReplayRule> rules);
    void clearRules();

    /// Apply rules to a frame.
    /// Returns transformed frame, or nullopt if the frame should be dropped.
    std::optional<CanFrame> apply(const CanFrame &frame) const;

    /// Apply rules to a batch of frames, dropping as configured.
    std::vector<CanFrame> applyBatch(const std::vector<CanFrame> &frames) const;

    const std::vector<ReplayRule> &rules() const { return m_rules; }

private:
    std::vector<ReplayRule> m_rules;

    const ReplayRule *findRule(const CanFrame &frame) const;
    CanFrame          transform(const CanFrame &frame, const ReplayRule &rule) const;
};
