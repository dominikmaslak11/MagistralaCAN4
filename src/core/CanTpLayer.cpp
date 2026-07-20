#include "CanTpLayer.h"
#include <algorithm>
#include <cstring>

CanTpLayer::CanTpLayer(MessageCallback cb) : m_onMessage(std::move(cb)) {}

// ── Public ───────────────────────────────────────────────────

void CanTpLayer::processFrame(const CanFrame &frame) {
    if (frame.dlc < 1) return;
    switch (frameType(frame.data[0])) {
        case CanTpFrameType::SingleFrame:      handleSF(frame); break;
        case CanTpFrameType::FirstFrame:       handleFF(frame); break;
        case CanTpFrameType::ConsecutiveFrame: handleCF(frame); break;
        case CanTpFrameType::FlowControl:      break; // receiver ignores FC
        default: break;
    }
}

void CanTpLayer::sendMessage(uint32_t canId,
                             const std::vector<uint8_t> &payload,
                             OutputCallback out) {
    if (!out || payload.empty()) return;

    if (payload.size() <= 7) {
        // Single Frame
        CanFrame f;
        f.id  = canId;
        f.dlc = static_cast<uint8_t>(1 + payload.size());
        f.data[0] = static_cast<uint8_t>(payload.size()); // PCI: 0x0N
        std::copy(payload.begin(), payload.end(), f.data.begin() + 1);
        out(f);
        return;
    }

    // Multi-frame: FF then CFs (no FC wait — caller's responsibility)
    uint32_t total = static_cast<uint32_t>(payload.size());
    if (total > 4095) total = 4095;  // ISO 15765-2 classic CAN limit

    // First Frame
    CanFrame ff;
    ff.id     = canId;
    ff.dlc    = 8;
    ff.data[0] = static_cast<uint8_t>(0x10 | ((total >> 8) & 0x0F));
    ff.data[1] = static_cast<uint8_t>(total & 0xFF);
    size_t ffBytes = std::min<size_t>(6, payload.size());
    std::copy(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(ffBytes), ff.data.begin() + 2);
    out(ff);

    // Consecutive Frames
    size_t offset = ffBytes;
    uint8_t sn = 1;
    while (offset < total) {
        CanFrame cf;
        cf.id  = canId;
        size_t chunk = std::min<size_t>(7, total - offset);
        cf.dlc = static_cast<uint8_t>(1 + chunk);
        cf.data[0] = static_cast<uint8_t>(0x20 | (sn & 0x0F));
        std::copy(payload.begin() + static_cast<std::ptrdiff_t>(offset),
                  payload.begin() + static_cast<std::ptrdiff_t>(offset + chunk),
                  cf.data.begin() + 1);
        out(cf);
        offset += chunk;
        sn = (sn % 15) + 1;  // SN wraps 1..15
    }
}

void CanTpLayer::purgeTimedOut(uint64_t nowUs, uint64_t timeoutUs) {
    for (auto it = m_sessions.begin(); it != m_sessions.end(); ) {
        if (nowUs - it->second.lastFrameTs > timeoutUs) {
            CanTpMessage msg;
            msg.canId     = it->second.canId;
            msg.timestamp = it->second.firstFrameTs;
            msg.error     = "CAN TP session timeout";
            deliver(msg);
            it = m_sessions.erase(it);
        } else {
            ++it;
        }
    }
}

// ── Private ──────────────────────────────────────────────────

void CanTpLayer::handleSF(const CanFrame &f) {
    uint8_t len = f.data[0] & 0x0F;
    if (len == 0 || len > 7 || len > f.dlc - 1) return;

    // Remove any pending session for this ID
    m_sessions.erase(f.id);

    CanTpMessage msg;
    msg.canId     = f.id;
    msg.timestamp = f.timestamp;
    msg.payload.assign(f.data.begin() + 1, f.data.begin() + 1 + len);
    msg.complete  = true;
    deliver(msg);
}

void CanTpLayer::handleFF(const CanFrame &f) {
    if (f.dlc < 3) return;

    uint32_t total = (static_cast<uint32_t>(f.data[0] & 0x0F) << 8)
                   |  static_cast<uint32_t>(f.data[1]);
    if (total < 8 || total > 4095) return;

    Session s;
    s.canId        = f.id;
    s.totalLength  = total;
    s.expectedSN   = 1;
    s.firstFrameTs = f.timestamp;
    s.lastFrameTs  = f.timestamp;

    // FF carries bytes 2..7 (up to 6 bytes of payload)
    size_t ffBytes = std::min<size_t>(f.dlc - 2, static_cast<size_t>(total));
    s.buffer.reserve(total);
    s.buffer.insert(s.buffer.end(), f.data.begin() + 2, f.data.begin() + 2 + ffBytes);

    m_sessions[f.id] = std::move(s);
    // Sender expects a Flow Control here — external layer must send it
}

void CanTpLayer::handleCF(const CanFrame &f) {
    auto it = m_sessions.find(f.id);
    if (it == m_sessions.end()) return;

    Session &s = it->second;
    uint8_t sn = f.data[0] & 0x0F;

    if (sn != s.expectedSN) {
        // Sequence number mismatch — abort session
        CanTpMessage msg;
        msg.canId     = s.canId;
        msg.timestamp = s.firstFrameTs;
        msg.error     = "CAN TP sequence number error (expected "
                      + std::to_string(s.expectedSN) + ", got "
                      + std::to_string(sn) + ")";
        deliver(msg);
        m_sessions.erase(it);
        return;
    }

    s.lastFrameTs = f.timestamp;
    s.expectedSN  = (s.expectedSN % 15) + 1;  // next SN wraps 1..15

    size_t remaining = s.totalLength - s.buffer.size();
    size_t chunk     = std::min<size_t>(f.dlc - 1, remaining);
    s.buffer.insert(s.buffer.end(), f.data.begin() + 1, f.data.begin() + 1 + chunk);

    if (s.buffer.size() >= s.totalLength) {
        CanTpMessage msg;
        msg.canId     = s.canId;
        msg.timestamp = s.firstFrameTs;
        msg.payload   = std::move(s.buffer);
        msg.complete  = true;
        deliver(msg);
        m_sessions.erase(it);
    }
}

void CanTpLayer::deliver(const CanTpMessage &msg) {
    if (m_onMessage) m_onMessage(msg);
}

