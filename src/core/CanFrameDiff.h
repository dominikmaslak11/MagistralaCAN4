#pragma once
#include "CanFrame.h"
#include <QVector>
#include <QHash>
#include <QString>
#include <array>
#include <cstdint>

/// Per-ID summary extracted from a CAN recording.
struct CanIdSummary {
    uint32_t id          = 0;
    bool     extended    = false;
    uint64_t frameCount  = 0;
    uint8_t  maxDlc      = 0;
    std::array<uint8_t, 8> byteMin{};   // per-byte minimum value seen
    std::array<uint8_t, 8> byteMax{};   // per-byte maximum value seen
};

/// Result for one CAN ID when comparing two recordings.
struct CanDiffEntry {
    enum Change {
        Added,      ///< ID present in B but not in A
        Removed,    ///< ID present in A but not in B
        ByteChanged ///< ID in both; at least one byte range shifted
    };

    uint32_t  id      = 0;
    bool      extended = false;
    Change    change  = Added;
    uint64_t  countA  = 0;
    uint64_t  countB  = 0;

    // Byte-level change mask (bit i = byte i changed range)
    uint8_t changedBytesMask = 0;

    QString description() const;
};

/**
 * @brief Compares two sets of CAN frames and reports structural differences.
 *
 * Usage:
 *   CanFrameDiff diff;
 *   diff.setRecordingA(framesA);
 *   diff.setRecordingB(framesB);
 *   auto result = diff.compute();
 */
class CanFrameDiff {
public:
    void setRecordingA(const QVector<CanFrame> &frames);
    void setRecordingB(const QVector<CanFrame> &frames);

    /// Run the comparison and return the list of differences.
    QVector<CanDiffEntry> compute() const;

    /// Per-ID summaries for recording A (built by setRecordingA).
    const QHash<uint32_t, CanIdSummary> &summariesA() const { return m_summaryA; }
    /// Per-ID summaries for recording B (built by setRecordingB).
    const QHash<uint32_t, CanIdSummary> &summariesB() const { return m_summaryB; }

    /// Human-readable report.
    QString report() const;

private:
    static QHash<uint32_t, CanIdSummary> buildSummary(const QVector<CanFrame> &frames);
    static bool byteRangeChanged(const CanIdSummary &a, const CanIdSummary &b, uint8_t &mask);

    QHash<uint32_t, CanIdSummary> m_summaryA;
    QHash<uint32_t, CanIdSummary> m_summaryB;
};
