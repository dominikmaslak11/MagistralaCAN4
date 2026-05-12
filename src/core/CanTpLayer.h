#pragma once
#include "CanFrame.h"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// ISO 15765-2 (CAN TP) reassembly and send layer.
// Handles SF/FF/CF/FC for payloads up to 4095 bytes.

enum class CanTpFrameType : uint8_t {
    SingleFrame      = 0x0,
    FirstFrame       = 0x1,
    ConsecutiveFrame = 0x2,
    FlowControl      = 0x3,
    Unknown          = 0xF,
};

enum class CanTpFcStatus : uint8_t {
    ContinueToSend = 0x00,
    Wait           = 0x01,
    Overflow       = 0x02,
};

struct CanTpMessage {
    uint32_t             canId     = 0;
    uint64_t             timestamp = 0;   // µs, from first frame
    std::vector<uint8_t> payload;         // fully reassembled UDS payload
    bool                 complete  = false;
    std::string          error;
};

class CanTpLayer {
public:
    using MessageCallback = std::function<void(const CanTpMessage &)>;
    using OutputCallback  = std::function<void(const CanFrame &)>;

    explicit CanTpLayer(MessageCallback cb = nullptr);

    // Feed a received CAN frame into the state machine.
    void processFrame(const CanFrame &frame);

    // Transmit a payload (splits into SF or FF+CFs automatically).
    // Calls out() synchronously for each physical CAN frame.
    // For multi-frame, does NOT wait for FC — use with loopback/test harness.
    void sendMessage(uint32_t canId, const std::vector<uint8_t> &payload,
                     OutputCallback out);

    void setMessageCallback(MessageCallback cb) { m_onMessage = cb; }

    // Purge sessions that have not received a frame within timeoutUs.
    void purgeTimedOut(uint64_t nowUs, uint64_t timeoutUs = 1'000'000);

    size_t activeSessions() const { return m_sessions.size(); }

    // Expose frame type from first byte (for external inspection)
    static CanTpFrameType frameType(uint8_t pci)
    { return static_cast<CanTpFrameType>((pci >> 4) & 0x0F); }

private:
    struct Session {
        uint32_t             canId       = 0;
        uint32_t             totalLength = 0;
        uint8_t              expectedSN  = 1;   // next expected CF SN (1..15, wraps)
        uint64_t             firstFrameTs= 0;
        uint64_t             lastFrameTs = 0;
        std::vector<uint8_t> buffer;
    };

    void handleSF(const CanFrame &f);
    void handleFF(const CanFrame &f);
    void handleCF(const CanFrame &f);
    void deliver(const CanTpMessage &msg);

    MessageCallback                           m_onMessage;
    std::unordered_map<uint32_t, Session>     m_sessions; // key = canId
};
