#pragma once
#include "CanFrame.h"
#include "ICanDriver.h"
#include <QObject>
#include <QThread>
#include <QMutex>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>

/// Pure-C++ CAN gateway engine.
/// Reads frames from sourceDriver, applies filter/remap rules, writes to sinkDriver.
/// Thread-safe config updates via setRules().
class CanGateway : public QObject {
    Q_OBJECT
public:
    enum class FilterMode { PassAll, Whitelist, Blacklist };

    struct IdRule {
        uint32_t srcId{};
        uint32_t dstId{};       // remapped ID on the sink side (0 = same as srcId)
        bool     drop = false; // if true, frame is blocked even in PassAll mode
    };

    struct Stats {
        uint64_t forwarded = 0;
        uint64_t blocked   = 0;
        uint64_t remapped  = 0;
        uint64_t total     = 0;
    };

    explicit CanGateway(QObject *parent = nullptr);
    ~CanGateway() override;

    void setSource(ICanDriver *src) { m_src = src; }
    void setSink(ICanDriver *sink)  { m_sink = sink; }

    void setFilterMode(FilterMode mode);
    void setRules(const std::vector<IdRule> &rules);
    void clearRules();

    Stats stats() const;
    void  resetStats();

    bool isRunning() const { return m_running.load(); }

public slots:
    void start();
    void stop();

    /// Feed a frame directly (used when source is the main sniffer, not polled internally)
    void processFrame(const CanFrame &frame);

signals:
    void frameForwarded(const CanFrame &frame);
    void frameBlocked(const CanFrame &frame);
    void statsUpdated(quint64 forwarded, quint64 blocked, quint64 total);

private:
    void doWork();          // polling loop for standalone source driver
    bool applyRules(CanFrame &frame) const;  // returns true if frame should be forwarded

    ICanDriver        *m_src  = nullptr;
    ICanDriver        *m_sink = nullptr;
    std::atomic<bool>  m_running{false};

    mutable QMutex     m_rulesMutex;
    FilterMode         m_filterMode = FilterMode::PassAll;
    std::unordered_set<uint32_t>       m_whitelist;
    std::unordered_set<uint32_t>       m_blacklist;
    std::unordered_map<uint32_t, IdRule> m_remap;   // srcId → rule

    mutable QMutex m_statsMutex;
    Stats          m_stats;
};
