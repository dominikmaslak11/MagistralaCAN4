#include "CanSignalEncoder.h"
#include <algorithm>
#include <cmath>

int64_t CanSignalEncoder::physToRaw(const DbcSignal &sig, double physValue) {
    double rawD = (sig.scale != 0.0)
                ? std::round((physValue - sig.offset) / sig.scale)
                : 0.0;

    int64_t raw = static_cast<int64_t>(rawD);

    if (sig.isSigned) {
        int64_t lo = -(1LL << (sig.length - 1));
        int64_t hi =  (1LL << (sig.length - 1)) - 1;
        raw = std::clamp(raw, lo, hi);
    } else {
        int64_t hi = (sig.length >= 64) ? INT64_MAX : ((1LL << sig.length) - 1);
        raw = std::clamp(raw, int64_t(0), hi);
    }
    return raw;
}

bool CanSignalEncoder::encodeSignal(const DbcSignal &sig, double physValue,
                                     uint8_t *data, int dlc) {
    if (!data || sig.length <= 0 || sig.length > 64) return false;

    int64_t raw = physToRaw(sig, physValue);
    uint64_t uraw = static_cast<uint64_t>(raw);

    if (sig.isLittleEndian) {
        // Intel byte order: LSB at startBit, bits go upward
        int bitPos  = sig.startBit;
        int bitsLeft = sig.length;
        while (bitsLeft > 0) {
            int byteIdx    = bitPos / 8;
            int bitInByte  = bitPos % 8;
            if (byteIdx >= dlc) return false;
            int bitsAvail  = 8 - bitInByte;
            int bitsNow    = std::min(bitsAvail, bitsLeft);
            uint8_t mask   = static_cast<uint8_t>(((1U << bitsNow) - 1) << bitInByte);
            data[byteIdx] &= ~mask;
            data[byteIdx] |= static_cast<uint8_t>((uraw & ((1U << bitsNow) - 1)) << bitInByte);
            uraw   >>= bitsNow;
            bitsLeft -= bitsNow;
            bitPos   += bitsNow;
        }
    } else {
        // Motorola byte order: MSB at startBit, bits go downward
        int bitPos   = sig.startBit;
        int bitsLeft = sig.length;
        int rawShift = sig.length; // we consume MSB first
        while (bitsLeft > 0) {
            if (bitPos < 0) return false;
            int byteIdx       = bitPos / 8;
            int bitInByte     = bitPos % 8;
            if (byteIdx >= dlc) return false;
            int bitsAvail     = bitInByte + 1;
            int bitsNow       = std::min(bitsAvail, bitsLeft);
            int startBitInByte = bitInByte - bitsNow + 1;
            rawShift -= bitsNow;
            uint64_t chunk    = (uraw >> rawShift) & ((1U << bitsNow) - 1);
            uint8_t mask      = static_cast<uint8_t>(((1U << bitsNow) - 1) << startBitInByte);
            data[byteIdx]    &= ~mask;
            data[byteIdx]    |= static_cast<uint8_t>(chunk << startBitInByte);
            bitsLeft -= bitsNow;
            bitPos   -= bitsNow;
        }
    }
    return true;
}

CanFrame CanSignalEncoder::buildFrame(const DbcMessage &msg,
                                       const QHash<QString, double> &values,
                                       uint32_t canId) {
    CanFrame f{};
    f.id  = (canId != 0) ? canId : msg.id;
    f.dlc = static_cast<uint8_t>(std::min(msg.dlc, 64));

    for (const auto &sig : msg.sigList) {
        auto it = values.find(sig.name);
        if (it == values.end()) continue;
        encodeSignal(sig, it.value(), f.data.data(), f.dlc);
    }
    return f;
}
