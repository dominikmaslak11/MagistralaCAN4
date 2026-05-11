#include "LearningEngine.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <mutex>
#include <thread>

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
    if (m_iteration == 1) recalcAdaptiveWindow();
}

void LearningEngine::markNonEvent(int64_t adaptiveBefore, int64_t adaptiveAfter) {
    std::unique_lock lock(m_mutex);
    if (m_frameHistory.empty()) return;
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
    for (auto &kv : grouped) {
        std::vector<uint8_t> avg(64, 0);
        const auto &frames = kv.second;
        for (const auto &f : frames)
            for (int i = 0; i < f.dlc; ++i)
                avg[i] += static_cast<uint8_t>(f.data[i] / frames.size());
        obs.idAverageBytes[kv.first] = std::move(avg);
    }
    m_observations[variableKey].push_back(std::move(obs));
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
            deltas.push_back(frames[i].timestamp - frames[i - 1].timestamp);
        if (deltas.empty()) {
            feats[1] = 0; feats[2] = 0;
        } else {
            double sum = std::accumulate(deltas.begin(), deltas.end(), 0.0);
            feats[1] = static_cast<float>(sum / deltas.size()) / 1000.0f;
            double sq = 0;
            for (int64_t d : deltas)
                sq += (d - feats[1]) * (d - feats[1]);
            feats[2] = static_cast<float>(std::sqrt(sq / deltas.size())) / 1000.0f;
        }
        for (int b = 0; b < 64; ++b) {
            float avg = 0;
            for (const auto &f : frames)
                if (b < f.dlc) avg += f.data[b];
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
        double p = static_cast<double>(kv.second) / window.size();
        entropy -= p * std::log2(p + 1e-9);
    }
    feat[2] = static_cast<float>(entropy);
    int64_t dur = window.back().timestamp - window.front().timestamp;
    feat[3] = static_cast<float>(dur) / 1000.0f;
    return feat;
}

// ── Candidate ranking ──────────────────────────────────────

std::vector<LeCandidate>
LearningEngine::computeCandidates(const std::string *dbcDescription,
                                  const std::string *j1939Name) const {
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
                         pairs > 0 ? sim / pairs : 0.0f,
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
                    for (size_t k = 0; k < v1.size(); ++k) {
                        float a = v1[k], b = v2[k];
                        dot += a * b; nA += a * a; nB += b * b;
                    }
                    bgSim += dot / (std::sqrt(nA) * std::sqrt(nB) + 1e-6f);
                    bgPairs++;
                }
            }
            if (bgPairs > 0) {
                double bgAvg = bgSim / bgPairs;
                cand.score = cand.score * (1.0 - bgAvg * 0.5);
            }
        }
    }

    std::sort(cands.begin(), cands.end(),
              [](const LeCandidate &a, const LeCandidate &b) {
                  return a.score > b.score;
              });
    return cands;
}

// ── Correlation table (Pearson) ────────────────────────────

std::vector<LeCorrelationEntry>
LearningEngine::computeCorrelations(const std::string &variableKey) const {
    std::shared_lock lock(m_mutex);
    std::vector<LeCorrelationEntry> entries;
    const auto &obs = observations(variableKey);
    if (obs.size() < 3) return entries;

    // Find common IDs
    std::unordered_set<uint32_t> common;
    bool first = true;
    for (const auto &o : obs) {
        std::unordered_set<uint32_t> ids;
        for (const auto &kv : o.idAverageBytes) ids.insert(kv.first);
        if (first) { common = ids; first = false; }
        else {
            std::unordered_set<uint32_t> inter;
            for (auto id : common) if (ids.count(id)) inter.insert(id);
            common = std::move(inter);
        }
    }

    for (uint32_t id : common) {
        for (int b = 0; b < 64; ++b) {
            std::vector<double> vx, vy;
            for (const auto &o : obs) {
                auto it = o.idAverageBytes.find(id);
                if (it != o.idAverageBytes.end()) {
                    vx.push_back(o.value);
                    vy.push_back(static_cast<double>(it->second[b]));
                }
            }
            int N = static_cast<int>(vx.size());
            if (N < 3) continue;
            double corr = correlationPearson(vx, vy);
            double pv = pearsonPValue(corr, N);
            entries.push_back({id, b, corr, pv, pv < 0.05});
        }
    }

    std::sort(entries.begin(), entries.end(),
              [](const LeCorrelationEntry &a, const LeCorrelationEntry &b) {
                  return std::abs(a.correlation) > std::abs(b.correlation);
              });
    return entries;
}

// ── Sequence analysis ──────────────────────────────────────

std::vector<LeSequenceEntry>
LearningEngine::computeSequences(int ngramLength) const {
    std::shared_lock lock(m_mutex);
    std::vector<LeSequenceEntry> result;
    if (m_events.empty()) return result;

    std::unordered_map<std::string, int> seqCount;
    for (const auto &ev : m_events) {
        if (ev.windowFrames.size() < static_cast<size_t>(ngramLength)) continue;
        for (size_t i = 0; i <= ev.windowFrames.size() - ngramLength; ++i) {
            std::ostringstream oss;
            for (int j = 0; j < ngramLength; ++j) {
                if (j > 0) oss << "→";
                oss << "0x" << std::hex << ev.windowFrames[i + j].id;
            }
            seqCount[oss.str()]++;
        }
    }

    for (const auto &kv : seqCount) {
        result.push_back({kv.first, kv.second,
                          static_cast<float>(kv.second) / m_events.size()});
    }
    std::sort(result.begin(), result.end(),
              [](const LeSequenceEntry &a, const LeSequenceEntry &b) {
                  return a.occurrences > b.occurrences;
              });
    return result;
}

// ── Cross-byte correlation ─────────────────────────────────

std::vector<LeCrossByteEntry>
LearningEngine::computeCrossByte(const std::string &variableKey) const {
    std::shared_lock lock(m_mutex);
    std::vector<LeCrossByteEntry> entries;
    const auto &obs = observations(variableKey);
    if (obs.size() < 3) return entries;

    std::unordered_set<uint32_t> common;
    bool first = true;
    for (const auto &o : obs) {
        std::unordered_set<uint32_t> ids;
        for (const auto &kv : o.idAverageBytes) ids.insert(kv.first);
        if (first) { common = ids; first = false; }
        else {
            std::unordered_set<uint32_t> inter;
            for (auto id : common) if (ids.count(id)) inter.insert(id);
            common = std::move(inter);
        }
    }

    std::vector<uint32_t> idList(common.begin(), common.end());
    for (size_t i = 0; i < idList.size(); ++i) {
        for (size_t j = 0; j < idList.size(); ++j) {
            for (int b1 = 0; b1 < 8; ++b1) {
                for (int b2 = 0; b2 < 8; ++b2) {
                    std::vector<double> vb1, vb2;
                    for (const auto &o : obs) {
                        auto it1 = o.idAverageBytes.find(idList[i]);
                        auto it2 = o.idAverageBytes.find(idList[j]);
                        if (it1 != o.idAverageBytes.end() &&
                            it2 != o.idAverageBytes.end()) {
                            vb1.push_back(static_cast<double>(it1->second[b1]));
                            vb2.push_back(static_cast<double>(it2->second[b2]));
                        }
                    }
                    if (vb1.size() < 3) continue;
                    double corr = correlationPearson(vb1, vb2);
                    entries.push_back({idList[i], b1, idList[j], b2, corr});
                }
            }
        }
    }

    std::sort(entries.begin(), entries.end(),
              [](const LeCrossByteEntry &a, const LeCrossByteEntry &b) {
                  return std::abs(a.correlation) > std::abs(b.correlation);
              });
    return entries;
}

// ── Clustering (k-means — sequential, no QtConcurrent) ──────

int LearningEngine::kMeans(const std::vector<std::vector<float>> &data,
                            int K, std::vector<int> &assignments) {
    int N = static_cast<int>(data.size());
    if (N == 0) return 0;
    int dim = static_cast<int>(data[0].size());
    assignments.resize(N, 0);

    std::vector<std::vector<float>> centroids(K, std::vector<float>(dim));
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, N - 1);
    for (int k = 0; k < K; ++k)
        centroids[k] = data[dist(rng)];

    int iter = 0, maxIter = 100;
    while (iter++ < maxIter) {
        // Assignment step
        std::vector<int> counts(K, 0);
        std::vector<std::vector<float>> newCtr(K, std::vector<float>(dim, 0.0f));

        for (int i = 0; i < N; ++i) {
            int bestIdx = 0;
            double bestDist = std::numeric_limits<double>::max();
            for (int k = 0; k < K; ++k) {
                double d2 = 0.0;
                for (int d = 0; d < dim; ++d) {
                    double diff = data[i][d] - centroids[k][d];
                    d2 += diff * diff;
                }
                if (d2 < bestDist) { bestDist = d2; bestIdx = k; }
            }
            assignments[i] = bestIdx;
            counts[bestIdx]++;
            for (int d = 0; d < dim; ++d)
                newCtr[bestIdx][d] += data[i][d];
        }

        // Update step
        bool changed = false;
        for (int k = 0; k < K; ++k) {
            if (counts[k] > 0)
                for (int d = 0; d < dim; ++d)
                    newCtr[k][d] /= static_cast<float>(counts[k]);
            double diff = 0.0;
            for (int d = 0; d < dim; ++d)
                diff += (centroids[k][d] - newCtr[k][d]) *
                        (centroids[k][d] - newCtr[k][d]);
            if (diff > 0.001) changed = true;
            centroids[k] = newCtr[k];
        }
        if (!changed) break;
    }
    return K;
}

// ── DBSCAN ──────────────────────────────────────────────────

int LearningEngine::dbscan(const std::vector<std::vector<float>> &data,
                            float eps, int minPts,
                            std::vector<int> &assignments) {
    int N = static_cast<int>(data.size());
    if (N == 0) return 0;
    int dim = static_cast<int>(data[0].size());
    assignments.resize(N, -1);

    // Build distance matrix
    std::vector<std::vector<float>> dist(N, std::vector<float>(N, 0.0f));
    for (int i = 0; i < N; ++i)
        for (int j = i + 1; j < N; ++j) {
            float d2 = 0.0f;
            for (int k = 0; k < dim; ++k) {
                float diff = data[i][k] - data[j][k];
                d2 += diff * diff;
            }
            dist[i][j] = d2;
            dist[j][i] = d2;
        }

    float eps2 = eps * eps;
    int clusterId = 0;

    for (int p = 0; p < N; ++p) {
        if (assignments[p] != -1) continue;

        std::vector<int> neighbors;
        for (int q = 0; q < N; ++q)
            if (dist[p][q] <= eps2)
                neighbors.push_back(q);

        if (static_cast<int>(neighbors.size()) < minPts) continue;

        assignments[p] = clusterId;
        for (size_t ni = 0; ni < neighbors.size(); ++ni) {
            int q = neighbors[ni];
            if (assignments[q] != -1) continue;
            assignments[q] = clusterId;

            std::vector<int> qn;
            for (int r = 0; r < N; ++r)
                if (dist[q][r] <= eps2) qn.push_back(r);
            if (static_cast<int>(qn.size()) >= minPts)
                for (int r : qn)
                    if (std::find(neighbors.begin(), neighbors.end(), r) ==
                        neighbors.end())
                        neighbors.push_back(r);
        }
        clusterId++;
    }
    return clusterId;
}

// ── Window extraction helper ────────────────────────────────

std::vector<CanFrame> LearningEngine::extractWindow() const {
    std::shared_lock lock(m_mutex);
    if (m_frameHistory.empty()) return {};
    uint64_t latestTs = m_frameHistory.back().timestamp;
    std::vector<CanFrame> window;
    for (const auto &f : m_frameHistory)
        if (f.timestamp >= latestTs - m_adaptiveBefore &&
            f.timestamp <= latestTs + m_adaptiveAfter)
            window.push_back(f);
    return window;
}

// ── Cluster windows (compute windows from history) ──────────

static std::vector<std::vector<CanFrame>>
splitWindows(const std::deque<CanFrame> &history, int64_t winSize) {
    std::vector<std::vector<CanFrame>> windows;
    if (history.empty()) return windows;
    int64_t start = history.front().timestamp;
    int64_t end = history.back().timestamp;
    for (int64_t t = start; t < end; t += winSize / 2) {
        std::vector<CanFrame> win;
        for (const auto &f : history)
            if (f.timestamp >= t && f.timestamp < t + winSize)
                win.push_back(f);
        if (win.size() >= 3) windows.push_back(std::move(win));
    }
    return windows;
}

std::vector<LeClusterStats>
LearningEngine::clusterWindows(int K) const {
    std::shared_lock lock(m_mutex);
    std::vector<LeClusterStats> result;
    auto windows = splitWindows(m_frameHistory, 500000);
    if (windows.size() < 5) return result;

    std::vector<std::vector<float>> features;
    for (const auto &w : windows)
        features.push_back(buildWindowFeatures(w));

    std::vector<int> assignments;
    kMeans(features, K, assignments);

    struct Stats { int cnt = 0; double avg = 0;
        std::unordered_map<uint32_t, int> freq; };
    std::vector<Stats> stats(K);
    for (size_t i = 0; i < assignments.size(); ++i) {
        int c = assignments[i];
        stats[c].cnt++;
        stats[c].avg += windows[i].size();
        for (auto &f : windows[i]) stats[c].freq[f.id]++;
    }
    for (int c = 0; c < K; ++c)
        if (stats[c].cnt) stats[c].avg /= stats[c].cnt;

    for (int c = 0; c < K; ++c) {
        std::vector<std::pair<uint32_t, int>> srt;
        for (auto &kv : stats[c].freq)
            srt.push_back({kv.first, kv.second});
        std::sort(srt.begin(), srt.end(),
                  [](auto &a, auto &b) { return a.second > b.second; });
        std::vector<uint32_t> top;
        for (int i = 0; i < 3 && i < static_cast<int>(srt.size()); ++i)
            top.push_back(srt[i].first);
        result.push_back({c + 1, stats[c].avg, top, stats[c].cnt});
    }
    return result;
}

std::vector<LeClusterStats>
LearningEngine::dbscanClustering(float eps, int minPts) const {
    std::shared_lock lock(m_mutex);
    std::vector<LeClusterStats> result;
    auto windows = splitWindows(m_frameHistory, 500000);
    if (windows.size() < 5) return result;

    std::vector<std::vector<float>> features;
    for (const auto &w : windows)
        features.push_back(buildWindowFeatures(w));

    std::vector<int> assignments;
    int K = dbscan(features, eps, minPts, assignments);

    struct Stats { int cnt = 0; double avg = 0;
        std::unordered_map<uint32_t, int> freq; };
    std::vector<Stats> stats(std::max(1, K));
    int noiseCnt = 0;
    for (size_t i = 0; i < assignments.size(); ++i) {
        int c = assignments[i];
        if (c < 0) { noiseCnt++; continue; }
        stats[c].cnt++;
        stats[c].avg += windows[i].size();
        for (auto &f : windows[i]) stats[c].freq[f.id]++;
    }
    for (int c = 0; c < static_cast<int>(stats.size()); ++c)
        if (stats[c].cnt) stats[c].avg /= stats[c].cnt;

    for (int c = 0; c < K; ++c) {
        std::vector<std::pair<uint32_t, int>> srt;
        for (auto &kv : stats[c].freq)
            srt.push_back({kv.first, kv.second});
        std::sort(srt.begin(), srt.end(),
                  [](auto &a, auto &b) { return a.second > b.second; });
        std::vector<uint32_t> top;
        for (int i = 0; i < 3 && i < static_cast<int>(srt.size()); ++i)
            top.push_back(srt[i].first);
        result.push_back({c + 1, stats[c].avg, top, stats[c].cnt});
    }
    if (noiseCnt > 0)
        result.push_back({-1, 0.0, {}, noiseCnt});
    return result;
}

std::vector<LeWcssPoint>
LearningEngine::autoKMeans(int maxK) const {
    std::shared_lock lock(m_mutex);
    std::vector<LeWcssPoint> wcssHistory;
    auto windows = splitWindows(m_frameHistory, 500000);
    if (windows.size() < 5) return wcssHistory;

    std::vector<std::vector<float>> features;
    for (const auto &w : windows)
        features.push_back(buildWindowFeatures(w));

    int N = static_cast<int>(features.size());
    int dim = static_cast<int>(features[0].size());

    for (int k = 1; k <= maxK && k < N; ++k) {
        std::vector<int> assignments;
        kMeans(features, k, assignments);

        // Compute true WCSS
        std::vector<std::vector<double>> cents(k, std::vector<double>(dim, 0.0));
        std::vector<int> cnts(k, 0);
        for (int i = 0; i < N; ++i) {
            int c = assignments[i];
            cnts[c]++;
            for (int d = 0; d < dim; ++d)
                cents[c][d] += features[i][d];
        }
        for (int c = 0; c < k; ++c)
            if (cnts[c] > 0)
                for (int d = 0; d < dim; ++d)
                    cents[c][d] /= cnts[c];

        double wcss = 0.0;
        for (int i = 0; i < N; ++i) {
            int c = assignments[i];
            double d2 = 0.0;
            for (int d = 0; d < dim; ++d) {
                double diff = features[i][d] - cents[c][d];
                d2 += diff * diff;
            }
            wcss += d2;
        }
        wcssHistory.push_back({k, wcss});
    }
    return wcssHistory;
}

// ── PCA clustering ──────────────────────────────────────────

LearningEngine::PcaResult LearningEngine::runPcaClustering() const {
    PcaResult result;
    std::shared_lock lock(m_mutex);
    auto windows = splitWindows(m_frameHistory, 500000);
    if (windows.size() < 5) return result;

    std::vector<std::vector<float>> features;
    for (const auto &w : windows)
        features.push_back(buildWindowFeatures(w));
    int N = static_cast<int>(features.size());
    int dim = static_cast<int>(features[0].size());

    // 1. Compute mean
    std::vector<float> mean(dim, 0.0f);
    for (const auto &f : features)
        for (int d = 0; d < dim; ++d)
            mean[d] += f[d];
    for (int d = 0; d < dim; ++d) mean[d] /= N;

    // 2. Center
    std::vector<std::vector<float>> centered(N, std::vector<float>(dim));
    for (int i = 0; i < N; ++i)
        for (int d = 0; d < dim; ++d)
            centered[i][d] = features[i][d] - mean[d];

    // 3. Build correlation matrix (dim × dim)
    std::vector<std::vector<double>> cov(dim, std::vector<double>(dim, 0.0));
    for (int i = 0; i < dim; ++i) {
        for (int j = 0; j < dim; ++j) {
            double sum = 0.0;
            for (int k = 0; k < N; ++k)
                sum += centered[k][i] * centered[k][j];
            cov[i][j] = sum / (N - 1);
        }
    }

    double origTrace = 0.0;
    for (int d = 0; d < dim; ++d) origTrace += cov[d][d];

    // 4. Power iteration for first two eigenvectors
    auto powerIteration = [&](std::vector<double> initVec, int maxIter = 100)
        -> std::pair<double, std::vector<double>> {
        std::vector<double> vec = initVec;
        double eigenvalue = 0.0;
        for (int iter = 0; iter < maxIter; ++iter) {
            std::vector<double> newVec(dim, 0.0);
            for (int i = 0; i < dim; ++i)
                for (int j = 0; j < dim; ++j)
                    newVec[i] += cov[i][j] * vec[j];
            double norm = 0.0;
            for (int i = 0; i < dim; ++i) norm += newVec[i] * newVec[i];
            norm = std::sqrt(norm);
            if (norm < 1e-12) break;
            for (int i = 0; i < dim; ++i) newVec[i] /= norm;
            eigenvalue = 0.0;
            for (int i = 0; i < dim; ++i) eigenvalue += vec[i] * newVec[i];
            vec = newVec;
        }
        return {eigenvalue, vec};
    };

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> urd(0.0, 1.0);
    std::vector<double> initVec(dim);
    for (int i = 0; i < dim; ++i) initVec[i] = urd(rng);

    auto [eig1, pc1] = powerIteration(initVec);
    // Deflate
    for (int i = 0; i < dim; ++i)
        for (int j = 0; j < dim; ++j)
            cov[i][j] -= eig1 * pc1[i] * pc1[j];
    auto [eig2, pc2] = powerIteration(initVec);

    result.pc1 = pc1;
    result.pc2 = pc2;
    result.eig1 = eig1;
    result.eig2 = eig2;
    result.varianceExplained = (eig1 + eig2) / origTrace;

    // 5. Project to 2D
    for (int i = 0; i < N; ++i) {
        double x = 0.0, y = 0.0;
        for (int d = 0; d < dim; ++d) {
            x += centered[i][d] * pc1[d];
            y += centered[i][d] * pc2[d];
        }
        result.projected.push_back({x, y});
    }

    // 6. k-means on 2D (K=3)
    std::vector<std::vector<float>> data2D(N, std::vector<float>(2));
    for (int i = 0; i < N; ++i) {
        data2D[i][0] = static_cast<float>(result.projected[i].first);
        data2D[i][1] = static_cast<float>(result.projected[i].second);
    }
    kMeans(data2D, 3, result.clusterAssignments);

    return result;
}

// ── Prediction (linear regression) ──────────────────────────

std::vector<LePredictionModel>
LearningEngine::trainPrediction(const std::string &variableKey) {
    std::unique_lock lock(m_mutex);
    std::vector<LePredictionModel> models;
    const auto &obs = observations(variableKey);
    if (obs.size() < 3) return models;

    std::unordered_set<uint64_t> allPairs;
    for (const auto &o : obs)
        for (const auto &kv : o.idAverageBytes)
            for (int b = 0; b < 64; ++b)
                allPairs.insert(static_cast<uint64_t>(kv.first) * 100 + b);

    auto &modelsForVar = m_linearModels[variableKey];

    for (uint64_t key : allPairs) {
        uint32_t id = static_cast<uint32_t>(key / 100);
        int byteIdx = static_cast<int>(key % 100);
        std::vector<double> X, Y;
        for (const auto &o : obs) {
            auto it = o.idAverageBytes.find(id);
            if (it != o.idAverageBytes.end()) {
                X.push_back(static_cast<double>(it->second[byteIdx]));
                Y.push_back(o.value);
            }
        }
        int N = static_cast<int>(X.size());
        if (N < 3) continue;

        double sx = 0, sy = 0, sxy = 0, sx2 = 0;
        for (int i = 0; i < N; ++i) {
            double x = X[i], y = Y[i];
            sx += x; sy += y; sxy += x * y; sx2 += x * x;
        }
        double denom = N * sx2 - sx * sx;
        if (std::abs(denom) < 1e-9) continue;
        double a = (N * sxy - sx * sy) / denom;
        double b = (sy - a * sx) / N;

        double sy2 = 0;
        for (double y : Y) sy2 += y * y;
        double corr = (N * sxy - sx * sy) /
                      std::sqrt((N * sx2 - sx * sx) * (N * sy2 - sy * sy) + 1e-9);
        if (std::abs(corr) < 0.8) continue;

        modelsForVar[id * 100 + byteIdx] = {a, b};
        models.push_back({id, byteIdx, a, b});
    }
    return models;
}

std::vector<LeRealtimePrediction>
LearningEngine::predictRealtime(const std::string &variableKey) const {
    std::shared_lock lock(m_mutex);
    std::vector<LeRealtimePrediction> preds;
    auto itVar = m_linearModels.find(variableKey);
    if (itVar == m_linearModels.end() || m_frameHistory.empty())
        return preds;

    for (const auto &kv : itVar->second) {
        uint32_t id = kv.first / 100;
        int byteIdx = kv.first % 100;
        double a = kv.second.first;
        double b = kv.second.second;

        CanFrame lastFrame;
        bool found = false;
        for (auto ri = m_frameHistory.rbegin(); ri != m_frameHistory.rend(); ++ri) {
            if (ri->id == id) { lastFrame = *ri; found = true; break; }
        }
        if (found && byteIdx < lastFrame.dlc) {
            double pred = a * lastFrame.data[byteIdx] + b;
            preds.push_back({id, byteIdx, pred});
        }
    }
    return preds;
}

// ── Anomaly detection ───────────────────────────────────────

void LearningEngine::buildNormalModel() {
    std::unique_lock lock(m_mutex);
    int64_t winSize = 1000000;
    int64_t start = m_frameHistory.front().timestamp;
    int64_t end = m_frameHistory.back().timestamp;
    std::vector<std::vector<float>> feats;
    for (int64_t t = start; t < end - winSize; t += 500000) {
        std::vector<CanFrame> win;
        for (const auto &f : m_frameHistory)
            if (f.timestamp >= t && f.timestamp < t + winSize)
                win.push_back(f);
        if (win.size() >= 3)
            feats.push_back(buildWindowFeatures(win));
    }
    if (feats.size() < 10) return;
    int dim = static_cast<int>(feats[0].size());
    m_normalMean.resize(dim, 0);
    m_normalStd.resize(dim, 0);
    for (int d = 0; d < dim; ++d) {
        double sum = 0, sq = 0;
        for (auto &f : feats) { sum += f[d]; sq += f[d] * f[d]; }
        m_normalMean[d] = static_cast<float>(sum / feats.size());
        m_normalStd[d] = static_cast<float>(
            std::sqrt(sq / feats.size() - m_normalMean[d] * m_normalMean[d]));
        if (m_normalStd[d] < 1e-6f) m_normalStd[d] = 1.0f;
    }
}

double LearningEngine::checkAnomaly(double threshold) const {
    std::shared_lock lock(m_mutex);
    if (m_frameHistory.empty() || m_normalMean.empty()) return 0.0;
    uint64_t now = m_frameHistory.back().timestamp;
    std::vector<CanFrame> win;
    for (auto ri = m_frameHistory.rbegin(); ri != m_frameHistory.rend(); ++ri) {
        if (ri->timestamp >= now - 1000000)
            win.push_back(*ri);
        else break;
    }
    if (win.size() < 3) return 0.0;
    std::reverse(win.begin(), win.end());  // restore chronological order
    std::vector<float> feat = buildWindowFeatures(win);
    double score = 0.0;
    for (size_t d = 0; d < feat.size() && d < m_normalMean.size(); ++d) {
        double z = (feat[d] - m_normalMean[d]) / m_normalStd[d];
        score += z * z;
    }
    return score;
}

// ── Markov chain ────────────────────────────────────────────

void LearningEngine::trainMarkovModel() {
    std::unique_lock lock(m_mutex);
    m_transitions.clear();
    if (m_frameHistory.size() < 2) return;

    auto it = m_frameHistory.begin();
    auto next = it; ++next;
    for (; next != m_frameHistory.end(); ++it, ++next) {
        m_transitions[it->id][next->id]++;
    }

    for (auto &kv : m_transitions) {
        uint32_t fromId = kv.first;
        int total = 0;
        for (auto &t : kv.second) total += t.second;
        if (total == 0) continue;

        uint32_t bestId = 0;
        int bestCount = 0;
        for (auto &t : kv.second)
            if (t.second > bestCount) {
                bestCount = t.second;
                bestId = t.first;
            }
        m_markovBestNext[fromId] = bestId;
        m_markovProb[fromId] = static_cast<double>(bestCount) / total;
    }
}

std::vector<LeMarkovEntry>
LearningEngine::predictNextFrames() const {
    std::shared_lock lock(m_mutex);
    std::vector<LeMarkovEntry> entries;
    if (m_transitions.empty() || m_frameHistory.empty()) return entries;

    // Get last 5 unique IDs
    std::vector<uint32_t> recentIds;
    for (auto it = m_frameHistory.rbegin(); it != m_frameHistory.rend(); ++it) {
        if (std::find(recentIds.begin(), recentIds.end(), it->id) ==
            recentIds.end()) {
            recentIds.push_back(it->id);
            if (recentIds.size() >= 5) break;
        }
    }

    for (uint32_t id : recentIds) {
        auto itBest = m_markovBestNext.find(id);
        auto itProb = m_markovProb.find(id);
        entries.push_back({id,
                           itBest != m_markovBestNext.end() ? itBest->second : 0,
                           itProb != m_markovProb.end() ? itProb->second : 0.0});
    }
    return entries;
}

// ── Cross-variable correlation matrix ───────────────────────

std::vector<std::vector<double>>
LearningEngine::computeCrossVariableMatrix() const {
    std::shared_lock lock(m_mutex);
    std::vector<std::string> names = variableNames();
    int N = static_cast<int>(names.size());
    std::vector<std::vector<double>> matrix(N, std::vector<double>(N, 0.0));
    if (N < 2) return matrix;

    std::vector<std::vector<double>> series(N);
    for (int i = 0; i < N; ++i) {
        const auto &obs = observations(names[i]);
        for (const auto &o : obs)
            series[i].push_back(o.value);
    }

    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            const auto &X = series[i];
            const auto &Y = series[j];
            int len = std::min(static_cast<int>(X.size()),
                               static_cast<int>(Y.size()));
            if (len < 3) { matrix[i][j] = 0.0; continue; }
            std::vector<double> xv(X.begin(), X.begin() + len);
            std::vector<double> yv(Y.begin(), Y.begin() + len);
            matrix[i][j] = correlationPearson(xv, yv);
        }
    }
    return matrix;
}

// ── Mutual Information ──────────────────────────────────────

std::vector<LeMiEntry>
LearningEngine::computeMutualInformation(const std::string &variableKey) const {
    std::shared_lock lock(m_mutex);
    std::vector<LeMiEntry> entries;
    const auto &obs = observations(variableKey);
    if (obs.size() < 5) return entries;

    std::vector<double> values;
    for (const auto &o : obs) values.push_back(o.value);

    std::unordered_set<uint32_t> commonIds;
    bool first = true;
    for (const auto &o : obs) {
        std::unordered_set<uint32_t> ids;
        for (const auto &kv : o.idAverageBytes) ids.insert(kv.first);
        if (first) { commonIds = ids; first = false; }
        else {
            std::unordered_set<uint32_t> inter;
            for (auto id : commonIds) if (ids.count(id)) inter.insert(id);
            commonIds = std::move(inter);
        }
    }

    for (uint32_t id : commonIds) {
        for (int b = 0; b < 64; ++b) {
            std::vector<double> byteVals;
            for (const auto &o : obs) {
                auto it = o.idAverageBytes.find(id);
                if (it != o.idAverageBytes.end())
                    byteVals.push_back(static_cast<double>(it->second[b]));
            }
            if (byteVals.size() < 3) continue;

            const int bins = 10;
            double minVal = *std::min_element(values.begin(), values.end());
            double maxVal = *std::max_element(values.begin(), values.end());
            double minByte = *std::min_element(byteVals.begin(), byteVals.end());
            double maxByte = *std::max_element(byteVals.begin(), byteVals.end());
            double rangeVal = maxVal - minVal + 1e-9;
            double rangeByte = maxByte - minByte + 1e-9;

            std::vector<std::vector<double>> jointHist(bins,
                std::vector<double>(bins, 0.0));
            for (size_t i = 0; i < values.size(); ++i) {
                int vi = std::min(bins - 1,
                    static_cast<int>((values[i] - minVal) / rangeVal * bins));
                int bi = std::min(bins - 1,
                    static_cast<int>((byteVals[i] - minByte) / rangeByte * bins));
                jointHist[vi][bi] += 1.0;
            }

            double total = static_cast<double>(values.size());
            for (int i = 0; i < bins; ++i)
                for (int j = 0; j < bins; ++j)
                    jointHist[i][j] /= total;

            double Hx = 0.0, Hy = 0.0, Hxy = 0.0;
            std::vector<double> margX(bins, 0.0), margY(bins, 0.0);
            for (int i = 0; i < bins; ++i)
                for (int j = 0; j < bins; ++j) {
                    margX[i] += jointHist[i][j];
                    margY[j] += jointHist[i][j];
                }

            for (int i = 0; i < bins; ++i) {
                if (margX[i] > 0) Hx -= margX[i] * std::log(margX[i]);
                if (margY[i] > 0) Hy -= margY[i] * std::log(margY[i]);
            }
            for (int i = 0; i < bins; ++i)
                for (int j = 0; j < bins; ++j)
                    if (jointHist[i][j] > 0)
                        Hxy -= jointHist[i][j] * std::log(jointHist[i][j]);

            double mi = Hx + Hy - Hxy;
            double pear = correlationPearson(values, byteVals);

            std::string comp;
            if (mi > 0.1 && std::abs(pear) < 0.3) comp = "Nieliniowa";
            else if (mi > 0.1) comp = "Silna";
            else comp = "Slaba";

            entries.push_back({id, b, mi, pear, comp});
        }
    }

    std::sort(entries.begin(), entries.end(),
              [](const LeMiEntry &a, const LeMiEntry &b) {
                  return a.mi > b.mi;
              });
    return entries;
}

// ── Maximal Information Coefficient ─────────────────────────

std::vector<LeMicEntry>
LearningEngine::computeMIC(const std::string &variableKey) const {
    // Simplified MIC: use MI with optimal grid size 2×2 to 8×8
    std::shared_lock lock(m_mutex);
    std::vector<LeMicEntry> entries;
    const auto &obs = observations(variableKey);
    if (obs.size() < 5) return entries;

    std::vector<double> values;
    for (const auto &o : obs) values.push_back(o.value);

    std::unordered_set<uint32_t> commonIds;
    bool first = true;
    for (const auto &o : obs) {
        std::unordered_set<uint32_t> ids;
        for (const auto &kv : o.idAverageBytes) ids.insert(kv.first);
        if (first) { commonIds = ids; first = false; }
        else {
            std::unordered_set<uint32_t> inter;
            for (auto id : commonIds) if (ids.count(id)) inter.insert(id);
            commonIds = std::move(inter);
        }
    }

    auto computeMI = [&](const std::vector<double> &xvals,
                         const std::vector<double> &yvals,
                         int binsX, int binsY) -> double {
        double minX = *std::min_element(xvals.begin(), xvals.end());
        double maxX = *std::max_element(xvals.begin(), xvals.end());
        double minY = *std::min_element(yvals.begin(), yvals.end());
        double maxY = *std::max_element(yvals.begin(), yvals.end());
        double rangeX = maxX - minX + 1e-9;
        double rangeY = maxY - minY + 1e-9;

        std::vector<std::vector<double>> hist(binsX,
            std::vector<double>(binsY, 0.0));
        for (size_t i = 0; i < xvals.size(); ++i) {
            int ix = std::min(binsX - 1,
                static_cast<int>((xvals[i] - minX) / rangeX * binsX));
            int iy = std::min(binsY - 1,
                static_cast<int>((yvals[i] - minY) / rangeY * binsY));
            hist[ix][iy] += 1.0;
        }
        double total = static_cast<double>(xvals.size());
        for (int i = 0; i < binsX; ++i)
            for (int j = 0; j < binsY; ++j)
                hist[i][j] /= total;

        double Hx = 0, Hy = 0, Hxy = 0;
        std::vector<double> mx(binsX, 0), my(binsY, 0);
        for (int i = 0; i < binsX; ++i)
            for (int j = 0; j < binsY; ++j) {
                mx[i] += hist[i][j];
                my[j] += hist[i][j];
            }
        for (int i = 0; i < binsX; ++i)
            if (mx[i] > 0) Hx -= mx[i] * std::log(mx[i]);
        for (int j = 0; j < binsY; ++j)
            if (my[j] > 0) Hy -= my[j] * std::log(my[j]);
        for (int i = 0; i < binsX; ++i)
            for (int j = 0; j < binsY; ++j)
                if (hist[i][j] > 0) Hxy -= hist[i][j] * std::log(hist[i][j]);

        double mi = Hx + Hy - Hxy;
        double norm = std::log(std::min(binsX, binsY));
        return norm > 0 ? mi / norm : 0.0;
    };

    for (uint32_t id : commonIds) {
        for (int b = 0; b < 64; ++b) {
            std::vector<double> byteVals;
            for (const auto &o : obs) {
                auto it = o.idAverageBytes.find(id);
                if (it != o.idAverageBytes.end())
                    byteVals.push_back(static_cast<double>(it->second[b]));
            }
            if (byteVals.size() < 3) continue;

            double bestMic = 0.0;
            for (int g = 2; g <= 8; ++g) {
                double mi = computeMI(values, byteVals, g, g);
                if (mi > bestMic) bestMic = mi;
            }
            entries.push_back({id, b, bestMic});
        }
    }

    std::sort(entries.begin(), entries.end(),
              [](const LeMicEntry &a, const LeMicEntry &b) {
                  return a.mic > b.mic;
              });
    return entries;
}

// ── FFT ─────────────────────────────────────────────────────

void LearningEngine::computeDft(const std::vector<double> &signal, double fs,
                                 std::vector<double> &mags,
                                 std::vector<double> &freqs) {
    int N = static_cast<int>(signal.size());
    if (N < 2) return;

    int N2 = 1;
    while (N2 < N) N2 <<= 1;

    std::vector<std::complex<double>> x(N2, 0.0);
    for (int i = 0; i < N; ++i)
        x[i] = std::complex<double>(signal[i], 0.0);

    // Bit-reversal permutation
    for (int i = 1, j = 0; i < N2; ++i) {
        int bit = N2 >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }

    // Radix-2 butterflies
    for (int len = 2; len <= N2; len <<= 1) {
        double angle = -2.0 * M_PI / len;
        std::complex<double> wlen(std::cos(angle), std::sin(angle));
        for (int i = 0; i < N2; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (int j = 0; j < len / 2; ++j) {
                std::complex<double> u = x[i + j];
                std::complex<double> v = x[i + j + len / 2] * w;
                x[i + j] = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    int outBins = N2 / 2 + 1;
    mags.resize(outBins);
    freqs.resize(outBins);

    double scale = 1.0 / N;
    for (int k = 0; k < outBins; ++k) {
        mags[k] = std::abs(x[k]) * scale;
        freqs[k] = k * fs / N2;
    }
    if (!mags.empty()) mags[0] /= 2.0;  // DC correction
}

LearningEngine::FftResult
LearningEngine::runFftAnalysis(uint32_t targetId, int byteIdx) const {
    FftResult result;
    std::shared_lock lock(m_mutex);
    if (m_frameHistory.empty()) return result;

    std::vector<uint64_t> timestamps;
    std::vector<double> values;
    for (const auto &f : m_frameHistory) {
        if (f.id == targetId && byteIdx < f.dlc) {
            timestamps.push_back(f.timestamp);
            values.push_back(static_cast<double>(f.data[byteIdx]));
        }
    }
    int M = static_cast<int>(values.size());
    if (M < 4) return result;

    double totalDuration = 0.0;
    for (int i = 1; i < M; ++i)
        totalDuration += static_cast<double>(timestamps[i] - timestamps[i - 1]);
    double avgInterval = totalDuration / (M - 1);
    if (avgInterval < 1.0) avgInterval = 1.0;

    double fs_hz = 1'000'000.0 / avgInterval;
    computeDft(values, fs_hz, result.magnitudes, result.frequencies);
    result.fsHz = fs_hz;
    result.sampleCount = M;

    // Find peaks
    double magMax = 0.0;
    for (size_t i = 1; i < result.magnitudes.size(); ++i)
        if (result.magnitudes[i] > magMax) magMax = result.magnitudes[i];
    if (magMax < 0.01) magMax = 1.0;

    double threshold = magMax * 0.15;
    struct Peak { double freq; double mag; int idx; };
    std::vector<Peak> peaks;
    for (size_t i = 2; i < result.magnitudes.size() - 1; ++i) {
        if (result.magnitudes[i] > threshold &&
            result.magnitudes[i] > result.magnitudes[i-1] &&
            result.magnitudes[i] > result.magnitudes[i+1])
            peaks.push_back({result.frequencies[i],
                             result.magnitudes[i],
                             static_cast<int>(i)});
    }
    std::sort(peaks.begin(), peaks.end(),
              [](auto &a, auto &b) { return a.mag > b.mag; });

    int showN = std::min(10, static_cast<int>(peaks.size()));
    for (int i = 0; i < showN; ++i) {
        double periodMs = peaks[i].freq > 0.001 ? 1000.0 / peaks[i].freq : 0.0;
        std::string desc;
        if (peaks[i].freq < 0.5) desc = "Wolna zmiana (trend)";
        else if (periodMs > 900 && periodMs < 1100) desc = "Cykl ~1s";
        else if (periodMs > 90 && periodMs < 110) desc = "Cykl ~100ms (heartbeat)";
        else if (periodMs > 9 && periodMs < 11) desc = "Cykl ~10ms (szybki sensor)";
        else {
            std::ostringstream oss;
            oss << "Okresowy, T=" << static_cast<int>(periodMs) << "ms";
            desc = oss.str();
        }
        result.peaks.push_back({peaks[i].freq, peaks[i].mag, periodMs, desc});
    }
    return result;
}

// ── Neural network ──────────────────────────────────────────

static double relu(double x) { return x > 0 ? x : 0; }
static double reluDeriv(double x) { return x > 0 ? 1.0 : 0.0; }
static double sigmoid(double x) { return 1.0 / (1.0 + std::exp(-x)); }

LearningEngine::NnTrainingResult
LearningEngine::trainNeuralNetwork(
    const std::vector<LeCorrelationEntry> &correlations,
    const std::string &variableKey,
    int maxInputDim) {
    NnTrainingResult result;
    std::unique_lock lock(m_mutex);
    const auto &obs = observations(variableKey);
    if (obs.size() < 10) {
        result.status = "Za malo obserwacji (min. 10)";
        return result;
    }

    // Collect top features from correlations
    struct Feature { uint32_t id; int b; double corr; };
    std::vector<Feature> features;
    for (const auto &c : correlations) {
        if (static_cast<int>(features.size()) >= maxInputDim) break;
        features.push_back({c.id, c.byte, c.correlation});
    }
    if (features.size() < 4) {
        result.status = "Za malo cech (min. 4) – dodaj wiecej obserwacji";
        return result;
    }

    int D = static_cast<int>(features.size());
    int H1 = std::max(8, D / 2);
    int H2 = std::max(4, H1 / 2);

    // Build training data
    std::vector<std::vector<double>> X;
    std::vector<double> Y;
    for (const auto &o : obs) {
        std::vector<double> x(D);
        bool valid = true;
        for (int f = 0; f < D; ++f) {
            auto it = o.idAverageBytes.find(features[f].id);
            if (it != o.idAverageBytes.end() && features[f].b < 64) {
                x[f] = static_cast<double>(it->second[features[f].b]) / 255.0;
            } else {
                valid = false; break;
            }
        }
        if (valid) { X.push_back(x); Y.push_back(o.value); }
    }

    if (X.size() < 10) {
        result.status = "Za malo kompletnych probek (" +
                        std::to_string(X.size()) + ")";
        return result;
    }

    // Normalize Y
    double yMin = *std::min_element(Y.begin(), Y.end());
    double yMax = *std::max_element(Y.begin(), Y.end());
    double yRange = yMax - yMin;
    if (yRange < 1e-9) yRange = 1.0;
    for (auto &y : Y) y = (y - yMin) / yRange;

    // Initialize weights
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> wdist(-0.1, 0.1);
    auto &w1 = m_nnWeights.w1; w1.resize(D, std::vector<double>(H1));
    auto &w2 = m_nnWeights.w2; w2.resize(H1, std::vector<double>(H2));
    auto &w3 = m_nnWeights.w3; w3.resize(H2, std::vector<double>(1));
    auto &b1 = m_nnWeights.b1; b1.resize(H1, 0);
    auto &b2 = m_nnWeights.b2; b2.resize(H2, 0);
    m_nnWeights.b3 = 0;

    for (int i = 0; i < D; ++i)
        for (int j = 0; j < H1; ++j)
            w1[i][j] = wdist(rng);
    for (int i = 0; i < H1; ++i)
        for (int j = 0; j < H2; ++j)
            w2[i][j] = wdist(rng);
    for (int i = 0; i < H2; ++i)
        w3[i][0] = wdist(rng);

    // SGD training
    double lr = 0.01;
    int epochs = 200;
    int batchSize = 16;

    for (int epoch = 0; epoch < epochs; ++epoch) {
        // Shuffle
        for (int i = static_cast<int>(X.size()) - 1; i > 0; --i) {
            std::uniform_int_distribution<int> idist(0, i);
            int j = idist(rng);
            std::swap(X[i], X[j]);
            std::swap(Y[i], Y[j]);
        }

        for (size_t start = 0; start < X.size(); start += batchSize) {
            size_t end = std::min(start + batchSize, X.size());
            // Gradients
            std::vector<std::vector<double>> dw1(D, std::vector<double>(H1, 0));
            std::vector<std::vector<double>> dw2(H1, std::vector<double>(H2, 0));
            std::vector<std::vector<double>> dw3(H2, std::vector<double>(1, 0));
            std::vector<double> db1(H1, 0), db2(H2, 0);
            double db3 = 0;

            for (size_t s = start; s < end; ++s) {
                // Forward
                std::vector<double> h1(H1), h2(H2);
                for (int j = 0; j < H1; ++j) {
                    double z = b1[j];
                    for (int i = 0; i < D; ++i) z += w1[i][j] * X[s][i];
                    h1[j] = relu(z);
                }
                for (int j = 0; j < H2; ++j) {
                    double z = b2[j];
                    for (int i = 0; i < H1; ++i) z += w2[i][j] * h1[i];
                    h2[j] = relu(z);
                }
                double out = m_nnWeights.b3;
                for (int i = 0; i < H2; ++i) out += w3[i][0] * h2[i];
                out = sigmoid(out);

                double error = out - Y[s];

                // Backward
                double dOut = error * out * (1.0 - out);
                std::vector<double> dH2(H2, 0);
                for (int i = 0; i < H2; ++i) dH2[i] = dOut * w3[i][0] * reluDeriv(h2[i]);
                std::vector<double> dH1(H1, 0);
                for (int i = 0; i < H1; ++i)
                    for (int j = 0; j < H2; ++j)
                        dH1[i] += dH2[j] * w2[i][j] * reluDeriv(h1[i]);

                // Accumulate
                db3 += dOut;
                for (int i = 0; i < H2; ++i) {
                    dw3[i][0] += dOut * h2[i];
                    db2[i] += dH2[i];
                }
                for (int i = 0; i < H1; ++i) {
                    for (int j = 0; j < H2; ++j)
                        dw2[i][j] += dH2[j] * h1[i];
                    db1[i] += dH1[i];
                }
                for (int i = 0; i < D; ++i)
                    for (int j = 0; j < H1; ++j)
                        dw1[i][j] += dH1[j] * X[s][i];
            }

            // Update
            int bs = static_cast<int>(end - start);
            for (int i = 0; i < D; ++i)
                for (int j = 0; j < H1; ++j)
                    w1[i][j] -= lr * dw1[i][j] / bs;
            for (int i = 0; i < H1; ++i)
                for (int j = 0; j < H2; ++j)
                    w2[i][j] -= lr * dw2[i][j] / bs;
            for (int i = 0; i < H2; ++i)
                w3[i][0] -= lr * dw3[i][0] / bs;
            for (int i = 0; i < H1; ++i)
                b1[i] -= lr * db1[i] / bs;
            for (int i = 0; i < H2; ++i)
                b2[i] -= lr * db2[i] / bs;
            m_nnWeights.b3 -= lr * db3 / bs;
        }
    }

    m_nnWeights.trained = true;
    result.trained = true;
    result.inputDim = D;
    result.hidden1 = H1;
    result.hidden2 = H2;
    result.sampleCount = static_cast<int>(X.size());
    result.status = "Wytrenowany (epochs=" + std::to_string(epochs) + ")";
    return result;
}

double LearningEngine::predictNeural(const std::vector<double> &input) const {
    if (!m_nnWeights.trained || input.empty()) return 0.0;
    const auto &w1 = m_nnWeights.w1;
    const auto &w2 = m_nnWeights.w2;
    const auto &w3 = m_nnWeights.w3;

    int D = static_cast<int>(w1.size());
    int H1 = w1.empty() ? 0 : static_cast<int>(w1[0].size());
    int H2 = w2.empty() ? 0 : static_cast<int>(w2[0].size());

    std::vector<double> h1(H1, 0), h2(H2, 0);
    for (int j = 0; j < H1; ++j) {
        double z = m_nnWeights.b1[j];
        for (int i = 0; i < D && i < static_cast<int>(input.size()); ++i)
            z += w1[i][j] * input[i];
        h1[j] = relu(z);
    }
    for (int j = 0; j < H2; ++j) {
        double z = m_nnWeights.b2[j];
        for (int i = 0; i < H1; ++i)
            z += w2[i][j] * h1[i];
        h2[j] = relu(z);
    }
    double out = m_nnWeights.b3;
    for (int i = 0; i < H2; ++i)
        out += w3[i][0] * h2[i];
    return sigmoid(out);
}

// ── Auto-discovery ──────────────────────────────────────────

std::vector<LeCorrelationEntry>
LearningEngine::autoDiscovery(const std::string &variableKey) const {
    std::shared_lock lock(m_mutex);
    std::vector<LeCorrelationEntry> entries;
    const auto &obs = observations(variableKey);
    if (obs.size() < 5 || m_frameHistory.empty()) return entries;

    uint64_t now = m_frameHistory.back().timestamp;
    std::unordered_set<uint32_t> recentIds;
    for (auto it = m_frameHistory.rbegin(); it != m_frameHistory.rend(); ++it) {
        if (now - it->timestamp > 2'000'000) break;
        recentIds.insert(it->id);
    }

    for (uint32_t id : recentIds) {
        for (int b = 0; b < 64; ++b) {
            std::vector<double> bx, by;
            for (const auto &o : obs) {
                auto it = o.idAverageBytes.find(id);
                if (it != o.idAverageBytes.end()) {
                    bx.push_back(o.value);
                    by.push_back(static_cast<double>(it->second[b]));
                }
            }
            int N = static_cast<int>(bx.size());
            if (N < 5) continue;
            double corr = correlationPearson(bx, by);
            if (std::abs(corr) < 0.5) continue;
            double pv = pearsonPValue(corr, N);
            entries.push_back({id, b, corr, pv, pv < 0.05});
        }
    }

    std::sort(entries.begin(), entries.end(),
              [](const LeCorrelationEntry &a, const LeCorrelationEntry &b) {
                  return std::abs(a.correlation) > std::abs(b.correlation);
              });
    return entries;
}

// ── Statistical helpers ─────────────────────────────────────

double LearningEngine::pearsonPValue(double r, int n) {
    if (n <= 2) return 1.0;
    if (std::abs(r) >= 1.0) return 0.0;
    double t = r * std::sqrt((n - 2) / (1.0 - r * r));
    double df = n - 2.0;
    double x = df / (df + t * t);
    double a = df / 2.0, b = 0.5;
    double bt = (x > 0 && x < 1)
        ? std::exp(std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b) +
                   a * std::log(x) + b * std::log(1.0 - x))
        : 0.0;
    double p = bt * (x < (a + 1.0) / (a + b + 2.0)
                     ? (1.0 / a) : (1.0 / b));
    return std::min(1.0, 2.0 * std::max(p, 1.0 - p));
}

double LearningEngine::correlationPearson(const std::vector<double> &x,
                                          const std::vector<double> &y) {
    int N = std::min(static_cast<int>(x.size()), static_cast<int>(y.size()));
    if (N < 3) return 0.0;
    double sx = 0, sy = 0, sxy = 0, sx2 = 0, sy2 = 0;
    for (int i = 0; i < N; ++i) {
        double xi = x[i], yi = y[i];
        sx += xi; sy += yi; sxy += xi * yi;
        sx2 += xi * xi; sy2 += yi * yi;
    }
    double den = std::sqrt((N * sx2 - sx * sx) * (N * sy2 - sy * sy));
    return den != 0 ? (N * sxy - sx * sy) / den : 0.0;
}

double LearningEngine::correlationPearson(const std::vector<float> &x,
                                          const std::vector<float> &y) {
    int N = std::min(static_cast<int>(x.size()), static_cast<int>(y.size()));
    if (N < 3) return 0.0;
    double sx = 0, sy = 0, sxy = 0, sx2 = 0, sy2 = 0;
    for (int i = 0; i < N; ++i) {
        double xi = x[i], yi = y[i];
        sx += xi; sy += yi; sxy += xi * yi;
        sx2 += xi * xi; sy2 += yi * yi;
    }
    double den = std::sqrt((N * sx2 - sx * sx) * (N * sy2 - sy * sy));
    return den != 0 ? (N * sxy - sx * sy) / den : 0.0;
}

// ── Adaptive window ─────────────────────────────────────────

void LearningEngine::recalcAdaptiveWindow() {
    std::unique_lock lock(m_mutex);
    if (m_events.empty()) return;
    const auto &win = m_events[0].windowFrames;
    if (win.size() < 2) return;
    std::vector<int64_t> deltas;
    for (size_t i = 1; i < win.size(); ++i)
        deltas.push_back(win[i].timestamp - win[i - 1].timestamp);
    double mean = std::accumulate(deltas.begin(), deltas.end(), 0.0) /
                  static_cast<double>(deltas.size());
    m_adaptiveBefore = std::max<int64_t>(100000,
        std::min<int64_t>(2000000, static_cast<int64_t>(3 * mean)));
    m_adaptiveAfter = m_adaptiveBefore / 3;
}

// ── Serialization (stub — returns empty JSON for now) ───────

std::string LearningEngine::serializeSession() const {
    // TODO: full JSON serialization with STL types (Phase F task)
    std::shared_lock lock(m_mutex);
    return "{\"iteration\":" + std::to_string(m_iteration) + "}";
}

bool LearningEngine::deserializeSession(const std::string &json) {
    // TODO: full JSON deserialization (Phase F task)
    (void)json;
    return false;
}
