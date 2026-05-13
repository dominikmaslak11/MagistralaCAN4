#include "CanModuleProfiler.h"
#include <QDateTime>
#include <QJsonArray>
#include <algorithm>

// ── ModuleProfile serialization ───────────────────────────────────────────────

QJsonObject ModuleProfile::toJson() const {
    QJsonArray periodicArr;
    for (const auto &p : periodicIds) {
        QJsonObject o;
        o["id"]            = QString("0x%1").arg(p.id, 0, 16);
        o["extended"]      = p.extended;
        o["avgIntervalMs"] = p.avgIntervalMs;
        o["jitterMs"]      = p.jitterMs;
        o["frameCount"]    = static_cast<qint64>(p.frameCount);
        periodicArr.append(o);
    }
    QJsonArray rrArr;
    for (const auto &r : reqResPairs) {
        QJsonObject o;
        o["reqId"]        = QString("0x%1").arg(r.reqId, 0, 16);
        o["respId"]       = QString("0x%1").arg(r.respId, 0, 16);
        o["avgLatencyMs"] = r.avgLatencyMs;
        o["occurrences"]  = r.occurrences;
        rrArr.append(o);
    }
    QJsonObject obj;
    obj["name"]            = name;
    obj["learnDurationMs"] = learnDurationMs;
    obj["learnedAtUs"]     = static_cast<qint64>(learnedAtUs);
    obj["periodicIds"]     = periodicArr;
    obj["reqResPairs"]     = rrArr;
    return obj;
}

ModuleProfile ModuleProfile::fromJson(const QJsonObject &obj) {
    ModuleProfile p;
    p.name            = obj["name"].toString();
    p.learnDurationMs = obj["learnDurationMs"].toInt();
    p.learnedAtUs     = static_cast<uint64_t>(obj["learnedAtUs"].toInteger());

    for (const auto &v : obj["periodicIds"].toArray()) {
        QJsonObject o = v.toObject();
        ModulePeriodicId pid;
        pid.id            = o["id"].toString().toUInt(nullptr, 16);
        pid.extended      = o["extended"].toBool();
        pid.avgIntervalMs = o["avgIntervalMs"].toDouble();
        pid.jitterMs      = o["jitterMs"].toDouble();
        pid.frameCount    = static_cast<uint64_t>(o["frameCount"].toInteger());
        p.periodicIds.append(pid);
    }
    for (const auto &v : obj["reqResPairs"].toArray()) {
        QJsonObject o = v.toObject();
        ModuleReqResPair rr;
        rr.reqId        = o["reqId"].toString().toUInt(nullptr, 16);
        rr.respId       = o["respId"].toString().toUInt(nullptr, 16);
        rr.avgLatencyMs = o["avgLatencyMs"].toDouble();
        rr.occurrences  = o["occurrences"].toInt();
        p.reqResPairs.append(rr);
    }
    return p;
}

// ── CanModuleProfiler ─────────────────────────────────────────────────────────

CanModuleProfiler::CanModuleProfiler(QObject *parent) : QObject(parent) {}

void CanModuleProfiler::startLearning(const QString &name, int durationMs) {
    cancelLearning();
    stopDetecting();

    m_state              = Learning;
    m_learningName       = name;
    m_learningDurationMs = durationMs;
    m_learningStartMs    = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
    m_learnData.clear();
    m_recentFrames.clear();
    m_rrCandidates.clear();

    delete m_learningTimer;
    m_learningTimer = new QTimer(this);
    m_learningTimer->setSingleShot(true);
    connect(m_learningTimer, &QTimer::timeout, this, &CanModuleProfiler::onLearningTimeout);
    m_learningTimer->start(durationMs);

    delete m_progressTimer;
    m_progressTimer = new QTimer(this);
    connect(m_progressTimer, &QTimer::timeout, this, [this]() {
        uint64_t nowMs = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch());
        int elapsed    = static_cast<int>(nowMs - m_learningStartMs);
        int pct        = std::min(100, elapsed * 100 / m_learningDurationMs);
        emit learningProgress(pct);
    });
    m_progressTimer->start(200);
}

void CanModuleProfiler::cancelLearning() {
    if (m_state != Learning) return;
    delete m_learningTimer; m_learningTimer = nullptr;
    delete m_progressTimer; m_progressTimer = nullptr;
    m_state = Idle;
    m_learnData.clear();
    m_recentFrames.clear();
    m_rrCandidates.clear();
}

void CanModuleProfiler::finalizeLearning() {
    onLearningTimeout();
}

void CanModuleProfiler::onLearningTimeout() {
    delete m_learningTimer; m_learningTimer = nullptr;
    delete m_progressTimer; m_progressTimer = nullptr;
    emit learningProgress(100);
    finalizeProfile();
    m_state = Idle;
}

void CanModuleProfiler::finalizeProfile() {
    ModuleProfile profile;
    profile.name            = m_learningName;
    profile.learnDurationMs = m_learningDurationMs;
    profile.learnedAtUs     = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch()) * 1000;

    for (const auto &[id, wd] : m_learnData) {
        if (wd.frames < static_cast<uint64_t>(kMinPeriodicFrames)) continue;
        if (wd.n < kMinPeriodicFrames - 1)                         continue;
        if (wd.mean < 0.5)                                         continue;

        double jitter = wd.stddev();
        if (jitter > kPeriodicJitterRatio * wd.mean)               continue;

        ModulePeriodicId pid;
        pid.id            = id;
        pid.extended      = wd.extended;
        pid.avgIntervalMs = wd.mean;
        pid.jitterMs      = jitter;
        pid.frameCount    = wd.frames;
        profile.periodicIds.append(pid);
    }

    std::sort(profile.periodicIds.begin(), profile.periodicIds.end(),
              [](const ModulePeriodicId &a, const ModulePeriodicId &b) {
                  return a.id < b.id;
              });

    for (const auto &[key, cand] : m_rrCandidates) {
        if (cand.count >= kMinReqRespOccurrences) {
            ModuleReqResPair rr;
            rr.reqId        = cand.reqId;
            rr.respId       = cand.respId;
            rr.avgLatencyMs = cand.latSum / cand.count;
            rr.occurrences  = cand.count;
            profile.reqResPairs.append(rr);
        }
    }

    m_lastLearned = profile;
    m_learnData.clear();
    m_recentFrames.clear();
    m_rrCandidates.clear();
    emit learningFinished(profile);
}

void CanModuleProfiler::startDetecting(const ModuleProfile &profile, uint64_t initNowUs) {
    stopDetecting();
    m_detectProfile = profile;
    m_lastSeenUs.clear();
    m_currentlyMissing.clear();

    if (initNowUs == std::numeric_limits<uint64_t>::max())
        initNowUs = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch()) * 1000;
    for (const auto &pid : profile.periodicIds)
        m_lastSeenUs[pid.id] = initNowUs;

    m_state = Detecting;

    if (!m_detectionTimer) {
        m_detectionTimer = new QTimer(this);
        connect(m_detectionTimer, &QTimer::timeout, this, &CanModuleProfiler::onDetectionTick);
    }
    m_detectionTimer->start(kDetectionTickMs);
}

void CanModuleProfiler::stopDetecting() {
    if (m_detectionTimer) m_detectionTimer->stop();
    if (m_state == Detecting) m_state = Idle;
    m_currentlyMissing.clear();
}

void CanModuleProfiler::feed(const CanFrame &frame, uint64_t tsUs) {
    if (m_state == Detecting) {
        m_lastSeenUs[frame.id] = tsUs;
        return;
    }
    if (m_state != Learning) return;

    auto &wd = m_learnData[frame.id];
    wd.frames++;
    wd.extended = frame.extended;

    if (wd.lastTs > 0 && tsUs > wd.lastTs) {
        double interval = static_cast<double>(tsUs - wd.lastTs) / 1000.0;  // µs → ms
        wd.n++;
        double delta  = interval - wd.mean;
        wd.mean      += delta / wd.n;
        double delta2  = interval - wd.mean;
        wd.M2         += delta * delta2;
    }
    wd.lastTs = tsUs;

    // Prune recent-frame window
    while (!m_recentFrames.empty() &&
           static_cast<int64_t>(tsUs - m_recentFrames.front().tsUs) > kReqRespWindowUs)
        m_recentFrames.pop_front();

    // Req/resp: record pairs where another ID preceded this one by 10–150ms
    for (const auto &recent : m_recentFrames) {
        if (recent.id == frame.id) continue;
        int64_t latUs = static_cast<int64_t>(tsUs - recent.tsUs);
        if (latUs >= 10'000) {
            uint64_t key   = (uint64_t(recent.id) << 32) | frame.id;
            auto &cand     = m_rrCandidates[key];
            cand.reqId     = recent.id;
            cand.respId    = frame.id;
            cand.count++;
            cand.latSum   += static_cast<double>(latUs) / 1000.0;
        }
    }

    m_recentFrames.push_back({frame.id, tsUs});
    if (m_recentFrames.size() > 50)
        m_recentFrames.pop_front();
}

void CanModuleProfiler::onDetectionTick() {
    uint64_t nowUs = static_cast<uint64_t>(QDateTime::currentMSecsSinceEpoch()) * 1000;
    checkDetection(nowUs);
}

void CanModuleProfiler::checkDetection(uint64_t nowUs) {
    if (m_state != Detecting) return;

    bool hadMissing = !m_currentlyMissing.empty();

    for (const auto &pid : m_detectProfile.periodicIds) {
        auto it       = m_lastSeenUs.find(pid.id);
        uint64_t last = (it != m_lastSeenUs.end()) ? it->second : 0;

        double windowMs    = pid.avgIntervalMs + std::max(3.0 * pid.jitterMs, 50.0);
        uint64_t deadlineUs = last + static_cast<uint64_t>(windowMs * 1000.0);

        if (nowUs > deadlineUs) {
            if (m_currentlyMissing.insert(pid.id).second)
                emit moduleOffline(m_detectProfile.name, pid.id);
        } else {
            m_currentlyMissing.erase(pid.id);
        }
    }

    if (hadMissing && m_currentlyMissing.empty())
        emit moduleOnline(m_detectProfile.name);
}
