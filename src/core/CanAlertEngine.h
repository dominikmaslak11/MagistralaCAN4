#pragma once
#include "CanFrame.h"
#include <QObject>
#include <QString>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <functional>

enum class AlertType {
    NewCanId,            // ID seen for the first time in this session
    DlcChange,           // DLC differs from first-seen DLC for this ID
    ByteValue,           // frame.data[byteIdx] op threshold
    RateAnomaly,         // frame rate deviates more than N% from learned baseline
    IsolationForestScore,// injected ML scorer() > threshold
};

enum class AlertAction {
    Log,         // add to alert log only
    Tray,        // system tray notification only
    Both,        // log + tray
};

enum class ByteOp { GT, LT, EQ, NEQ, GTE, LTE };

struct CanAlertRule {
    QString   name;
    AlertType type   = AlertType::NewCanId;
    AlertAction action = AlertAction::Both;
    bool      enabled = true;

    // ByteValue rule parameters
    uint32_t  targetId  = 0;   // 0 = match any ID
    bool      matchAny  = true;
    int       byteIdx   = 0;
    ByteOp    op        = ByteOp::GT;
    uint8_t   threshold = 0;

    // RateAnomaly parameters
    double    rateDeviationPct = 50.0; // trigger if current rate deviates >N%

    // IsolationForestScore parameters
    double    isThreshold = 0.5;             // trigger if scorer() > this value
    std::function<double()> scorer;          // injected ML scoring function
};

struct CanAlert {
    QString  ruleName;
    QString  description;
    CanFrame frame;
    uint64_t timestampUs = 0;
};

class CanAlertEngine : public QObject {
    Q_OBJECT
public:
    explicit CanAlertEngine(QObject *parent = nullptr);

    void addRule(const CanAlertRule &rule);
    void removeRule(int idx);
    void updateRule(int idx, const CanAlertRule &rule);
    void clearRules();
    const std::vector<CanAlertRule> &rules() const { return m_rules; }

    void evaluate(const CanFrame &frame, uint64_t tsUs = 0);
    void submitExternalAlert(const CanAlert &alert);
    void reset();

    int totalAlerts() const { return m_totalAlerts; }

signals:
    void alertTriggered(const CanAlert &alert);

private:
    bool evaluateByteValue(const CanAlertRule &r, const CanFrame &f) const;
    bool evaluateRateAnomaly(const CanAlertRule &r, const CanFrame &f, uint64_t tsUs);
    void fire(const CanAlertRule &r, const CanFrame &f, uint64_t tsUs, const QString &desc);

    std::vector<CanAlertRule> m_rules;

    std::unordered_set<uint32_t>          m_seenIds;
    std::unordered_map<uint32_t, uint8_t> m_knownDlc;

    // RateAnomaly: sliding window of last 20 inter-frame intervals per ID
    struct RateState {
        std::deque<uint64_t> timestamps; // last N arrival timestamps (µs)
        double baselineHz = 0.0;
        int    sampleCount = 0;
    };
    std::unordered_map<uint32_t, RateState> m_rateState;

    int m_totalAlerts = 0;
};
