#include "BusLoadAnalyzer.h"
#include <algorithm>

// ── Bit-count tables ──────────────────────────────────────────────────────────
//
// Classic CAN, 11-bit standard ID (non-FD):
//   SOF(1) + ID(11) + RTR(1) + IDE(0) + r0(0) + DLC(4) + DATA(8*dlc)
//   + CRC(15) + CRC_DEL(1) + ACK(1) + ACK_DEL(1) + EOF(7) + IFS(3)
//   = 44 + 8*dlc bits (worst-case without stuffing)
//
// Extended, 29-bit ID:
//   SOF(1) + BASE_ID(11) + SRR(1) + IDE(1) + EXT_ID(18) + RTR(1) + r1(0)
//   + r0(0) + DLC(4) + DATA(8*dlc) + CRC(15) + CRC_DEL(1) + ACK(1)
//   + ACK_DEL(1) + EOF(7) + IFS(3)
//   = 64 + 8*dlc bits
//
// CAN FD (simplified, no BRS/ESI overhead modelled):
//   Standard header + FD data (up to 64 bytes) + CRC(21+4 for dlc>10)
//   Approximation: 80 + 8*dlc bits extended, 60 + 8*dlc standard

uint32_t BusLoadAnalyzer::frameBits(bool extended, uint8_t dlc, bool fd) {
    uint8_t effectiveDlc = (fd && dlc > 64) ? 64 : dlc;
    if (fd) {
        // Approximate FD frame overhead
        return (extended ? 80 : 60) + 8u * effectiveDlc;
    }
    uint8_t clampedDlc = (effectiveDlc > 8) ? 8 : effectiveDlc;
    return (extended ? 64 : 44) + 8u * clampedDlc;
}

// ── Constructor / setters ─────────────────────────────────────────────────────

BusLoadAnalyzer::BusLoadAnalyzer(uint32_t baudRateBps)
    : m_baudRate(baudRateBps) {}

void BusLoadAnalyzer::setBaudRate(uint32_t bps) {
    m_baudRate = (bps > 0) ? bps : 500000;
    reset();
}

void BusLoadAnalyzer::setWindowSec(double sec) {
    m_windowSec = (sec > 0.0) ? sec : 1.0;
}

void BusLoadAnalyzer::reset() {
    m_window.clear();
    m_windowBits = 0;
    m_idBits.clear();
    m_idFrames.clear();
    m_seenIds.clear();
    m_peakLoad = 0.0;
}

// ── Frame ingestion ───────────────────────────────────────────────────────────

void BusLoadAnalyzer::processFrame(const CanFrame &frame) {
    uint64_t nowUs = frame.timestamp;
    pruneOld(nowUs);

    uint32_t bits = frameBits(frame.extended, frame.dlc, frame.fd);
    m_window.push_back({nowUs, frame.id, bits});
    m_windowBits           += bits;
    m_idBits[frame.id]     += bits;
    m_idFrames[frame.id]   += 1;
    m_seenIds.insert(frame.id);

    double load = currentLoad();
    if (load > m_peakLoad) m_peakLoad = load;
}

// ── Queries ───────────────────────────────────────────────────────────────────

void BusLoadAnalyzer::pruneOld(uint64_t nowUs) {
    uint64_t cutoffUs = (nowUs > static_cast<uint64_t>(m_windowSec * 1e6))
                      ? nowUs - static_cast<uint64_t>(m_windowSec * 1e6)
                      : 0;
    while (!m_window.empty() && m_window.front().tsUs < cutoffUs) {
        const auto &r = m_window.front();
        m_windowBits -= r.bits;
        m_idBits[r.id]   -= r.bits;
        m_idFrames[r.id] -= 1;
        if (m_idBits[r.id] == 0) {
            m_idBits.erase(r.id);
            m_idFrames.erase(r.id);
        }
        m_window.pop_front();
    }
}

double BusLoadAnalyzer::currentLoad() const {
    if (m_baudRate == 0) return 0.0;
    double windowBaudBits = m_windowSec * static_cast<double>(m_baudRate);
    return static_cast<double>(m_windowBits) / windowBaudBits;
}

double BusLoadAnalyzer::framesPerSec() const {
    return m_windowSec > 0.0
         ? static_cast<double>(m_window.size()) / m_windowSec
         : 0.0;
}

std::vector<BusLoadAnalyzer::IdLoad> BusLoadAnalyzer::topLoaders(size_t n) const {
    std::vector<IdLoad> result;
    result.reserve(m_idBits.size());

    double windowBaudBits = m_windowSec * static_cast<double>(m_baudRate);

    for (const auto &[id, bits] : m_idBits) {
        IdLoad il;
        il.id            = id;
        il.bitsInWindow  = bits;
        il.framesInWindow= m_idFrames.count(id) ? m_idFrames.at(id) : 0;
        il.loadFraction  = (windowBaudBits > 0) ? static_cast<double>(bits) / windowBaudBits : 0.0;
        il.fps           = (m_windowSec > 0.0)
                         ? static_cast<double>(il.framesInWindow) / m_windowSec
                         : 0.0;
        result.push_back(il);
    }

    std::sort(result.begin(), result.end(),
              [](const IdLoad &a, const IdLoad &b) { return a.loadFraction > b.loadFraction; });

    if (result.size() > n) result.resize(n);
    return result;
}
