#include "CanPeriodicSender.h"
#include <chrono>
#include <algorithm>

static uint64_t wallClockMs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

CanPeriodicSender::CanPeriodicSender(CanSniffer *sniffer, QObject *parent)
    : QObject(parent), m_sniffer(sniffer)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(kTickIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &CanPeriodicSender::onTick);
}

void CanPeriodicSender::addEntry(const PeriodicEntry &e) {
    m_entries.push_back(e);
    m_entries.back().lastTxMs = wallClockMs(); // arm immediately — send after first period
    m_entries.back().txCount  = 0;
}

void CanPeriodicSender::removeEntry(int index) {
    if (index < 0 || index >= static_cast<int>(m_entries.size())) return;
    m_entries.erase(m_entries.begin() + index);
}

void CanPeriodicSender::updateEntry(int index, const PeriodicEntry &e) {
    if (index < 0 || index >= static_cast<int>(m_entries.size())) return;
    uint64_t count = m_entries[index].txCount;
    m_entries[index] = e;
    m_entries[index].txCount = count;
}

void CanPeriodicSender::setEntryEnabled(int index, bool enabled) {
    if (index < 0 || index >= static_cast<int>(m_entries.size())) return;
    m_entries[index].enabled = enabled;
    if (enabled) m_entries[index].lastTxMs = wallClockMs(); // re-arm
}

void CanPeriodicSender::start() {
    if (m_running) return;
    m_running = true;
    uint64_t now = wallClockMs();
    for (auto &e : m_entries) e.lastTxMs = now;
    m_timer->start();
}

void CanPeriodicSender::stop() {
    m_running = false;
    m_timer->stop();
}

void CanPeriodicSender::clearAll() {
    m_entries.clear();
}

void CanPeriodicSender::onTick() {
    uint64_t now = wallClockMs();
    bool changed = false;
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        auto &e = m_entries[i];
        if (!e.enabled) continue;
        if (e.periodMs <= 0) continue;
        if ((now - e.lastTxMs) >= static_cast<uint64_t>(e.periodMs)) {
            if (m_sniffer) m_sniffer->writeFrame(e.frame);
            e.lastTxMs = now;
            ++e.txCount;
            changed = true;
            emit frameSent(i, e.frame);
        }
    }
    if (changed) emit statsUpdated();
}

uint64_t CanPeriodicSender::nowMs() const {
    return wallClockMs();
}
