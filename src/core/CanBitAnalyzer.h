#pragma once
#include "CanFrame.h"
#include <QVector>
#include <QHash>
#include <QString>
#include <array>
#include <cstdint>

/**
 * @brief Per-byte bit profile for one CAN ID.
 *
 * For each of the 8 data bytes:
 *   alwaysZero  — bits that were 0 in every observed frame
 *   alwaysOne   — bits that were 1 in every observed frame
 *   varying     — bits that changed at least once (= ~alwaysZero & ~alwaysOne)
 *
 * Bits in alwaysZero and alwaysOne are disjoint; a bit that is both in
 * alwaysZero and alwaysOne means only one frame was seen (initial state).
 * After the second frame at minimum they stay disjoint: a bit that was 1
 * once and 0 once moves to varying.
 */
struct CanBitProfile {
    uint32_t id         = 0;
    bool     extended   = false;
    uint64_t frameCount = 0;
    uint8_t  maxDlc     = 0;

    std::array<uint8_t, 8> alwaysZero{};  // bit=1 means "seen only 0 in this position"
    std::array<uint8_t, 8> alwaysOne{};   // bit=1 means "seen only 1 in this position"

    /// Bits that have taken both values across observed frames.
    std::array<uint8_t, 8> varying() const {
        std::array<uint8_t, 8> v{};
        for (int i = 0; i < 8; ++i)
            v[i] = static_cast<uint8_t>(~alwaysZero[i] & ~alwaysOne[i]);
        return v;
    }

    /// True if any bit is varying in the given byte.
    bool hasVaryingBits(int byteIdx) const {
        if (byteIdx < 0 || byteIdx >= 8) return false;
        return varying()[byteIdx] != 0;
    }

    /// Human-readable single-line summary.
    QString summary() const;
};

/**
 * @brief Analyses a set of CAN frames and builds per-ID bit profiles.
 *
 * Usage:
 *   CanBitAnalyzer analyzer;
 *   for (const auto &f : frames)
 *       analyzer.add(f);
 *   auto profiles = analyzer.profiles();
 *   QString report = analyzer.report();
 */
class CanBitAnalyzer {
public:
    /// Feed one frame into the analysis.
    void add(const CanFrame &frame);

    /// Feed a batch of frames.
    void addAll(const QVector<CanFrame> &frames);

    /// Reset all accumulated state.
    void reset();

    /// All profiles, one per observed CAN ID.
    QVector<CanBitProfile> profiles() const;

    /// Profile for a specific ID, or nullptr if not seen.
    const CanBitProfile *profileFor(uint32_t id) const;

    /// Human-readable report listing every ID and its bit classification.
    QString report() const;

private:
    QHash<uint32_t, CanBitProfile> m_profiles;
};
