#include "IcSimDecoder.h"
#include <cmath>

bool IcSimDecoder::decode(const CanFrame &frame, IcSimState &state,
                          uint32_t speedId, uint32_t doorId, uint32_t signalId)
{
    if (frame.id == speedId && frame.dlc >= 5) {
        int raw = (static_cast<int>(frame.data[3]) << 8) | frame.data[4];
        state.speedKph = raw / 100.0f;
        state.speedMph = state.speedKph * 0.6213751f;
        return true;
    }
    if (frame.id == doorId && frame.dlc >= 3) {
        state.doorMask = frame.data[2] & 0x0F;
        return true;
    }
    if (frame.id == signalId && frame.dlc >= 1) {
        state.leftSignal  = (frame.data[0] & 0x01) != 0;
        state.rightSignal = (frame.data[0] & 0x02) != 0;
        return true;
    }
    return false;
}

CanFrame IcSimDecoder::buildSpeedFrame(float mph, uint32_t id)
{
    CanFrame f;
    f.id  = id;
    f.dlc = 5;
    int raw = static_cast<int>(std::max(0.f, mph) / 0.6213751f * 100.f);
    f.data[3] = static_cast<uint8_t>((raw >> 8) & 0xFF);
    f.data[4] = static_cast<uint8_t>(raw & 0xFF);
    return f;
}

CanFrame IcSimDecoder::buildDoorFrame(uint8_t lockMask, uint32_t id)
{
    CanFrame f;
    f.id     = id;
    f.dlc    = 3;
    f.data[2] = lockMask & 0x0F;
    return f;
}

CanFrame IcSimDecoder::buildSignalFrame(bool left, bool right, uint32_t id)
{
    CanFrame f;
    f.id     = id;
    f.dlc    = 1;
    f.data[0] = (left ? 0x01u : 0u) | (right ? 0x02u : 0u);
    return f;
}
