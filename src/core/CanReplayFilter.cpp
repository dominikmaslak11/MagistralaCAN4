#include "CanReplayFilter.h"
#include <cmath>
#include <algorithm>

void CanReplayFilter::setRules(std::vector<ReplayRule> rules) {
    m_rules = std::move(rules);
}

void CanReplayFilter::clearRules() {
    m_rules.clear();
}

std::optional<CanFrame> CanReplayFilter::apply(const CanFrame &frame) const {
    const ReplayRule *rule = findRule(frame);
    if (!rule) return frame; // no match → pass through unchanged

    if (rule->drop) return std::nullopt;

    return transform(frame, *rule);
}

std::vector<CanFrame> CanReplayFilter::applyBatch(const std::vector<CanFrame> &frames) const {
    std::vector<CanFrame> out;
    out.reserve(frames.size());
    for (auto &f : frames) {
        auto result = apply(f);
        if (result) out.push_back(*result);
    }
    return out;
}

const ReplayRule *CanReplayFilter::findRule(const CanFrame &frame) const {
    for (auto &r : m_rules) {
        if (r.srcId != frame.id) continue;
        if (r.useExt && r.useExt != frame.extended) continue;
        return &r;
    }
    return nullptr;
}

CanFrame CanReplayFilter::transform(const CanFrame &frame, const ReplayRule &rule) const {
    CanFrame out = frame;

    if (rule.remapId) out.id = rule.dstId;

    for (int i = 0; i < out.dlc && i < 8; ++i) {
        const auto &xf = rule.byteXforms[i];
        if (!xf.active) continue;

        double raw = static_cast<double>(out.data[i]);
        double val = raw * xf.scale + xf.offset;
        int    clamped = static_cast<int>(std::round(val));
        if (clamped < 0)   clamped = 0;
        if (clamped > 255) clamped = 255;
        out.data[i] = static_cast<uint8_t>(clamped);
    }

    return out;
}
