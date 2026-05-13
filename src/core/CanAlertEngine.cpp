#include "CanAlertEngine.h"
#include <QDateTime>
#include <chrono>
#include <cmath>

static constexpr int kRateWindow    = 20;   // keep last 20 timestamps per ID
static constexpr int kRateMinSamples = 8;   // need at least 8 samples before baseline is stable

CanAlertEngine::CanAlertEngine(QObject *parent) : QObject(parent) {}

void CanAlertEngine::addRule(const CanAlertRule &rule) { m_rules.push_back(rule); }

void CanAlertEngine::removeRule(int idx) {
    if (idx >= 0 && idx < static_cast<int>(m_rules.size()))
        m_rules.erase(m_rules.begin() + idx);
}

void CanAlertEngine::updateRule(int idx, const CanAlertRule &rule) {
    if (idx >= 0 && idx < static_cast<int>(m_rules.size()))
        m_rules[idx] = rule;
}

void CanAlertEngine::clearRules() { m_rules.clear(); }

void CanAlertEngine::reset() {
    m_seenIds.clear();
    m_knownDlc.clear();
    m_rateState.clear();
    m_totalAlerts = 0;
}

void CanAlertEngine::evaluate(const CanFrame &frame, uint64_t tsUs) {
    if (tsUs == 0) {
        using namespace std::chrono;
        tsUs = static_cast<uint64_t>(
            duration_cast<microseconds>(system_clock::now().time_since_epoch()).count());
    }

    for (const auto &rule : m_rules) {
        if (!rule.enabled) continue;

        switch (rule.type) {
        case AlertType::NewCanId:
            if (m_seenIds.find(frame.id) == m_seenIds.end())
                fire(rule, frame, tsUs,
                     QString("New CAN ID 0x%1 (DLC=%2)").arg(frame.id, 0, 16).arg(frame.dlc));
            break;

        case AlertType::DlcChange: {
            auto it = m_knownDlc.find(frame.id);
            if (it != m_knownDlc.end() && it->second != frame.dlc)
                fire(rule, frame, tsUs,
                     QString("DLC changed for 0x%1: was %2, now %3")
                         .arg(frame.id, 0, 16).arg(it->second).arg(frame.dlc));
            break;
        }

        case AlertType::ByteValue:
            if (evaluateByteValue(rule, frame))
                fire(rule, frame, tsUs,
                     QString("ID 0x%1 byte[%2]=%3 matched rule '%4'")
                         .arg(frame.id, 0, 16)
                         .arg(rule.byteIdx)
                         .arg(frame.dlc > rule.byteIdx ? frame.data[rule.byteIdx] : 0)
                         .arg(rule.name));
            break;

        case AlertType::RateAnomaly:
            if (evaluateRateAnomaly(rule, frame, tsUs))
                fire(rule, frame, tsUs,
                     QString("Rate anomaly for 0x%1 (threshold ±%2%)")
                         .arg(frame.id, 0, 16).arg(rule.rateDeviationPct, 0, 'f', 0));
            break;

        case AlertType::IsolationForestScore:
            if (rule.scorer) {
                double score = rule.scorer();
                if (score > rule.isThreshold)
                    fire(rule, frame, tsUs,
                         QString("IsoForest score %1 > threshold %2 (0x%3)")
                             .arg(score, 0, 'f', 3)
                             .arg(rule.isThreshold, 0, 'f', 2)
                             .arg(frame.id, 0, 16));
            }
            break;
        }
    }

    // Update learned state AFTER evaluating (so first-frame triggers NewCanId/DlcChange)
    m_seenIds.insert(frame.id);
    m_knownDlc.emplace(frame.id, frame.dlc); // only inserts if not present

    // Always update rate state
    auto &rs = m_rateState[frame.id];
    rs.timestamps.push_back(tsUs);
    if (static_cast<int>(rs.timestamps.size()) > kRateWindow)
        rs.timestamps.pop_front();
}

bool CanAlertEngine::evaluateByteValue(const CanAlertRule &r, const CanFrame &f) const {
    if (!r.matchAny && f.id != r.targetId) return false;
    if (r.matchAny  && f.id != r.targetId && r.targetId != 0) return false;
    if (r.byteIdx < 0 || r.byteIdx >= f.dlc || r.byteIdx >= 8) return false;

    uint8_t v = f.data[r.byteIdx];
    switch (r.op) {
    case ByteOp::GT:  return v >  r.threshold;
    case ByteOp::LT:  return v <  r.threshold;
    case ByteOp::GTE: return v >= r.threshold;
    case ByteOp::LTE: return v <= r.threshold;
    case ByteOp::EQ:  return v == r.threshold;
    case ByteOp::NEQ: return v != r.threshold;
    }
    return false;
}

bool CanAlertEngine::evaluateRateAnomaly(const CanAlertRule &r, const CanFrame &f, uint64_t tsUs) {
    auto &rs = m_rateState[f.id];

    // Update baseline while in learning phase
    if (rs.sampleCount < kRateMinSamples * 2) {
        if (!rs.timestamps.empty()) ++rs.sampleCount;

        if (rs.timestamps.size() >= static_cast<size_t>(kRateMinSamples)) {
            // compute average interval from current window
            double sum = 0.0;
            for (size_t i = 1; i < rs.timestamps.size(); ++i)
                sum += static_cast<double>(rs.timestamps[i] - rs.timestamps[i-1]);
            double avgIntervalUs = sum / (rs.timestamps.size() - 1);
            if (avgIntervalUs > 0.0)
                rs.baselineHz = 1e6 / avgIntervalUs;
        }
        return false; // still learning
    }

    if (rs.baselineHz <= 0.0 || rs.timestamps.size() < 2) return false;

    // Current rate from last 2 timestamps
    if (rs.timestamps.size() < 2) return false;
    auto it = rs.timestamps.rbegin();
    uint64_t last = *it++;
    uint64_t prev = *it;
    if (last <= prev) return false;
    double currentHz = 1e6 / static_cast<double>(last - prev);

    double deviation = std::abs(currentHz - rs.baselineHz) / rs.baselineHz * 100.0;
    return deviation > r.rateDeviationPct;
}

void CanAlertEngine::fire(const CanAlertRule &r, const CanFrame &f, uint64_t tsUs, const QString &desc) {
    ++m_totalAlerts;
    CanAlert alert;
    alert.ruleName    = r.name;
    alert.description = desc;
    alert.frame       = f;
    alert.timestampUs = tsUs;
    emit alertTriggered(alert);
}
