#include "CanFrameDiff.h"
#include <algorithm>

// ── Summary building ─────────────────────────────────────────

QHash<uint32_t, CanIdSummary> CanFrameDiff::buildSummary(const QVector<CanFrame> &frames) {
    QHash<uint32_t, CanIdSummary> map;
    for (const auto &f : frames) {
        auto &s = map[f.id];
        if (s.frameCount == 0) {
            s.id       = f.id;
            s.extended = f.extended;
            s.byteMin.fill(0xFF);
            s.byteMax.fill(0x00);
        }
        ++s.frameCount;
        s.maxDlc = std::max(s.maxDlc, f.dlc);
        int limit = std::min<int>(f.dlc, 8);
        for (int i = 0; i < limit; ++i) {
            s.byteMin[i] = std::min(s.byteMin[i], f.data[i]);
            s.byteMax[i] = std::max(s.byteMax[i], f.data[i]);
        }
    }
    return map;
}

void CanFrameDiff::setRecordingA(const QVector<CanFrame> &frames) {
    m_summaryA = buildSummary(frames);
}

void CanFrameDiff::setRecordingB(const QVector<CanFrame> &frames) {
    m_summaryB = buildSummary(frames);
}

// ── Byte-range change detection ──────────────────────────────

bool CanFrameDiff::byteRangeChanged(const CanIdSummary &a, const CanIdSummary &b,
                                    uint8_t &mask) {
    mask = 0;
    int limit = std::max<int>(a.maxDlc, b.maxDlc);
    limit = std::min(limit, 8);
    for (int i = 0; i < limit; ++i) {
        bool minChanged = a.byteMin[i] != b.byteMin[i];
        bool maxChanged = a.byteMax[i] != b.byteMax[i];
        if (minChanged || maxChanged)
            mask |= static_cast<uint8_t>(1u << i);
    }
    return mask != 0;
}

// ── Compute ──────────────────────────────────────────────────

QVector<CanDiffEntry> CanFrameDiff::compute() const {
    QVector<CanDiffEntry> result;

    // IDs in A — check if removed or changed
    for (auto it = m_summaryA.cbegin(); it != m_summaryA.cend(); ++it) {
        uint32_t id = it.key();
        if (!m_summaryB.contains(id)) {
            CanDiffEntry e;
            e.id       = id;
            e.extended = it->extended;
            e.change   = CanDiffEntry::Removed;
            e.countA   = it->frameCount;
            e.countB   = 0;
            result.append(e);
        } else {
            const CanIdSummary &a = *it;
            const CanIdSummary &b = m_summaryB[id];
            uint8_t mask = 0;
            if (byteRangeChanged(a, b, mask)) {
                CanDiffEntry e;
                e.id               = id;
                e.extended         = a.extended;
                e.change           = CanDiffEntry::ByteChanged;
                e.countA           = a.frameCount;
                e.countB           = b.frameCount;
                e.changedBytesMask = mask;
                result.append(e);
            }
        }
    }

    // IDs in B but not in A → Added
    for (auto it = m_summaryB.cbegin(); it != m_summaryB.cend(); ++it) {
        uint32_t id = it.key();
        if (!m_summaryA.contains(id)) {
            CanDiffEntry e;
            e.id       = id;
            e.extended = it->extended;
            e.change   = CanDiffEntry::Added;
            e.countA   = 0;
            e.countB   = it->frameCount;
            result.append(e);
        }
    }

    // Sort by ID for stable output
    std::sort(result.begin(), result.end(),
              [](const CanDiffEntry &a, const CanDiffEntry &b) {
                  return a.id < b.id;
              });

    return result;
}

// ── Descriptions / report ────────────────────────────────────

QString CanDiffEntry::description() const {
    switch (change) {
    case Added:
        return QString("ADDED   0x%1 (%2 frames in B)")
            .arg(id, 3, 16, QChar('0')).arg(countB);
    case Removed:
        return QString("REMOVED 0x%1 (%2 frames in A)")
            .arg(id, 3, 16, QChar('0')).arg(countA);
    case ByteChanged: {
        QString bytes;
        for (int i = 0; i < 8; ++i)
            if (changedBytesMask & (1u << i))
                bytes += QString("B%1 ").arg(i);
        return QString("CHANGED 0x%1 — bytes: %2(A:%3 B:%4 frames)")
            .arg(id, 3, 16, QChar('0')).arg(bytes.trimmed()).arg(countA).arg(countB);
    }
    }
    return {};
}

QString CanFrameDiff::report() const {
    auto entries = compute();
    if (entries.isEmpty())
        return "No differences found.";

    QString out;
    int added = 0, removed = 0, changed = 0;
    for (const auto &e : entries) {
        out += e.description() + "\n";
        switch (e.change) {
        case CanDiffEntry::Added:       ++added;   break;
        case CanDiffEntry::Removed:     ++removed; break;
        case CanDiffEntry::ByteChanged: ++changed; break;
        }
    }
    out += QString("\n--- %1 added, %2 removed, %3 byte-changed ---\n")
        .arg(added).arg(removed).arg(changed);
    return out;
}
