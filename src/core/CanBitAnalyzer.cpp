#include "CanBitAnalyzer.h"
#include <algorithm>

void CanBitAnalyzer::add(const CanFrame &frame) {
    auto &p = m_profiles[frame.id];

    if (p.frameCount == 0) {
        // First frame: initialise profile
        p.id       = frame.id;
        p.extended = frame.extended;
        p.maxDlc   = frame.dlc;
        // Seed: assume every bit could be always-zero or always-one
        p.alwaysZero.fill(0xFF);
        p.alwaysOne.fill(0xFF);
    } else {
        p.maxDlc = std::max(p.maxDlc, frame.dlc);
    }

    ++p.frameCount;

    int limit = std::min<int>(frame.dlc, 8);
    for (int i = 0; i < limit; ++i) {
        uint8_t b = frame.data[i];
        // A bit is no longer "always zero" if it is 1 in this frame
        p.alwaysZero[i] &= static_cast<uint8_t>(~b);
        // A bit is no longer "always one" if it is 0 in this frame
        p.alwaysOne[i]  &= b;
    }
    // Bytes beyond the frame's DLC — treat as zero for those positions
    // (no update needed: their alwaysZero stays unchanged; they saw no data)
}

void CanBitAnalyzer::addAll(const QVector<CanFrame> &frames) {
    for (const auto &f : frames) add(f);
}

void CanBitAnalyzer::reset() {
    m_profiles.clear();
}

QVector<CanBitProfile> CanBitAnalyzer::profiles() const {
    QVector<CanBitProfile> result = m_profiles.values().toVector();
    std::sort(result.begin(), result.end(),
              [](const CanBitProfile &a, const CanBitProfile &b) {
                  return a.id < b.id;
              });
    return result;
}

const CanBitProfile *CanBitAnalyzer::profileFor(uint32_t id) const {
    auto it = m_profiles.find(id);
    return (it != m_profiles.end()) ? &it.value() : nullptr;
}

QString CanBitProfile::summary() const {
    QString line = QString("0x%1 (%2 frames, DLC=%3): ")
                   .arg(id, 3, 16, QChar('0'))
                   .arg(frameCount)
                   .arg(maxDlc);
    auto var = varying();
    bool anyVarying = false;
    for (int i = 0; i < static_cast<int>(maxDlc) && i < 8; ++i) {
        if (var[i]) { anyVarying = true; break; }
    }
    if (!anyVarying) {
        line += "all bits static";
    } else {
        for (int i = 0; i < static_cast<int>(maxDlc) && i < 8; ++i) {
            if (var[i]) {
                line += QString("B%1[0x%2] ").arg(i).arg(var[i], 2, 16, QChar('0'));
            }
        }
    }
    return line.trimmed();
}

QString CanBitAnalyzer::report() const {
    auto profs = profiles();
    if (profs.isEmpty())
        return "No frames analysed.";

    QString out;
    for (const auto &p : profs)
        out += p.summary() + "\n";
    return out;
}
