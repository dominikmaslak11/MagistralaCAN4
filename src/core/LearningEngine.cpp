#include "LearningEngine.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iomanip>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <fstream>
#include <mutex>

#include <thread>

GpuCompute LearningEngine::m_gpu;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ── Thread-safe frame ingestion ──────────────────────────────

void LearningEngine::processFrame(const CanFrame &frame) {
    std::unique_lock lock(m_mutex);
    m_frameHistory.push_back(frame);
    if (static_cast<int>(m_frameHistory.size()) > HISTORY_MAX)
        m_frameHistory.pop_front();
}

std::vector<CanFrame> LearningEngine::frameHistorySnapshot() const {
    std::shared_lock lock(m_mutex);
    return {m_frameHistory.begin(), m_frameHistory.end()};
}

// ── Event management ────────────────────────────────────────

void LearningEngine::markEvent(int64_t adaptiveBefore, int64_t adaptiveAfter) {
    std::unique_lock lock(m_mutex);
    if (m_frameHistory.empty()) return;
    if (m_frameHistory.back().timestamp == 0) return;
    uint64_t latestTs = m_frameHistory.back().timestamp;
    std::vector<CanFrame> window;
    for (const auto &f : m_frameHistory)
        if (f.timestamp >= latestTs - adaptiveBefore &&
            f.timestamp <= latestTs + adaptiveAfter)
            window.push_back(f);
    if (window.size() < 3) return;
    LeEventRecord rec;
    rec.windowFrames = window;
    rec.idFeatures = buildFeatureVectors(window);
    m_events.push_back(rec);
    m_iteration++;
    if (m_iteration == 1) recalcAdaptiveWindowLocked();
}

void LearningEngine::markNonEvent(int64_t adaptiveBefore, int64_t adaptiveAfter) {
    std::unique_lock lock(m_mutex);
    if (m_frameHistory.empty()) return;
    if (m_frameHistory.back().timestamp == 0) return;
    uint64_t latestTs = m_frameHistory.back().timestamp;
    std::vector<CanFrame> window;
    for (const auto &f : m_frameHistory)
        if (f.timestamp >= latestTs - adaptiveBefore &&
            f.timestamp <= latestTs + adaptiveAfter)
            window.push_back(f);
    if (window.size() < 3) return;
    LeEventRecord rec;
    rec.windowFrames = window;
    rec.idFeatures = buildFeatureVectors(window);
    m_nonEvents.push_back(rec);
}

void LearningEngine::resetLearning() {
    std::unique_lock lock(m_mutex);
    m_events.clear();
    m_nonEvents.clear();
    m_observations.clear();
    m_iteration = 0;
    m_linearModels.clear();
    m_transitions.clear();
    m_markovBestNext.clear();
    m_markovProb.clear();
    m_normalMean.clear();
    m_normalStd.clear();
    m_nnWeights = NnWeights{};
}

// ── Variable & observation management ──────────────────────

void LearningEngine::addVariable(const std::string &key) {
    std::unique_lock lock(m_mutex);
    if (key.empty() || m_observations.count(key)) return;
    m_observations[key] = {};
}

void LearningEngine::addObservation(const std::string &variableKey, double value,
                                     int64_t adaptiveBefore, int64_t adaptiveAfter) {
    std::unique_lock lock(m_mutex);
    if (variableKey.empty() || m_frameHistory.empty()) return;
    uint64_t latestTs = m_frameHistory.back().timestamp;
    std::vector<CanFrame> window;
    for (const auto &f : m_frameHistory)
        if (f.timestamp >= latestTs - adaptiveBefore &&
            f.timestamp <= latestTs + adaptiveAfter)
            window.push_back(f);
    if (window.empty()) return;

    std::unordered_map<uint32_t, std::vector<CanFrame>> grouped;
    for (const auto &f : window) grouped[f.id].push_back(f);

    LeValueObservation obs;
    obs.value = value;
    obs.timestamp = latestTs;
    for (auto &kv : grouped) {
        std::vector<uint8_t> avg(64, 0);
        const auto &frames = kv.second;
        float n = static_cast<float>(frames.size());
        for (int i = 0; i < 64; ++i) {
            float sum = 0;
            for (const auto &f : frames)
                if (i < f.dlc) sum += static_cast<float>(f.data[i]);
            avg[i] = static_cast<uint8_t>(sum / n);
        }
        obs.idAverageBytes[kv.first] = std::move(avg);
    }
    m_observations[variableKey].push_back(std::move(obs));
    // Ring buffer trim (#26)
    auto &vec = m_observations[variableKey];
    while (vec.size() > m_maxObservations) vec.erase(vec.begin());
    // Online Welford update (#28)
    if (m_onlineLearning) {
        for (const auto &kv : vec.back().idAverageBytes)
            for (int b = 0; b < 64; ++b)
                updateWelford(variableKey, kv.first, b,
                              value, static_cast<double>(kv.second[b]));
    }
}

const std::vector<LeValueObservation> &
LearningEngine::observations(const std::string &variableKey) const {
    std::shared_lock lock(m_mutex);
    static const std::vector<LeValueObservation> empty;
    auto it = m_observations.find(variableKey);
    return it != m_observations.end() ? it->second : empty;
}

std::vector<std::string> LearningEngine::variableNames() const {
    std::shared_lock lock(m_mutex);
    std::vector<std::string> names;
    for (const auto &kv : m_observations)
        names.push_back(kv.first);
    return names;
}

bool LearningEngine::hasVariable(const std::string &key) const {
    std::shared_lock lock(m_mutex);
    return m_observations.count(key) > 0;
}

// ── Feature extraction ──────────────────────────────────────

std::unordered_map<uint32_t, std::vector<float>>
LearningEngine::buildFeatureVectors(const std::vector<CanFrame> &window) const {
    std::unordered_map<uint32_t, std::vector<CanFrame>> grouped;
    for (const auto &f : window) grouped[f.id].push_back(f);
    std::unordered_map<uint32_t, std::vector<float>> res;
    for (auto &kv : grouped) {
        const auto &frames = kv.second;
        std::vector<float> feats(67);
        feats[0] = static_cast<float>(frames.size());

        std::vector<int64_t> deltas;
        for (size_t i = 1; i < frames.size(); ++i)
            deltas.push_back(static_cast<int64_t>(frames[i].timestamp - frames[i - 1].timestamp));
        if (deltas.empty()) {
            feats[1] = 0; feats[2] = 0;
        } else {
            double sum = std::accumulate(deltas.begin(), deltas.end(), 0.0);
            feats[1] = static_cast<float>(sum / static_cast<double>(deltas.size())) / 1000.0f;
            double sq = 0;
            for (int64_t d : deltas)
                sq += (static_cast<float>(d) - feats[1]) * (static_cast<float>(d) - feats[1]);
            feats[2] = static_cast<float>(std::sqrt(sq / static_cast<double>(deltas.size()))) / 1000.0f;
        }
        for (int b = 0; b < 64; ++b) {
            float avg = 0;
            for (const auto &f : frames)
                if (b < f.dlc) avg += static_cast<float>(f.data[b]);
            avg /= static_cast<float>(frames.size());
            feats[3 + b] = avg / 255.0f;
        }
        res[kv.first] = std::move(feats);
    }
    return res;
}

std::vector<float>
LearningEngine::buildWindowFeatures(const std::vector<CanFrame> &window) const {
    std::vector<float> feat(5, 0);
    if (window.empty()) return feat;
    feat[0] = static_cast<float>(window.size());
    std::unordered_set<uint32_t> ids;
    for (const auto &f : window) ids.insert(f.id);
    feat[1] = static_cast<float>(ids.size());

    std::unordered_map<uint32_t, int> freq;
    for (const auto &f : window) freq[f.id]++;
    double entropy = 0.0;
    for (auto &kv : freq) {
        double p = static_cast<double>(kv.second) / static_cast<double>(window.size());
        entropy -= p * std::log2(p + 1e-9);
    }
    feat[2] = static_cast<float>(entropy);
    int64_t dur = static_cast<int64_t>(window.back().timestamp - window.front().timestamp);
    feat[3] = static_cast<float>(dur) / 1000.0f;
    return feat;
}

// ── Candidate ranking ──────────────────────────────────────

std::vector<LeCandidate>
LearningEngine::computeCandidates(const std::string * /*dbcDescription*/,
                                  const std::string * /*j1939Name*/) const {
    std::shared_lock lock(m_mutex);
    std::vector<LeCandidate> cands;
    if (m_events.empty()) return cands;

    if (m_events.size() == 1) {
        for (const auto &kv : m_events[0].idFeatures)
            cands.push_back({kv.first, "Pierwsze zdarzenie", 0.0f, 1});
        return cands;
    }

    // Find common IDs across all events
    std::unordered_set<uint32_t> common;
    bool first = true;
    for (const auto &ev : m_events) {
        std::unordered_set<uint32_t> ids;
        for (const auto &kv : ev.idFeatures) ids.insert(kv.first);
        if (first) { common = ids; first = false; }
        else {
            std::unordered_set<uint32_t> inter;
            for (auto id : common) if (ids.count(id)) inter.insert(id);
            common = std::move(inter);
        }
    }

    for (uint32_t id : common) {
        std::vector<std::vector<float>> vecs;
        for (const auto &ev : m_events) {
            auto it = ev.idFeatures.find(id);
            if (it != ev.idFeatures.end()) vecs.push_back(it->second);
        }
        int N = static_cast<int>(vecs.size());
        float sim = 0;
        int pairs = 0;
        for (int i = 0; i < N; ++i) {
            for (int j = i + 1; j < N; ++j) {
                float dot = 0, nA = 0, nB = 0;
                for (size_t k = 0; k < vecs[i].size(); ++k) {
                    float a = vecs[i][k], b = vecs[j][k];
                    dot += a * b; nA += a * a; nB += b * b;
                }
                sim += dot / (std::sqrt(nA) * std::sqrt(nB) + 1e-6f);
                pairs++;
            }
        }

        std::ostringstream desc;
        desc << "ID 0x" << std::hex << std::uppercase << id;
        // DBC/J1939 enrichment would be done by the caller (AssociativeLearner)
        cands.push_back({id, desc.str(),
                         pairs > 0 ? sim / static_cast<float>(pairs) : 0.0f,
                         static_cast<int>(vecs.size())});
    }

    // Contrast with non-events
    if (!m_nonEvents.empty()) {
        for (auto &cand : cands) {
            double bgSim = 0.0;
            int bgPairs = 0;
            for (const auto &nonEv : m_nonEvents) {
                auto it = nonEv.idFeatures.find(cand.canId);
                if (it == nonEv.idFeatures.end()) continue;
                for (const auto &ev : m_events) {
                    auto itEv = ev.idFeatures.find(cand.canId);
                    if (itEv == ev.idFeatures.end()) continue;
                    float dot = 0, nA = 0, nB = 0;
                    const auto &v1 = it->second;
                    const auto &v2 = itEv->second;
                    size_t K = std::min(v1.size(), v2.size());
                for (size_t k = 0; k < K; ++k) {
                    float a = v1[k], b = v2[k];
                        dot += a * b; nA += a * a; nB += b * b;
                    }
                    bgSim += dot / (std::sqrt(nA) * std::sqrt(nB) + 1e-6f);
                    bgPairs++;
                }
            }
            if (bgPairs > 0) {
                double bgAvg = bgSim / bgPairs;
                cand.score = static_cast<float>(cand.score * (1.0 - bgAvg * 0.5));
            }
        }
    }

    std::sort(cands.begin(), cands.end(),
              [](const LeCandidate &a, const LeCandidate &b) {
                  return a.score > b.score;
              });
    return cands;
}

