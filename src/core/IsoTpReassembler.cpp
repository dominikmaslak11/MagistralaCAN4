#include "IsoTpReassembler.h"
#include <QtGlobal>

bool IsoTpReassembler::isObdId(uint32_t id) {
    return id == 0x7DF || (id >= 0x7E0 && id <= 0x7EF);
}

bool IsoTpReassembler::feed(const CanFrame &frame, Result &out) {
    if (!isObdId(frame.id) || frame.dlc < 1)
        return false;

    uint8_t pci  = frame.data[0];
    uint8_t type = (pci >> 4) & 0xF;

    // ── Single Frame ──────────────────────────────────────────────────────────
    if (type == 0x0) {
        uint8_t len = pci & 0x0F;
        if (len == 0 || static_cast<int>(len) > frame.dlc - 1)
            return false;

        out.canId     = frame.id;
        out.timestamp = frame.timestamp;
        out.payload   = QByteArray(reinterpret_cast<const char *>(frame.data.data() + 1), len);
        m_sessions.remove(frame.id);
        return true;
    }

    // ── First Frame ───────────────────────────────────────────────────────────
    if (type == 0x1) {
        if (frame.dlc < 8)
            return false;

        uint16_t total = (static_cast<uint16_t>(pci & 0x0F) << 8)
                       | static_cast<uint8_t>(frame.data[1]);
        if (total < 8)  // wouldn't need FF for payload that fits in a SF
            return false;

        Session &s  = m_sessions[frame.id];
        s.totalLen  = total;
        s.timestamp = frame.timestamp;
        s.nextSeq   = 1;
        s.buf.clear();
        // FF carries first 6 payload bytes in data[2..7]
        s.buf.append(reinterpret_cast<const char *>(frame.data.data() + 2), 6);
        s.active = true;
        return false;
    }

    // ── Consecutive Frame ─────────────────────────────────────────────────────
    if (type == 0x2) {
        auto it = m_sessions.find(frame.id);
        if (it == m_sessions.end() || !it->active)
            return false;

        uint8_t seq = pci & 0x0F;
        if (seq != (it->nextSeq & 0x0F)) {
            // Sequence error — abandon and wait for a fresh First Frame
            it->active = false;
            return false;
        }
        it->nextSeq++;

        int remaining = static_cast<int>(it->totalLen) - static_cast<int>(it->buf.size());
        int take      = qMin(remaining, static_cast<int>(frame.dlc) - 1);
        if (take > 0)
            it->buf.append(reinterpret_cast<const char *>(frame.data.data() + 1), take);

        if (it->buf.size() >= static_cast<int>(it->totalLen)) {
            out.canId     = frame.id;
            out.timestamp = it->timestamp;
            out.payload   = it->buf.left(it->totalLen);
            it->active    = false;
            return true;
        }
        return false;
    }

    // Flow Control (0x3) and unknown types — no action needed on receive side
    return false;
}

void IsoTpReassembler::reset(uint32_t canId) {
    m_sessions.remove(canId);
}

void IsoTpReassembler::resetAll() {
    m_sessions.clear();
}
