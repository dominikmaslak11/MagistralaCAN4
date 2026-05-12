#include "CanSignalMonitor.h"
#include <cmath>
#include <algorithm>

bool SignalValue::alarmActive() const {
    if (!std::isnan(alarmMin) && physValue < alarmMin) return true;
    if (!std::isnan(alarmMax) && physValue > alarmMax) return true;
    return false;
}

void CanSignalMonitor::setDbc(const DbcParser *dbc) {
    m_dbc = dbc;
}

void CanSignalMonitor::update(const CanFrame &frame) {
    if (!m_dbc) return;

    DbcMessage msg = m_dbc->messageForId(frame.id);
    if (msg.id == 0 && msg.sigList.isEmpty()) return;

    QHash<QString, double> decoded = m_dbc->decodeSignals(frame.id, frame.data.data(), frame.dlc);

    for (auto &sig : msg.sigList) {
        auto it = decoded.find(sig.name);
        if (it == decoded.end()) continue;

        double phys = it.value();
        double raw  = (sig.scale != 0.0) ? (phys - sig.offset) / sig.scale : phys;

        auto &sv = m_values[sig.name];
        sv.msgName      = msg.name;
        sv.signalName   = sig.name;
        sv.physValue    = phys;
        sv.rawValue     = raw;
        sv.unit         = sig.unit;
        sv.lastUpdateUs = frame.timestamp;
        ++sv.updateCount;
    }
}

std::vector<SignalValue> CanSignalMonitor::allValues() const {
    std::vector<SignalValue> out;
    out.reserve(m_values.size());
    for (auto &v : m_values) out.push_back(v);
    std::sort(out.begin(), out.end(),
              [](const SignalValue &a, const SignalValue &b) {
                  return a.signalName < b.signalName;
              });
    return out;
}

const SignalValue *CanSignalMonitor::valueFor(const QString &signalName) const {
    auto it = m_values.find(signalName);
    return it != m_values.end() ? &it.value() : nullptr;
}

void CanSignalMonitor::setAlarm(const QString &signalName, double minVal, double maxVal) {
    m_values[signalName].alarmMin = minVal;
    m_values[signalName].alarmMax = maxVal;
}

int CanSignalMonitor::activeAlarmCount() const {
    int count = 0;
    for (auto &v : m_values) if (v.alarmActive()) ++count;
    return count;
}

std::vector<SignalValue> CanSignalMonitor::staleSignals(uint64_t nowUs, uint64_t staleThresholdUs) const {
    std::vector<SignalValue> out;
    for (auto &v : m_values) {
        if (nowUs > v.lastUpdateUs && (nowUs - v.lastUpdateUs) > staleThresholdUs)
            out.push_back(v);
    }
    return out;
}

void CanSignalMonitor::reset() {
    m_values.clear();
}
