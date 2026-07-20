#include "DbcAutoGenerator.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <unordered_map>

// ── Helpers ───────────────────────────────────────────────────────────────────

namespace {

double median(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

double stddev(const std::vector<double> &v, double mean) {
    if (v.size() < 2) return 0.0;
    double sum = 0.0;
    for (double x : v) sum += (x - mean) * (x - mean);
    return std::sqrt(sum / static_cast<double>(v.size() - 1));
}

} // namespace

// ── Public static methods ─────────────────────────────────────────────────────

double DbcAutoGenerator::computeEntropy(const std::vector<uint8_t> &values) {
    if (values.empty()) return 0.0;
    int hist[256] = {};
    for (uint8_t v : values) hist[v]++;
    double h = 0.0;
    double n = static_cast<double>(values.size());
    for (int c : hist) {
        if (c == 0) continue;
        double p = c / n;
        h -= p * std::log2(p);
    }
    return h;
}

double DbcAutoGenerator::counterFraction(const std::vector<uint8_t> &values) {
    if (values.size() < 2) return 0.0;
    int incrCount = 0;
    for (size_t i = 1; i < values.size(); ++i) {
        int diff = static_cast<int>(values[i]) - static_cast<int>(values[i - 1]);
        if (diff == 1 || diff == -255) // +1 mod 256
            ++incrCount;
    }
    return static_cast<double>(incrCount) / static_cast<double>(values.size() - 1);
}

bool DbcAutoGenerator::isChecksumByte(int byteIdx, const std::vector<CanFrame> &frames, int dlc) {
    if (frames.size() < kMinFrames || dlc < 2) return false;
    int matches = 0;
    for (const auto &f : frames) {
        uint8_t xorVal = 0;
        for (int b = 0; b < dlc; ++b)
            if (b != byteIdx) xorVal ^= f.data[b];
        if (xorVal == f.data[byteIdx]) ++matches;
    }
    return (static_cast<double>(matches) / static_cast<double>(frames.size())) >= 0.85;
}

double DbcAutoGenerator::estimatePeriodMs(const std::vector<CanFrame> &frames) {
    if (frames.size() < 2) return 0.0;
    std::vector<double> intervals;
    intervals.reserve(frames.size() - 1);
    for (size_t i = 1; i < frames.size(); ++i)
        intervals.push_back(
            static_cast<double>(frames[i].timestamp - frames[i - 1].timestamp) / 1000.0);
    return median(intervals);
}

ByteRole DbcAutoGenerator::classifyRole(const AutoByteStats &bs,
                                         bool xorMatch, double ctrFrac) {
    // Constant — no change
    if (bs.uniqueValues.size() == 1)
        return ByteRole::Constant;

    // Checksum wins first (before counter check)
    if (xorMatch)
        return ByteRole::Checksum;

    // Counter: >70% increments by +1 (mod 256)
    if (ctrFrac >= 0.70)
        return ByteRole::Counter;

    // Toggle: exactly 2 values
    if (bs.uniqueValues.size() == 2)
        return ByteRole::Toggle;

    // State machine: ≤8 unique values and low entropy
    if (bs.uniqueValues.size() <= 8 && bs.entropy < 2.5)
        return ByteRole::State;

    // Continuous value signal
    return ByteRole::Value;
}

std::string DbcAutoGenerator::roleName(ByteRole r) {
    switch (r) {
        case ByteRole::Counter:  return "COUNTER";
        case ByteRole::Checksum: return "CHECKSUM";
        case ByteRole::Toggle:   return "TOGGLE";
        case ByteRole::State:    return "STATE";
        case ByteRole::Value:    return "VALUE";
        case ByteRole::Constant: return "CONST";
        default:                 return "UNKNOWN";
    }
}

// ── Per-byte analysis ─────────────────────────────────────────────────────────

AutoByteStats DbcAutoGenerator::analyzeOneByte(int byteIdx,
                                                const std::vector<CanFrame> &frames,
                                                int dlc) {
    AutoByteStats bs;
    std::vector<uint8_t> values;
    values.reserve(frames.size());

    for (const auto &f : frames) {
        if (byteIdx >= dlc) continue;
        uint8_t v = f.data[byteIdx];
        values.push_back(v);
        bs.uniqueValues.insert(v);
        if (v < bs.minVal) bs.minVal = v;
        if (v > bs.maxVal) bs.maxVal = v;
    }

    if (values.empty()) {
        bs.role = ByteRole::Constant;
        return bs;
    }

    bs.entropy = computeEntropy(values);
    double ctrFrac = counterFraction(values);
    bool xorMatch  = isChecksumByte(byteIdx, frames, dlc);
    bs.role = classifyRole(bs, xorMatch, ctrFrac);
    return bs;
}

// ── Signal extraction ─────────────────────────────────────────────────────────

std::vector<AutoSignal> DbcAutoGenerator::buildSignals(
    const std::vector<AutoByteStats> &stats, uint32_t id, int dlc,
    const std::vector<CanFrame> &frames)
{
    std::vector<AutoSignal> sigs;

    // Try to merge adjacent Value bytes into 2-byte signals (big-endian)
    std::vector<bool> merged(dlc, false);

    for (int b = 0; b < dlc - 1; ++b) {
        if (merged[b]) continue;
        if (stats[b].role != ByteRole::Value || stats[b + 1].role != ByteRole::Value) continue;

        // Heuristic: if both bytes have similar entropy and high range, merge
        if (stats[b].entropy > 3.0 && stats[b + 1].entropy > 3.0) {
            // Form a 16-bit signal (big-endian: byte b is MSB)
            std::vector<uint16_t> vals;
            vals.reserve(frames.size());
            for (const auto &f : frames) {
                if (b + 1 >= dlc) continue;
                vals.push_back(static_cast<uint16_t>((f.data[b] << 8) | f.data[b + 1]));
            }
            uint16_t vMin = *std::min_element(vals.begin(), vals.end());
            uint16_t vMax = *std::max_element(vals.begin(), vals.end());
            uint32_t range = vMax - vMin;

            AutoSignal s;
            std::ostringstream nm;
            nm << "SIG_" << std::hex << std::uppercase << std::setw(3) << std::setfill('0')
               << id << "_" << std::dec << b << "_W";
            s.name      = nm.str();
            s.startBit  = b * 8;
            s.bitLength = 16;
            s.role      = ByteRole::Value;
            s.minimum   = vMin;
            s.maximum   = vMax;
            s.scale     = (range > 0) ? (static_cast<double>(range) / 65535.0) : 1.0;
            s.offset    = vMin;
            sigs.push_back(s);
            merged[b]     = true;
            merged[b + 1] = true;
        }
    }

    // Remaining bytes: one signal per byte
    for (int b = 0; b < dlc; ++b) {
        if (merged[b]) continue;
        const auto &bs = stats[b];

        // Skip constants unless the whole DLC is constant (status byte)
        if (bs.role == ByteRole::Constant && dlc > 1) continue;

        AutoSignal s;
        std::ostringstream nm;
        nm << "SIG_" << std::hex << std::uppercase << std::setw(3) << std::setfill('0')
           << id << "_" << std::dec << b;
        if (bs.role == ByteRole::Counter)  nm << "_CTR";
        if (bs.role == ByteRole::Checksum) nm << "_CRC";
        if (bs.role == ByteRole::Toggle)   nm << "_BOOL";
        if (bs.role == ByteRole::State)    nm << "_STATE";
        s.name      = nm.str();
        s.startBit  = b * 8;
        s.bitLength = 8;
        s.role      = bs.role;
        s.minimum   = bs.minVal;
        s.maximum   = bs.maxVal;
        uint8_t range = bs.maxVal - bs.minVal;
        s.scale     = (range > 0) ? (static_cast<double>(range) / 255.0) : 1.0;
        s.offset    = bs.minVal;
        sigs.push_back(s);
    }

    // Sort by startBit
    std::sort(sigs.begin(), sigs.end(),
              [](const AutoSignal &a, const AutoSignal &b) { return a.startBit < b.startBit; });
    return sigs;
}

// ── Main entry point ──────────────────────────────────────────────────────────

std::vector<AutoMessage> DbcAutoGenerator::analyze(const std::vector<CanFrame> &frames,
                                                    uint64_t /*observationTimeUs*/) {
    // Group frames by CAN ID
    std::unordered_map<uint32_t, std::vector<CanFrame>> byId;
    byId.reserve(256);
    for (const auto &f : frames)
        byId[f.id].push_back(f);

    std::vector<AutoMessage> result;
    result.reserve(byId.size());

    for (auto &[id, idFrames] : byId) {
        if (static_cast<int>(idFrames.size()) < kMinFrames) continue;

        // Sort by timestamp
        std::sort(idFrames.begin(), idFrames.end(),
                  [](const CanFrame &a, const CanFrame &b) { return a.timestamp < b.timestamp; });

        AutoMessage msg;
        msg.id         = id;
        msg.frameCount = idFrames.size();
        msg.dlc        = idFrames.front().dlc;

        // Classify frame timing
        double periodMs = estimatePeriodMs(idFrames);
        msg.periodMs    = periodMs;
        if (periodMs > 0) {
            // Compute coefficient of variation
            std::vector<double> ivals;
            ivals.reserve(idFrames.size() - 1);
            for (size_t i = 1; i < idFrames.size(); ++i)
                ivals.push_back(static_cast<double>(idFrames[i].timestamp - idFrames[i - 1].timestamp) / 1000.0);
            double cv = stddev(ivals, periodMs) / (periodMs + 1e-9);
            if (cv < 0.25 && periodMs > 0)
                msg.timing = "periodic";
            else if (idFrames.size() < 20)
                msg.timing = "sporadic";
            else
                msg.timing = "event";
        } else {
            msg.timing = "sporadic";
        }

        // Analyze each byte position
        msg.byteStats.reserve(msg.dlc);
        for (int b = 0; b < msg.dlc; ++b)
            msg.byteStats.push_back(analyzeOneByte(b, idFrames, msg.dlc));

        // Build signals
        msg.sigs = buildSignals(msg.byteStats, id, msg.dlc, idFrames);

        result.push_back(std::move(msg));
    }

    // Sort by ID for consistent output
    std::sort(result.begin(), result.end(),
              [](const AutoMessage &a, const AutoMessage &b) { return a.id < b.id; });
    return result;
}

// ── Convert to DbcMessage ─────────────────────────────────────────────────────

std::vector<DbcMessage> DbcAutoGenerator::toDbcMessages(const std::vector<AutoMessage> &msgs) {
    std::vector<DbcMessage> out;
    out.reserve(msgs.size());

    for (const auto &am : msgs) {
        DbcMessage dm;
        dm.id  = am.id;
        dm.dlc = am.dlc;

        std::ostringstream nm;
        nm << "MSG_" << std::hex << std::uppercase << std::setw(3) << std::setfill('0') << am.id;
        if (am.timing == "periodic" && am.periodMs > 0)
            nm << "_" << static_cast<int>(am.periodMs) << "ms";
        dm.name = QString::fromStdString(nm.str());

        for (const auto &as : am.sigs) {
            DbcSignal ds;
            ds.name          = QString::fromStdString(as.name);
            ds.startBit      = as.startBit;
            ds.length        = as.bitLength;
            ds.isLittleEndian= true;
            ds.isSigned      = as.isSigned;
            ds.scale         = as.scale;
            ds.offset        = as.offset;
            ds.minimum       = as.minimum;
            ds.maximum       = as.maximum;
            ds.unit          = QString::fromStdString(as.unit);
            dm.sigList.append(ds);
        }
        out.push_back(std::move(dm));
    }
    return out;
}
