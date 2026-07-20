#include "CanBusErrorAnalyzer.h"
#include <algorithm>

CanBusErrorAnalyzer::ErrorClass CanBusErrorAnalyzer::classifyId(uint32_t errId) {
    // Priority: first matching bit wins (lowest enum value = highest priority)
    if (errId & ERR_TX_TIMEOUT) return ErrorClass::TxTimeout;
    if (errId & ERR_LOSTARB)    return ErrorClass::LostArbitration;
    if (errId & ERR_CRTL)       return ErrorClass::Controller;
    if (errId & ERR_PROT)       return ErrorClass::Protocol;
    if (errId & ERR_TRX)        return ErrorClass::Transceiver;
    if (errId & ERR_ACK)        return ErrorClass::Ack;
    if (errId & ERR_BUSOFF)     return ErrorClass::BusOff;
    if (errId & ERR_BUSERROR)   return ErrorClass::BusError;
    if (errId & ERR_RESTARTED)  return ErrorClass::Restarted;
    return ErrorClass::Unknown;
}

void CanBusErrorAnalyzer::processFrame(const CanFrame &frame) {
    if (!frame.error) return;

    ErrorClass cls = classifyId(frame.id);
    m_events.push_back({frame.timestamp, cls, frame.id});
    ++m_counts[static_cast<uint8_t>(cls)];
    ++m_total;
    if (frame.timestamp > m_latestTs)
        m_latestTs = frame.timestamp;
}

void CanBusErrorAnalyzer::reset() {
    m_events.clear();
    m_counts.clear();
    m_total   = 0;
    m_latestTs = 0;
}

int CanBusErrorAnalyzer::errorCount(ErrorClass cls) const {
    auto it = m_counts.find(static_cast<uint8_t>(cls));
    return (it != m_counts.end()) ? it->second : 0;
}

bool CanBusErrorAnalyzer::hasBurst(const BurstConfig &cfg) const {
    if (m_total < cfg.thresholdCount) return false;
    // Walk backwards from the end; count events within windowUs of the latest
    int count = 0;
    for (auto it = m_events.rbegin(); it != m_events.rend(); ++it) {
        if (m_latestTs - it->timestampUs > cfg.windowUs) break;
        if (++count >= cfg.thresholdCount) return true;
    }
    return false;
}

std::vector<CanBusErrorAnalyzer::ErrorEvent>
CanBusErrorAnalyzer::recentEvents(uint64_t windowUs) const {
    if (windowUs == 0 || m_events.empty()) return {};
    uint64_t cutoff = (m_latestTs > windowUs) ? (m_latestTs - windowUs) : 0;
    std::vector<ErrorEvent> out;
    for (const auto &e : m_events)
        if (e.timestampUs >= cutoff) out.push_back(e);
    return out;
}

double CanBusErrorAnalyzer::errorRate(uint64_t windowUs) const {
    if (windowUs == 0 || m_events.size() < 2) return 0.0;
    auto recent = recentEvents(windowUs);
    if (recent.size() < 2) return 0.0;
    uint64_t span = recent.back().timestampUs - recent.front().timestampUs;
    if (span == 0) return 0.0;
    return static_cast<double>(recent.size()) / (static_cast<double>(span) / 1e6);
}
