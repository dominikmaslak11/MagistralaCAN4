#include "CanPayloadSearch.h"
#include <algorithm>

static bool patternMatch(const CanFrame &frame, const CanPayloadSearch::Query &query,
                         int offset) {
    const auto &pat  = query.pattern;
    const auto &mask = query.mask;
    int patLen = static_cast<int>(pat.size());

    if (offset + patLen > frame.dlc) return false;

    for (int i = 0; i < patLen; ++i) {
        uint8_t m = (i < static_cast<int>(mask.size())) ? mask[i] : 0xFF;
        if ((frame.data[offset + i] & m) != (pat[i] & m))
            return false;
    }
    return true;
}

bool CanPayloadSearch::matches(const CanFrame &frame, const Query &query) {
    if (query.pattern.empty()) return false;
    if (query.filterById && frame.id != query.canId) return false;
    for (int off = 0; off <= frame.dlc - static_cast<int>(query.pattern.size()); ++off)
        if (patternMatch(frame, query, off)) return true;
    return false;
}

std::vector<CanPayloadSearch::Match> CanPayloadSearch::search(
    const std::vector<CanFrame> &frames, const Query &query) {

    std::vector<Match> results;
    if (query.pattern.empty()) return results;

    int patLen = static_cast<int>(query.pattern.size());

    for (size_t fi = 0; fi < frames.size(); ++fi) {
        const CanFrame &f = frames[fi];

        if (query.filterById && f.id != query.canId) continue;

        for (int off = 0; off <= f.dlc - patLen; ++off) {
            if (patternMatch(f, query, off)) {
                results.push_back({fi, f.id, off});
                break; // one match per frame
            }
        }

        if (query.maxResults > 0 && static_cast<int>(results.size()) >= query.maxResults)
            break;
    }
    return results;
}

std::vector<CanFrame> CanPayloadSearch::filterFrames(const std::vector<CanFrame> &frames,
                                                      const Query &query) {
    std::vector<CanFrame> out;
    for (const auto &f : frames)
        if (matches(f, query)) out.push_back(f);
    return out;
}
