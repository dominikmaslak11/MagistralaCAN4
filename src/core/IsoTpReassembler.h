#pragma once
#include <cstdint>
#include <QByteArray>
#include <QHash>
#include "CanFrame.h"

// ISO 15765-2 (ISO-TP) reassembler for OBD-II CAN IDs (0x7DF / 0x7E0-0x7EF).
// Handles Single Frame, First Frame, and Consecutive Frame PDUs.
// Flow Control frames (0x3x) are silently ignored — the sender is responsible
// for timing, and we only consume incoming ECU responses here.
class IsoTpReassembler {
public:
    struct Result {
        uint32_t   canId     = 0;
        uint64_t   timestamp = 0;
        QByteArray payload;  // complete OBD payload starting from the mode byte (no ISO-TP header)
    };

    // Feed one CAN frame. Returns true (and populates `out`) when a complete
    // ISO-TP PDU is assembled: immediately for Single Frames, on the last
    // Consecutive Frame for multi-frame sequences.
    bool feed(const CanFrame &frame, Result &out);

    void reset(uint32_t canId);
    void resetAll();

private:
    static bool isObdId(uint32_t id);

    struct Session {
        uint16_t   totalLen  = 0;
        uint64_t   timestamp = 0;
        uint8_t    nextSeq   = 1;
        QByteArray buf;
        bool       active    = false;
    };

    QHash<uint32_t, Session> m_sessions;
};
