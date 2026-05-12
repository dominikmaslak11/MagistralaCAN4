#pragma once
#include "CanFrame.h"
#include "CanSniffer.h"
#include <QObject>
#include <QTimer>
#include <vector>
#include <cstdint>
#include <memory>

/// One scheduled periodic CAN frame entry.
struct PeriodicEntry {
    CanFrame   frame;
    int        periodMs   = 100;  // transmission interval
    bool       enabled    = true;
    uint64_t   txCount    = 0;
    uint64_t   lastTxMs   = 0;   // wall-clock ms of last TX
    std::string label;
};

/// Sends a configurable list of CAN frames on independent per-entry timers.
/// Uses a single QTimer + polling to avoid spawning N timers.
class CanPeriodicSender : public QObject {
    Q_OBJECT
public:
    explicit CanPeriodicSender(CanSniffer *sniffer, QObject *parent = nullptr);

    void addEntry(const PeriodicEntry &e);
    void removeEntry(int index);
    void updateEntry(int index, const PeriodicEntry &e);
    void setEntryEnabled(int index, bool enabled);

    const std::vector<PeriodicEntry> &entries() const { return m_entries; }
    int entryCount() const { return static_cast<int>(m_entries.size()); }

    void start();
    void stop();
    bool isRunning() const { return m_running; }

    void clearAll();

signals:
    void frameSent(int entryIndex, CanFrame frame);
    void statsUpdated();

private slots:
    void onTick();

private:
    CanSniffer                *m_sniffer;
    std::vector<PeriodicEntry> m_entries;
    QTimer                    *m_timer;
    bool                       m_running = false;
    uint64_t                   m_tickMs  = 0;

    static constexpr int kTickIntervalMs = 10; // 10ms tick resolution
    uint64_t nowMs() const;
};
