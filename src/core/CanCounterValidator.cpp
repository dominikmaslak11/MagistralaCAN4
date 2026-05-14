#include "CanCounterValidator.h"

void CanCounterValidator::addConfig(const Config &cfg) {
    State s;
    s.cfg = cfg;
    m_states[cfg.canId] = s;
}

void CanCounterValidator::removeConfig(uint32_t canId) {
    m_states.erase(canId);
}

void CanCounterValidator::reset() {
    for (auto &kv : m_states) {
        kv.second.hasFirst = false;
        kv.second.expected = 0;
        kv.second.stats    = {};
    }
}

uint8_t CanCounterValidator::extractCounter(const CanFrame &frame, const Config &cfg) const {
    uint8_t raw = frame.data[cfg.byteIndex];
    if (cfg.upperNibble)
        return (raw >> 4) & 0x0F;
    return raw;
}

CanCounterValidator::Result CanCounterValidator::update(const CanFrame &frame) {
    auto it = m_states.find(frame.id);
    if (it == m_states.end()) return Result::Ok; // no config for this ID → ignore

    State &s = it->second;
    ++s.stats.totalFrames;

    if (s.cfg.byteIndex < 0 || s.cfg.byteIndex >= frame.dlc)
        return Result::OutOfRange;

    uint8_t counter = extractCounter(frame, s.cfg);

    if (!s.hasFirst) {
        s.hasFirst = true;
        s.expected = static_cast<uint8_t>((counter + 1) % s.cfg.modulus);
        ++s.stats.okCount;
        return Result::Skip;
    }

    if (counter != s.expected) {
        ++s.stats.mismatchCount;
        // Re-sync to current value
        s.expected = static_cast<uint8_t>((counter + 1) % s.cfg.modulus);
        return Result::Mismatch;
    }

    ++s.stats.okCount;
    s.expected = static_cast<uint8_t>((counter + 1) % s.cfg.modulus);
    return Result::Ok;
}

CanCounterValidator::Stats CanCounterValidator::statsFor(uint32_t canId) const {
    auto it = m_states.find(canId);
    return (it != m_states.end()) ? it->second.stats : Stats{};
}
