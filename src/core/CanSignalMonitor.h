#pragma once
#include "CanFrame.h"
#include "DbcParser.h"
#include <QString>
#include <QHash>
#include <unordered_map>
#include <string>
#include <cstdint>

/// Current value record for a single DBC signal.
struct SignalValue {
    QString  msgName;
    QString  signalName;
    double   physValue  = 0.0;
    double   rawValue   = 0.0;  // decoded integer before scale/offset
    QString  unit;
    uint64_t lastUpdateUs = 0;  // µs timestamp of last update
    uint64_t updateCount  = 0;

    // Alarm thresholds (NaN = not set)
    double   alarmMin = std::numeric_limits<double>::quiet_NaN();
    double   alarmMax = std::numeric_limits<double>::quiet_NaN();
    bool     alarmActive() const;
};

/// Tracks the live physical value of every signal defined in a DBC.
/// Call update() for each received CAN frame; read allValues() for the dashboard.
class CanSignalMonitor {
public:
    void setDbc(const DbcParser *dbc);

    /// Decode signals in the frame and update tracked values.
    void update(const CanFrame &frame);

    /// All currently tracked signal values (unordered).
    std::vector<SignalValue> allValues() const;

    /// Value for a specific signal (by name), or nullptr if not seen yet.
    const SignalValue *valueFor(const QString &signalName) const;

    /// Set alarm thresholds for a signal.
    void setAlarm(const QString &signalName, double minVal, double maxVal);

    /// Count of signals with active alarms.
    int activeAlarmCount() const;

    /// Signals not updated within staleThresholdUs microseconds.
    std::vector<SignalValue> staleSignals(uint64_t nowUs, uint64_t staleThresholdUs) const;

    void reset();

private:
    const DbcParser *m_dbc = nullptr;
    QHash<QString, SignalValue> m_values;
};
