#include "CanGateway.h"
#include <QMutexLocker>
#include <QThread>

CanGateway::CanGateway(QObject *parent) : QObject(parent) {}

CanGateway::~CanGateway() {
    stop();
}

// ── Config ────────────────────────────────────────────────────────────────────

void CanGateway::setFilterMode(FilterMode mode) {
    QMutexLocker lk(&m_rulesMutex);
    m_filterMode = mode;
}

void CanGateway::setRules(const std::vector<IdRule> &rules) {
    QMutexLocker lk(&m_rulesMutex);
    m_whitelist.clear();
    m_blacklist.clear();
    m_remap.clear();
    for (const auto &r : rules) {
        if (r.drop)
            m_blacklist.insert(r.srcId);
        else
            m_whitelist.insert(r.srcId);
        if (r.dstId != 0 && r.dstId != r.srcId)
            m_remap[r.srcId] = r;
    }
}

void CanGateway::clearRules() {
    QMutexLocker lk(&m_rulesMutex);
    m_whitelist.clear();
    m_blacklist.clear();
    m_remap.clear();
}

// ── Stats ─────────────────────────────────────────────────────────────────────

CanGateway::Stats CanGateway::stats() const {
    QMutexLocker lk(&m_statsMutex);
    return m_stats;
}

void CanGateway::resetStats() {
    QMutexLocker lk(&m_statsMutex);
    m_stats = {};
}

// ── Rule application ──────────────────────────────────────────────────────────

bool CanGateway::applyRules(CanFrame &frame) const {
    QMutexLocker lk(&m_rulesMutex);

    // Explicit block always wins
    if (m_blacklist.count(frame.id))
        return false;

    bool pass;
    switch (m_filterMode) {
        case FilterMode::Whitelist: pass = m_whitelist.count(frame.id) > 0;  break;
        case FilterMode::Blacklist: pass = m_blacklist.count(frame.id) == 0; break;
        default:                    pass = true;                               break;
    }
    if (!pass)
        return false;

    // ID remapping
    auto it = m_remap.find(frame.id);
    if (it != m_remap.end() && it->second.dstId != 0) {
        frame.id = it->second.dstId;
        return true; // remapped flag updated by caller
    }
    return true;
}

// ── Start / stop ──────────────────────────────────────────────────────────────

void CanGateway::start() {
    if (m_running.exchange(true)) return; // already running
    if (!m_src) { m_running = false; return; } // no source → use processFrame() slot

    // Run polling loop in a detached thread only when we own the source driver
    QThread::create([this]() { doWork(); })->start();
}

void CanGateway::stop() {
    m_running = false;
}

// ── Internal polling loop (when gateway owns source) ─────────────────────────

void CanGateway::doWork() {
    while (m_running.load()) {
        if (!m_src || !m_src->isValid()) {
            QThread::msleep(50);
            continue;
        }
        CanFrame f = m_src->readFrame();
        if (f.id == 0 && f.dlc == 0 && f.timestamp == 0) {
            QThread::msleep(1);
            continue;
        }
        processFrame(f);
    }
}

// ── Frame processing (also callable as slot) ──────────────────────────────────

void CanGateway::processFrame(const CanFrame &frame) {
    CanFrame f = frame; // mutable copy for remapping
    bool wasRemapped = (f.id != frame.id);

    {
        QMutexLocker lk(&m_statsMutex);
        m_stats.total++;
    }

    if (!applyRules(f)) {
        {
            QMutexLocker lk(&m_statsMutex);
            m_stats.blocked++;
        }
        emit frameBlocked(frame);
        return;
    }

    wasRemapped = (f.id != frame.id);

    if (m_sink && m_sink->isValid())
        m_sink->writeFrame(f);

    {
        QMutexLocker lk(&m_statsMutex);
        m_stats.forwarded++;
        if (wasRemapped) m_stats.remapped++;
    }

    emit frameForwarded(f);

    auto s = stats();
    emit statsUpdated(s.forwarded, s.blocked, s.total);
}
