#pragma once
#include "CanFrame.h"
#include <cstdint>

struct IcSimState {
    float   speedMph   = 0.f;
    float   speedKph   = 0.f;
    uint8_t doorMask   = 0x0F; // bit i=1 → door(i+1) locked; ICSim default: all locked
    bool    leftSignal  = false;
    bool    rightSignal = false;

    bool isDoorLocked(int door) const { return (doorMask >> door) & 1u; }
};

// Decodes ICSim CAN frames and builds control frames for ICSim.
// ICSim default CAN IDs (without -r/-s seed option):
//   Speed:   0x244 (580), bytes 3-4: big-endian, raw = kph*100
//   Doors:   0x19B (411), byte  2:   bits 0-3, 1=locked
//   Signals: 0x188 (392), byte  0:   bit0=left, bit1=right
class IcSimDecoder {
public:
    static constexpr uint32_t SPEED_ID  = 0x244;
    static constexpr uint32_t DOOR_ID   = 0x19B;
    static constexpr uint32_t SIGNAL_ID = 0x188;

    // Returns true if frame is an ICSim frame and state was updated.
    // speedId/doorId/signalId allow override for seeded ICSim (-s <seed>).
    [[nodiscard]] static bool decode(const CanFrame &frame, IcSimState &state,
                                     uint32_t speedId  = SPEED_ID,
                                     uint32_t doorId   = DOOR_ID,
                                     uint32_t signalId = SIGNAL_ID);

    static CanFrame buildSpeedFrame(float mph, uint32_t id = SPEED_ID);
    static CanFrame buildDoorFrame(uint8_t lockMask, uint32_t id = DOOR_ID);
    static CanFrame buildSignalFrame(bool left, bool right, uint32_t id = SIGNAL_ID);
};
