#pragma once
#include "CanFrame.h"
#include <cstdint>
#include <vector>
#include <unordered_map>

// Decodes and accumulates SocketCAN error frames (frame.error == true).
// Error class is encoded in the frame ID using the CAN_ERR_* bitmask convention.
class CanBusErrorAnalyzer {
public:
    // SocketCAN error-class flags (bits in errored frame's CAN ID)
    static constexpr uint32_t ERR_TX_TIMEOUT  = 0x00000001;
    static constexpr uint32_t ERR_LOSTARB     = 0x00000002;
    static constexpr uint32_t ERR_CRTL        = 0x00000004;
    static constexpr uint32_t ERR_PROT        = 0x00000008;
    static constexpr uint32_t ERR_TRX         = 0x00000010;
    static constexpr uint32_t ERR_ACK         = 0x00000020;
    static constexpr uint32_t ERR_BUSOFF      = 0x00000040;
    static constexpr uint32_t ERR_BUSERROR    = 0x00000080;
    static constexpr uint32_t ERR_RESTARTED   = 0x00000100;

    enum class ErrorClass : uint8_t {
        TxTimeout = 0,
        LostArbitration,
        Controller,
        Protocol,
        Transceiver,
        Ack,
        BusOff,
        BusError,
        Restarted,
        Unknown
    };

    struct ErrorEvent {
        uint64_t   timestampUs = 0;
        ErrorClass cls         = ErrorClass::Unknown;
        uint32_t   rawId       = 0;
    };

    struct BurstConfig {
        int      thresholdCount = 10;         // triggers burst if >= this many errors …
        uint64_t windowUs       = 1'000'000;  // … occur within this window (µs)
    };

    // Process one frame. Only frames where frame.error==true are counted.
    void processFrame(const CanFrame &frame);

    // Reset all state.
    void reset();

    // Total error frames processed.
    int totalErrors() const { return m_total; }

    // Count of errors in a specific class.
    int errorCount(ErrorClass cls) const;

    // True if any BusOff error has been recorded.
    bool isBusOff() const { return errorCount(ErrorClass::BusOff) > 0; }

    // True if at least cfg.thresholdCount errors occurred within cfg.windowUs
    // looking at the most recent events (from the end of the event list).
    bool hasBurst(const BurstConfig &cfg) const;

    // All recorded error events in arrival order.
    const std::vector<ErrorEvent> &events() const { return m_events; }

    // Events within the last windowUs microseconds relative to the latest timestamp.
    std::vector<ErrorEvent> recentEvents(uint64_t windowUs) const;

    // Errors per second based on events in the last windowUs µs.
    // Returns 0.0 if fewer than 2 events or windowUs==0.
    double errorRate(uint64_t windowUs) const;

    // Classify a SocketCAN error-frame ID into its primary ErrorClass.
    // When multiple flags are set, the highest-priority (lowest enum value) wins.
    static ErrorClass classifyId(uint32_t errId);

private:
    std::vector<ErrorEvent>               m_events;
    std::unordered_map<uint8_t, int>      m_counts;  // keyed by static_cast<uint8_t>(ErrorClass)
    int                                   m_total = 0;
    uint64_t                              m_latestTs = 0;
};
