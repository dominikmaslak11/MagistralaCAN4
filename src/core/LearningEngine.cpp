#include "LearningEngine.h"
#include <algorithm>
#include <cmath>
#include <complex>
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
                if (i < f.dlc) sum += f.data[i];
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

// ── Auto-increment byte filter ──────────────────────────────

std::unordered_set<uint64_t> LearningEngine::detectAutoIncrementBytes() const {
    std::shared_lock lock(m_mutex);
    std::unordered_set<uint64_t> result;
    if (m_frameHistory.size() < 10) return result;

    // Group frames by ID
    std::unordered_map<uint32_t, std::vector<CanFrame>> byId;
    for (const auto &f : m_frameHistory)
        byId[f.id].push_back(f);

    for (const auto &kv : byId) {
        uint32_t id = kv.first;
        const auto &frames = kv.second;
        if (frames.size() < 5) continue;

        for (int b = 0; b < 8; ++b) {
            // Check if byte always changes by +1 (with wrap)
            int changes = 0;
            int incByOne = 0;
            int totalPairs = 0;
            for (size_t i = 1; i < frames.size(); ++i) {
                if (b >= frames[i].dlc || b >= frames[i-1].dlc) continue;
                int prev = frames[i-1].data[b];
                int curr = frames[i].data[b];
                totalPairs++;
                if (prev != curr) {
                    changes++;
                    int diff = (curr - prev) & 0xFF;
                    if (diff == 1 || diff == 0xFF)  // +1 or wrap (255→0)
                        incByOne++;
                }
            }
            // Auto-increment: >90% of changes are +1, and >80% of pairs change
            if (totalPairs > 4 && changes > totalPairs * 0.8 && incByOne > changes * 0.9) {
                result.insert((static_cast<uint64_t>(id) << 8) | b);
            }
        }
    }
    return result;
}

// ── Correlation table (Pearson) ────────────────────────────

std::vector<LeCorrelationEntry>
LearningEngine::computeCorrelations(const std::string &variableKey) const {
    std::shared_lock lock(m_mutex);
    std::vector<LeCorrelationEntry> entries;
    const auto &obs = observations(variableKey);
    if (obs.size() < 3) return entries;

    // Find IDs present in majority of observations (>= 50%)
    std::unordered_map<uint32_t, int> idCount;
    for (const auto &o : obs) {
        std::unordered_set<uint32_t> seen;
        for (const auto &kv : o.idAverageBytes) {
            if (!seen.count(kv.first)) {
                seen.insert(kv.first);
                idCount[kv.first]++;
            }
        }
    }
    int threshold = std::max(3, static_cast<int>(obs.size()) / 2);
    std::unordered_set<uint32_t> common;
    for (const auto &kv : idCount)
        if (kv.second >= threshold)
            common.insert(kv.first);

    // Detect auto-increment bytes to filter out
    auto aiBytes = detectAutoIncrementBytes();

    for (uint32_t id : common) {
        for (int b = 0; b < 64; ++b) {
            std::vector<double> vx, vy;
            std::vector<uint64_t> timestamps;
            for (const auto &o : obs) {
                auto it = o.idAverageBytes.find(id);
                if (it != o.idAverageBytes.end()) {
                    vx.push_back(o.value);
                    vy.push_back(static_cast<double>(it->second[b]));
                    timestamps.push_back(o.timestamp);
                }
            }
            int N = static_cast<int>(vx.size());
            if (N < 3) continue;
            // Skip auto-increment bytes (counters, timestamps)
            if (aiBytes.count((static_cast<uint64_t>(id) << 8) | b)) continue;
            double corr = (m_decayLambda > 0.0)
                ? correlationPearsonWeighted(vx, vy, timestamps, m_decayLambda)
                : correlationPearson(vx, vy);
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

    // #34: Pre-compute variance per (ID, byte) to skip zero-variance pairs
    std::unordered_map<uint64_t, double> varCache;
    for (uint32_t id : common) {
        for (int b = 0; b < 8; ++b) {
            double sum = 0, sq = 0; int cnt = 0;
            for (const auto &o : obs) {
                auto it = o.idAverageBytes.find(id);
                if (it != o.idAverageBytes.end()) {
                    double v = it->second[b];
                    sum += v; sq += v * v; cnt++;
                }
            }
            if (cnt >= 3) {
                double v = sq / cnt - (sum / cnt) * (sum / cnt);
                varCache[(static_cast<uint64_t>(id) << 8) | b] = v;
            }
        }
    }

    std::vector<uint32_t> idList(common.begin(), common.end());
    for (size_t i = 0; i < idList.size(); ++i) {
        uint64_t ki = static_cast<uint64_t>(idList[i]) << 8;
        for (size_t j = 0; j < idList.size(); ++j) {
            uint64_t kj = static_cast<uint64_t>(idList[j]) << 8;
            for (int b1 = 0; b1 < 8; ++b1) {
                double v1 = varCache[ki | b1];
                if (v1 < 1e-9) continue;  // #34: skip zero-variance bytes
                for (int b2 = 0; b2 < 8; ++b2) {
                    double v2 = varCache[kj | b2];
                    if (v2 < 1e-9) continue;  // #34: skip zero-variance bytes
                    std::vector<double> vb1, vb2;
                    std::vector<uint64_t> timestamps;
                    for (const auto &o : obs) {
                        auto it1 = o.idAverageBytes.find(idList[i]);
                        auto it2 = o.idAverageBytes.find(idList[j]);
                        if (it1 != o.idAverageBytes.end() &&
                            it2 != o.idAverageBytes.end()) {
                            vb1.push_back(static_cast<double>(it1->second[b1]));
                            vb2.push_back(static_cast<double>(it2->second[b2]));
                            timestamps.push_back(o.timestamp);
                        }
                    }
                    if (vb1.size() < 3) continue;
                    double corr = (m_decayLambda > 0.0)
                        ? correlationPearsonWeighted(vb1, vb2, timestamps, m_decayLambda)
                        : correlationPearson(vb1, vb2);
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
            // GPU-accelerated distance (#kMeans-GPU)
            std::vector<std::vector<float>> gpuDists;
            if (m_gpu.isAvailable() && N * K > 200) {
                gpuDists = m_gpu.kmeansDistances(data, centroids);
            }
            int bestIdx = 0;
            double bestDist = std::numeric_limits<double>::max();
            for (int k = 0; k < K; ++k) {
                double d2;
                if (!gpuDists.empty())
                    d2 = gpuDists[i][k];
                else {
                    d2 = 0.0;
                    for (int d = 0; d < dim; ++d) {
                        double diff = data[i][d] - centroids[k][d];
                        d2 += diff * diff;
                    }
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

// ── #36 k-means++ initialization ────────────────────────────

int LearningEngine::kMeansPP(const std::vector<std::vector<float>> &data,
                              int K, std::vector<int> &assignments) {
    int N = static_cast<int>(data.size());
    if (N == 0) return 0;
    int dim = static_cast<int>(data[0].size());
    assignments.resize(N, 0);

    std::vector<std::vector<float>> centroids(K, std::vector<float>(dim));
    std::mt19937 rng(42);

    // 1. Choose first centroid uniformly at random
    std::uniform_int_distribution<int> dist(0, N - 1);
    centroids[0] = data[dist(rng)];

    // 2. For each subsequent centroid, choose with probability proportional to D(x)^2
    std::vector<double> minDistSq(N, std::numeric_limits<double>::max());
    for (int k = 1; k < K; ++k) {
        double totalDist = 0.0;
        for (int i = 0; i < N; ++i) {
            double d2 = 0.0;
            for (int d = 0; d < dim; ++d) {
                double diff = data[i][d] - centroids[k-1][d];
                d2 += diff * diff;
            }
            if (d2 < minDistSq[i]) minDistSq[i] = d2;
            totalDist += minDistSq[i];
        }
        if (totalDist < 1e-9) {
            // All points are the same; pick random
            centroids[k] = data[dist(rng)];
            continue;
        }
        std::uniform_real_distribution<double> urd(0.0, totalDist);
        double r = urd(rng);
        double accum = 0.0;
        int chosen = 0;
        for (int i = 0; i < N; ++i) {
            accum += minDistSq[i];
            if (accum >= r) { chosen = i; break; }
        }
        centroids[k] = data[chosen];
    }

    // 3. Run standard k-means from these initial centroids
    int iter = 0, maxIter = 100;
    while (iter++ < maxIter) {
        std::vector<int> counts(K, 0);
        std::vector<std::vector<float>> newCtr(K, std::vector<float>(dim, 0.0f));
        for (int i = 0; i < N; ++i) {
            // GPU-accelerated distance (#kMeans-GPU)
            std::vector<std::vector<float>> gpuDists;
            if (m_gpu.isAvailable() && N * K > 200) {
                gpuDists = m_gpu.kmeansDistances(data, centroids);
            }
            int bestIdx = 0;
            double bestDist = std::numeric_limits<double>::max();
            for (int k = 0; k < K; ++k) {
                double d2;
                if (!gpuDists.empty())
                    d2 = gpuDists[i][k];
                else {
                    d2 = 0.0;
                    for (int d = 0; d < dim; ++d) {
                        double diff = data[i][d] - centroids[k][d];
                        d2 += diff * diff;
                    }
                }
                if (d2 < bestDist) { bestDist = d2; bestIdx = k; }
            }
            assignments[i] = bestIdx;
            counts[bestIdx]++;
            for (int d = 0; d < dim; ++d) newCtr[bestIdx][d] += data[i][d];
        }
        bool changed = false;
        for (int k = 0; k < K; ++k) {
            if (counts[k] > 0)
                for (int d = 0; d < dim; ++d) newCtr[k][d] /= counts[k];
            double diff = 0.0;
            for (int d = 0; d < dim; ++d)
                diff += (centroids[k][d] - newCtr[k][d]) * (centroids[k][d] - newCtr[k][d]);
            if (diff > 0.001) changed = true;
            centroids[k] = newCtr[k];
        }
        if (!changed) break;
    }
    return K;
}

// ── DBSCAN with k-d tree (#35) ─────────────────────────────

// Simple k-d tree for 5D float data (no external dependencies)
struct KdNode {
    int pointIdx;
    int splitDim;
    float splitVal;
    KdNode *left = nullptr;
    KdNode *right = nullptr;
    ~KdNode() { delete left; delete right; }
};

static KdNode* buildKdTree(const std::vector<std::vector<float>> &data,
                           std::vector<int> indices, int depth = 0) {
    if (indices.empty()) return nullptr;
    int dim = static_cast<int>(data[0].size());
    int axis = depth % dim;

    // Sort indices by axis
    std::sort(indices.begin(), indices.end(),
              [&](int a, int b) { return data[a][axis] < data[b][axis]; });
    int mid = static_cast<int>(indices.size()) / 2;

    auto *node = new KdNode;
    node->pointIdx = indices[mid];
    node->splitDim = axis;
    node->splitVal = data[indices[mid]][axis];
    node->left = buildKdTree(data, {indices.begin(), indices.begin() + mid}, depth + 1);
    node->right = buildKdTree(data, {indices.begin() + mid + 1, indices.end()}, depth + 1);
    return node;
}

static void kdRangeQuery(KdNode *node, const std::vector<std::vector<float>> &data,
                         const std::vector<float> &query, float eps2,
                         std::vector<int> &result) {
    if (!node) return;
    int dim = static_cast<int>(data[0].size());
    const auto &pt = data[node->pointIdx];

    // Check this point
    float d2 = 0.0f;
    for (int d = 0; d < dim; ++d) {
        float diff = pt[d] - query[d];
        d2 += diff * diff;
    }
    if (d2 <= eps2) result.push_back(node->pointIdx);

    // Decide which side to explore
    int axis = node->splitDim;
    float diff = query[axis] - node->splitVal;

    if (diff < 0) {
        kdRangeQuery(node->left, data, query, eps2, result);
        if (diff * diff <= eps2)
            kdRangeQuery(node->right, data, query, eps2, result);
    } else {
        kdRangeQuery(node->right, data, query, eps2, result);
        if (diff * diff <= eps2)
            kdRangeQuery(node->left, data, query, eps2, result);
    }
}

int LearningEngine::dbscan(const std::vector<std::vector<float>> &data,
                            float eps, int minPts,
                            std::vector<int> &assignments) {
    int N = static_cast<int>(data.size());
    if (N == 0) return 0;
    assignments.resize(N, -1);

    // #35: Build k-d tree instead of O(N²) distance matrix
    std::vector<int> allIdx(N);
    std::iota(allIdx.begin(), allIdx.end(), 0);
    KdNode *tree = buildKdTree(data, allIdx);

    float eps2 = eps * eps;
    int clusterId = 0;

    for (int p = 0; p < N; ++p) {
        if (assignments[p] != -1) continue;

        std::vector<int> neighbors;
        kdRangeQuery(tree, data, data[p], eps2, neighbors);

        if (static_cast<int>(neighbors.size()) < minPts) continue;

        assignments[p] = clusterId;
        for (size_t ni = 0; ni < neighbors.size(); ++ni) {
            int q = neighbors[ni];
            if (assignments[q] != -1) continue;
            assignments[q] = clusterId;

            std::vector<int> qn;
            kdRangeQuery(tree, data, data[q], eps2, qn);
            if (static_cast<int>(qn.size()) >= minPts)
                for (int r : qn)
                    if (std::find(neighbors.begin(), neighbors.end(), r) ==
                        neighbors.end())
                        neighbors.push_back(r);
        }
        clusterId++;
    }
    delete tree;
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
    kMeansPP(features, K, assignments);  // #36

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
        kMeansPP(features, k, assignments);  // #36

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

        // 3. Build correlation matrix via GPU (#PCA-GPU)
    // Transpose: features[dim][N] for GPU correlation
    std::vector<std::vector<float>> transposed(dim, std::vector<float>(N));
    for (int d = 0; d < dim; ++d)
        for (int i = 0; i < N; ++i)
            transposed[d][i] = centered[i][d];

    auto corrMat = m_gpu.correlationMatrix(transposed);
    std::vector<std::vector<double>> cov(dim, std::vector<double>(dim, 0.0));
    for (int i = 0; i < dim; ++i)
        for (int j = 0; j < dim; ++j)
            cov[i][j] = corrMat[i][j];

double origTrace = 0.0;
    for (int d = 0; d < dim; ++d) origTrace += cov[d][d];

    // #37: Jacobi eigen-decomposition (full, for 5×5 matrix)
    std::vector<double> eigenvalues;
    std::vector<std::vector<double>> eigenvectors;
    jacobiEigen(cov, eigenvalues, eigenvectors, 50);

    result.pc1.resize(dim);
    result.pc2.resize(dim);
    for (int d = 0; d < dim; ++d) {
        result.pc1[d] = eigenvectors[d][0];  // first eigenvector (column 0)
        result.pc2[d] = eigenvectors[d][1];  // second eigenvector (column 1)
    }
    result.eig1 = eigenvalues[0];
    result.eig2 = eigenvalues[1];
    double totalVar = 0.0;
    for (double e : eigenvalues) totalVar += e;
    result.varianceExplained = (eigenvalues[0] + eigenvalues[1]) / (totalVar > 0 ? totalVar : origTrace);

    // 5. Project to 2D
    for (int i = 0; i < N; ++i) {
        double x = 0.0, y = 0.0;
        for (int d = 0; d < dim; ++d) {
            x += centered[i][d] * result.pc1[d];
            y += centered[i][d] * result.pc2[d];
        }
        result.projected.push_back({x, y});
    }

    // 6. k-means on 2D (K=3)
    std::vector<std::vector<float>> data2D(N, std::vector<float>(2));
    for (int i = 0; i < N; ++i) {
        data2D[i][0] = static_cast<float>(result.projected[i].first);
        data2D[i][1] = static_cast<float>(result.projected[i].second);
    }
    kMeansPP(data2D, 3, result.clusterAssignments);  // #36

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
        std::vector<uint64_t> timestamps;
        for (const auto &o : obs) {
            auto it = o.idAverageBytes.find(id);
            if (it != o.idAverageBytes.end()) {
                X.push_back(static_cast<double>(it->second[byteIdx]));
                Y.push_back(o.value);
                timestamps.push_back(o.timestamp);
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
        double corr = (m_decayLambda > 0.0)
            ? correlationPearsonWeighted(X, Y, timestamps, m_decayLambda)
            : (N * sxy - sx * sy) /
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

    // Find IDs present in majority of observations (>= 50%)
    std::unordered_map<uint32_t, int> idCount;
    for (const auto &o : obs) {
        std::unordered_set<uint32_t> seen;
        for (const auto &kv : o.idAverageBytes) {
            if (!seen.count(kv.first)) {
                seen.insert(kv.first);
                idCount[kv.first]++;
            }
        }
    }
    int threshold = std::max(3, static_cast<int>(obs.size()) / 2);
    std::unordered_set<uint32_t> commonIds;
    for (const auto &kv : idCount)
        if (kv.second >= threshold)
            commonIds.insert(kv.first);

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

    // Find IDs present in majority of observations (>= 50%)
    std::unordered_map<uint32_t, int> idCount;
    for (const auto &o : obs) {
        std::unordered_set<uint32_t> seen;
        for (const auto &kv : o.idAverageBytes) {
            if (!seen.count(kv.first)) {
                seen.insert(kv.first);
                idCount[kv.first]++;
            }
        }
    }
    int threshold = std::max(3, static_cast<int>(obs.size()) / 2);
    std::unordered_set<uint32_t> commonIds;
    for (const auto &kv : idCount)
        if (kv.second >= threshold)
            commonIds.insert(kv.first);

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

    // ── #30 MLP v2: Adam + L2 regularization + early stopping ──
    double lr = 0.001;
    double l2_lambda = 0.0001;  // L2 regularization strength
    int epochs = 500;
    int batchSize = 16;
    int patience = 20;          // early stopping patience
    const double beta1 = 0.9, beta2 = 0.999, eps_adam = 1e-8;

    // Split: 80% train, 20% validation
    int trainN = static_cast<int>(X.size() * 0.8);
    if (trainN < 10) trainN = static_cast<int>(X.size());

    // Initialize Adam state
    auto &m_w1 = m_nnWeights.m_w1; m_w1.assign(D, std::vector<double>(H1, 0));
    auto &v_w1 = m_nnWeights.v_w1; v_w1.assign(D, std::vector<double>(H1, 0));
    auto &m_w2 = m_nnWeights.m_w2; m_w2.assign(H1, std::vector<double>(H2, 0));
    auto &v_w2 = m_nnWeights.v_w2; v_w2.assign(H1, std::vector<double>(H2, 0));
    auto &m_w3 = m_nnWeights.m_w3; m_w3.assign(H2, std::vector<double>(1, 0));
    auto &v_w3 = m_nnWeights.v_w3; v_w3.assign(H2, std::vector<double>(1, 0));
    m_nnWeights.m_b1.assign(H1, 0); m_nnWeights.v_b1.assign(H1, 0);
    m_nnWeights.m_b2.assign(H2, 0); m_nnWeights.v_b2.assign(H2, 0);
    m_nnWeights.m_b3 = 0; m_nnWeights.v_b3 = 0;

    double bestValLoss = std::numeric_limits<double>::max();
    int epochsNoImprove = 0;

    for (int epoch = 0; epoch < epochs; ++epoch) {
        // Shuffle training data
        for (int i = trainN - 1; i > 0; --i) {
            std::uniform_int_distribution<int> idist(0, i);
            int j = idist(rng);
            std::swap(X[i], X[j]);
            std::swap(Y[i], Y[j]);
        }

        double trainLoss = 0.0;
        int trainCount = 0;

        for (int start = 0; start < trainN; start += batchSize) {
            int end = std::min(start + batchSize, trainN);
            m_nnWeights.adamStep++;

            std::vector<std::vector<double>> dw1(D, std::vector<double>(H1, 0));
            std::vector<std::vector<double>> dw2(H1, std::vector<double>(H2, 0));
            std::vector<std::vector<double>> dw3(H2, std::vector<double>(1, 0));
            std::vector<double> db1(H1, 0), db2(H2, 0);
            double db3 = 0;

            for (int s = start; s < end; ++s) {
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
                trainLoss += error * error;
                trainCount++;

                // Backward
                double dOut = error * out * (1.0 - out);
                std::vector<double> dH2(H2, 0);
                for (int i = 0; i < H2; ++i) dH2[i] = dOut * w3[i][0] * reluDeriv(h2[i]);
                std::vector<double> dH1(H1, 0);
                for (int i = 0; i < H1; ++i)
                    for (int j = 0; j < H2; ++j)
                        dH1[i] += dH2[j] * w2[i][j] * reluDeriv(h1[i]);

                // Accumulate gradients
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

            int bs = end - start;
            double inv_bs = 1.0 / bs;
            double step = m_nnWeights.adamStep;
            double bc1 = 1.0 / (1.0 - std::pow(beta1, step));
            double bc2 = 1.0 / (1.0 - std::pow(beta2, step));

            // Adam update for w1
            for (int i = 0; i < D; ++i) {
                for (int j = 0; j < H1; ++j) {
                    double g = dw1[i][j] * inv_bs + l2_lambda * w1[i][j]; // L2 grad
                    m_w1[i][j] = beta1 * m_w1[i][j] + (1.0 - beta1) * g;
                    v_w1[i][j] = beta2 * v_w1[i][j] + (1.0 - beta2) * g * g;
                    w1[i][j] -= lr * (m_w1[i][j] * bc1) / (std::sqrt(v_w1[i][j] * bc2) + eps_adam);
                }
            }
            // Adam update for w2
            for (int i = 0; i < H1; ++i) {
                for (int j = 0; j < H2; ++j) {
                    double g = dw2[i][j] * inv_bs + l2_lambda * w2[i][j];
                    m_w2[i][j] = beta1 * m_w2[i][j] + (1.0 - beta1) * g;
                    v_w2[i][j] = beta2 * v_w2[i][j] + (1.0 - beta2) * g * g;
                    w2[i][j] -= lr * (m_w2[i][j] * bc1) / (std::sqrt(v_w2[i][j] * bc2) + eps_adam);
                }
            }
            // Adam update for w3
            for (int i = 0; i < H2; ++i) {
                double g = dw3[i][0] * inv_bs + l2_lambda * w3[i][0];
                m_w3[i][0] = beta1 * m_w3[i][0] + (1.0 - beta1) * g;
                v_w3[i][0] = beta2 * v_w3[i][0] + (1.0 - beta2) * g * g;
                w3[i][0] -= lr * (m_w3[i][0] * bc1) / (std::sqrt(v_w3[i][0] * bc2) + eps_adam);
            }
            // Adam update for biases (no L2 on biases)
            for (int i = 0; i < H1; ++i) {
                double g = db1[i] * inv_bs;
                m_nnWeights.m_b1[i] = beta1 * m_nnWeights.m_b1[i] + (1.0 - beta1) * g;
                m_nnWeights.v_b1[i] = beta2 * m_nnWeights.v_b1[i] + (1.0 - beta2) * g * g;
                b1[i] -= lr * (m_nnWeights.m_b1[i] * bc1) / (std::sqrt(m_nnWeights.v_b1[i] * bc2) + eps_adam);
            }
            for (int i = 0; i < H2; ++i) {
                double g = db2[i] * inv_bs;
                m_nnWeights.m_b2[i] = beta1 * m_nnWeights.m_b2[i] + (1.0 - beta1) * g;
                m_nnWeights.v_b2[i] = beta2 * m_nnWeights.v_b2[i] + (1.0 - beta2) * g * g;
                b2[i] -= lr * (m_nnWeights.m_b2[i] * bc1) / (std::sqrt(m_nnWeights.v_b2[i] * bc2) + eps_adam);
            }
            {
                double g = db3 * inv_bs;
                m_nnWeights.m_b3 = beta1 * m_nnWeights.m_b3 + (1.0 - beta1) * g;
                m_nnWeights.v_b3 = beta2 * m_nnWeights.v_b3 + (1.0 - beta2) * g * g;
                m_nnWeights.b3 -= lr * (m_nnWeights.m_b3 * bc1) / (std::sqrt(m_nnWeights.v_b3 * bc2) + eps_adam);
            }
        }

        // Validation loss (early stopping)
        if (trainN < static_cast<int>(X.size())) {
            double valLoss = 0.0;
            for (int s = trainN; s < static_cast<int>(X.size()); ++s) {
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
                double err = out - Y[s];
                valLoss += err * err;
            }
            valLoss /= (X.size() - trainN);

            if (valLoss < bestValLoss) {
                bestValLoss = valLoss;
                epochsNoImprove = 0;
            } else {
                epochsNoImprove++;
                if (epochsNoImprove >= patience) break;
            }
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
            std::vector<uint64_t> timestamps;
            for (const auto &o : obs) {
                auto it = o.idAverageBytes.find(id);
                if (it != o.idAverageBytes.end()) {
                    bx.push_back(o.value);
                    by.push_back(static_cast<double>(it->second[b]));
                    timestamps.push_back(o.timestamp);
                }
            }
            int N = static_cast<int>(bx.size());
            if (N < 5) continue;
            double corr = (m_decayLambda > 0.0)
                ? correlationPearsonWeighted(bx, by, timestamps, m_decayLambda)
                : correlationPearson(bx, by);
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

void LearningEngine::recalcAdaptiveWindowLocked() {
    // Caller must hold m_mutex
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

void LearningEngine::recalcAdaptiveWindow() {
    std::unique_lock lock(m_mutex);
    recalcAdaptiveWindowLocked();
}


// ── #31 Gradient Boosted Trees (XGBoost-lite) ───────────────

LearningEngine::GbtModel
LearningEngine::trainGbt(const std::string &variableKey,
                         int maxTrees, int maxDepth) const {
    GbtModel model;
    model.learningRate = 0.1;
    std::shared_lock lock(m_mutex);
    const auto &obs = observations(variableKey);
    if (obs.size() < 10) return model;

    // Build feature matrix: for each observation, extract all (id,byte) averages
    struct Feature { uint32_t id; int b; };
    std::vector<Feature> featureList;
    std::unordered_set<uint32_t> seenIds;
    for (const auto &o : obs)
        for (const auto &kv : o.idAverageBytes)
            if (!seenIds.count(kv.first)) {
                seenIds.insert(kv.first);
                for (int b = 0; b < 64; ++b)
                    featureList.push_back({kv.first, b});
            }

    int N = static_cast<int>(obs.size());
    int F = static_cast<int>(featureList.size());
    if (F == 0) return model;

    // Build feature matrix
    std::vector<std::vector<double>> X(N, std::vector<double>(F));
    std::vector<double> Y(N);
    for (int i = 0; i < N; ++i) {
        Y[i] = obs[i].value;
        for (int f = 0; f < F; ++f) {
            auto it = obs[i].idAverageBytes.find(featureList[f].id);
            X[i][f] = (it != obs[i].idAverageBytes.end())
                ? static_cast<double>(it->second[featureList[f].b]) : 0.0;
        }
    }

    // Base prediction = mean
    double base = 0;
    for (double y : Y) base += y;
    base /= N;
    model.basePrediction = base;

    // Residuals
    std::vector<double> residuals(N);
    for (int i = 0; i < N; ++i) residuals[i] = Y[i] - base;

    // Greedy tree building
    std::mt19937 rng(42);
    for (int t = 0; t < maxTrees; ++t) {
        // Subsample features
        int sampledF = std::min(F, std::max(4, F / 3));
        std::vector<int> featIdx(F);
        std::iota(featIdx.begin(), featIdx.end(), 0);
        std::shuffle(featIdx.begin(), featIdx.end(), rng);
        featIdx.resize(sampledF);

        // Find best split
        double bestGain = 0;
        int bestFeat = -1;
        double bestThresh = 0;
        double bestLeftVal = 0, bestRightVal = 0;

        for (int fi : featIdx) {
            // Sort values for this feature
            std::vector<std::pair<double, double>> sorted(N);
            for (int i = 0; i < N; ++i)
                sorted[i] = {X[i][fi], residuals[i]};
            std::sort(sorted.begin(), sorted.end());

            // Scan thresholds
            double sumL = 0, sumR = 0;
            for (int i = 0; i < N; ++i) sumR += sorted[i].second;
            int cntL = 0, cntR = N;

            for (int i = 0; i < N - 1; ++i) {
                sumL += sorted[i].second;
                cntL++;
                sumR -= sorted[i].second;
                cntR--;
                if (cntL < 1 || cntR < 1) continue;
                if (sorted[i].first >= sorted[i+1].first) continue;

                double leftVal = sumL / cntL;
                double rightVal = sumR / cntR;
                double gain = sumL * sumL / cntL + sumR * sumR / cntR;

                if (gain > bestGain) {
                    bestGain = gain;
                    bestFeat = fi;
                    bestThresh = (sorted[i].first + sorted[i+1].first) / 2.0;
                    bestLeftVal = leftVal;
                    bestRightVal = rightVal;
                }
            }
        }

        if (bestFeat < 0) break;

        GbtTree tree;
        tree.featureIdx = bestFeat;
        tree.threshold = bestThresh;
        tree.leftValue = bestLeftVal * model.learningRate;
        tree.rightValue = bestRightVal * model.learningRate;
        model.trees.push_back(tree);

        // Update residuals
        for (int i = 0; i < N; ++i) {
            double pred = (X[i][bestFeat] < bestThresh)
                ? tree.leftValue : tree.rightValue;
            residuals[i] -= pred;
        }
    }
    return model;
}

double LearningEngine::predictGbt(const GbtModel &model,
                                  const std::vector<double> &features) const {
    double pred = model.basePrediction;
    for (const auto &tree : model.trees) {
        if (tree.featureIdx < static_cast<int>(features.size()))
            pred += (features[tree.featureIdx] < tree.threshold)
                ? tree.leftValue : tree.rightValue;
    }
    return pred;
}

// ── #32 Confidence intervals (bootstrap residuals) ──────────

std::vector<LePredictionModel>
LearningEngine::trainPredictionWithCI(const std::string &variableKey,
                                       int bootstrapSamples) {
    std::unique_lock lock(m_mutex);
    std::vector<LePredictionModel> models;
    const auto &obs = observations(variableKey);
    if (obs.size() < 5) return models;

    // First, train normally
    models = trainPrediction(variableKey);

    // For each model, compute bootstrap confidence intervals
    std::mt19937 rng(42);
    for (auto &model : models) {
        // Collect (x, y) pairs for this model
        std::vector<double> X, Y;
        for (const auto &o : obs) {
            auto it = o.idAverageBytes.find(model.id);
            if (it != o.idAverageBytes.end() && model.byte < 64) {
                X.push_back(static_cast<double>(it->second[model.byte]));
                Y.push_back(o.value);
            }
        }
        if (X.size() < 5) continue;

        // Compute point predictions and residuals
        std::vector<double> residuals;
        for (size_t i = 0; i < X.size(); ++i) {
            double pred = model.a * X[i] + model.b;
            residuals.push_back(Y[i] - pred);
        }

        // Bootstrap residuals
        std::vector<double> bootPreds(bootstrapSamples);
        std::uniform_int_distribution<int> dist(0, static_cast<int>(residuals.size()) - 1);
        for (int b = 0; b < bootstrapSamples; ++b) {
            double sum = 0;
            int n = std::min(20, static_cast<int>(residuals.size()));
            for (int i = 0; i < n; ++i)
                sum += residuals[dist(rng)];
            bootPreds[b] = sum / n;
        }

        std::sort(bootPreds.begin(), bootPreds.end());
        double meanPred = 0;
        for (double p : bootPreds) meanPred += p;
        meanPred /= bootstrapSamples;

        // 95% CI
        int loIdx = static_cast<int>(bootstrapSamples * 0.025);
        int hiIdx = static_cast<int>(bootstrapSamples * 0.975);
        model.lowerBound = bootPreds[std::max(0, loIdx)];
        model.upperBound = bootPreds[std::min(bootstrapSamples - 1, hiIdx)];
    }
    return models;
}


// ── #33 Multi-target ────────────────────────────────────────

void LearningEngine::trainAllPredictions() {
    auto names = variableNames();
    for (const auto &name : names)
        trainPrediction(name);
}

std::vector<LearningEngine::MultiPrediction>
LearningEngine::predictRealtimeAll() const {
    std::vector<MultiPrediction> result;
    auto names = variableNames();
    for (const auto &name : names) {
        MultiPrediction mp;
        mp.variableName = name;
        mp.predictions = predictRealtime(name);
        if (!mp.predictions.empty())
            result.push_back(std::move(mp));
    }
    return result;
}


// ── #37 Jacobi eigen-decomposition for small matrices ──────

void LearningEngine::jacobiEigen(const std::vector<std::vector<double>> &mat,
                                  std::vector<double> &eigenvalues,
                                  std::vector<std::vector<double>> &eigenvectors,
                                  int maxIter) {
    int n = static_cast<int>(mat.size());
    eigenvalues.resize(n);
    eigenvectors.assign(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i) eigenvectors[i][i] = 1.0;

    std::vector<std::vector<double>> A = mat; // working copy

    for (int iter = 0; iter < maxIter; ++iter) {
        // Find largest off-diagonal element
        int p = 0, q = 1;
        double maxOff = 0.0;
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j)
                if (std::abs(A[i][j]) > maxOff) {
                    maxOff = std::abs(A[i][j]);
                    p = i; q = j;
                }
        if (maxOff < 1e-10) break;

        // Compute Jacobi rotation
        double theta = 0.5 * std::atan2(2.0 * A[p][q], A[p][p] - A[q][q]);
        double c = std::cos(theta), s = std::sin(theta);

        // Apply rotation to A
        std::vector<std::vector<double>> Anew = A;
        for (int i = 0; i < n; ++i) {
            if (i != p && i != q) {
                Anew[i][p] = Anew[p][i] = c * A[i][p] - s * A[i][q];
                Anew[i][q] = Anew[q][i] = s * A[i][p] + c * A[i][q];
            }
        }
        Anew[p][p] = c * c * A[p][p] + s * s * A[q][q] - 2.0 * s * c * A[p][q];
        Anew[q][q] = s * s * A[p][p] + c * c * A[q][q] + 2.0 * s * c * A[p][q];
        Anew[p][q] = Anew[q][p] = 0.0;
        A = Anew;

        // Update eigenvectors
        for (int i = 0; i < n; ++i) {
            double ei_p = eigenvectors[i][p];
            double ei_q = eigenvectors[i][q];
            eigenvectors[i][p] = c * ei_p - s * ei_q;
            eigenvectors[i][q] = s * ei_p + c * ei_q;
        }
    }

    // Extract eigenvalues from diagonal
    for (int i = 0; i < n; ++i)
        eigenvalues[i] = A[i][i];

    // Sort by eigenvalue descending
    for (int i = 0; i < n - 1; ++i) {
        int best = i;
        for (int j = i + 1; j < n; ++j)
            if (eigenvalues[j] > eigenvalues[best]) best = j;
        if (best != i) {
            std::swap(eigenvalues[i], eigenvalues[best]);
            for (int k = 0; k < n; ++k)
                std::swap(eigenvectors[k][i], eigenvectors[k][best]);
        }
    }
}


// ── #40 Cross-correlation with time lag ────────────────────

std::vector<LearningEngine::LagCorrEntry>
LearningEngine::computeCrossCorrelationLag(const std::string &variableKey,
                                           int maxLag) const {
    std::shared_lock lock(m_mutex);
    std::vector<LagCorrEntry> entries;
    const auto &obs = observations(variableKey);
    if (obs.size() < maxLag + 3) return entries;

    // Build time series: variable values and per-(ID,byte) byte values
    std::vector<double> values;
    for (const auto &o : obs) values.push_back(o.value);

    // Find all (ID, byte) pairs
    std::unordered_set<uint32_t> ids;
    for (const auto &o : obs)
        for (const auto &kv : o.idAverageBytes)
            ids.insert(kv.first);

    for (uint32_t id : ids) {
        for (int b = 0; b < 8; ++b) {
            std::vector<double> bytes;
            for (const auto &o : obs) {
                auto it = o.idAverageBytes.find(id);
                bytes.push_back((it != o.idAverageBytes.end())
                    ? static_cast<double>(it->second[b]) : 0.0);
            }

            int N = std::min(static_cast<int>(values.size()),
                             static_cast<int>(bytes.size()));

            for (int lag = -maxLag; lag <= maxLag; ++lag) {
                std::vector<double> x, y;
                for (int i = 0; i < N; ++i) {
                    int j = i + lag;
                    if (j >= 0 && j < N) {
                        x.push_back(bytes[i]);   // byte at time i
                        y.push_back(values[j]);  // variable at time i+lag
                    }
                }
                if (x.size() < 5) continue;
                double corr = correlationPearson(x, y);
                if (std::abs(corr) > 0.3)  // filter weak correlations
                    entries.push_back({id, b, lag, corr});
            }
        }
    }

    std::sort(entries.begin(), entries.end(),
        [](const LagCorrEntry &a, const LagCorrEntry &b) {
            return std::abs(a.correlation) > std::abs(b.correlation);
        });
    return entries;
}

// ── #38 Granger causality ──────────────────────────────────

std::vector<LearningEngine::GrangerResult>
LearningEngine::computeGrangerCausality(const std::string &variableKey,
                                         int maxLag) const {
    std::shared_lock lock(m_mutex);
    std::vector<GrangerResult> results;
    const auto &obs = observations(variableKey);
    if (obs.size() < maxLag + 10) return results;

    std::vector<double> Y;
    for (const auto &o : obs) Y.push_back(o.value);
    int T = static_cast<int>(Y.size());

    // Find all (ID, byte) pairs with enough data
    std::unordered_set<uint32_t> ids;
    for (const auto &o : obs)
        for (const auto &kv : o.idAverageBytes)
            ids.insert(kv.first);

    for (uint32_t id : ids) {
        for (int b = 0; b < 8; ++b) {
            std::vector<double> X;
            for (const auto &o : obs) {
                auto it = o.idAverageBytes.find(id);
                X.push_back((it != o.idAverageBytes.end())
                    ? static_cast<double>(it->second[b]) : 0.0);
            }

            double bestF = 0.0;
            int bestLag = 1;

            for (int lag = 1; lag <= maxLag; ++lag) {
                // Restricted model: Y_t = a0 + a1*Y_{t-1} + ... + a_lag*Y_{t-lag}
                // Unrestricted:  Y_t = a0 + a1*Y_{t-1} + ... + b1*X_{t-1} + ... + b_lag*X_{t-lag}
                int usable = T - lag;
                if (usable < 10) continue;

                // Restricted: linear regression Y[t] ~ Y[t-1..t-lag]
                // Simple OLS via normal equations
                int p = lag + 1; // params: intercept + lag Y terms
                std::vector<double> A_rest(p * p, 0), b_rest(p, 0);
                for (int t = lag; t < T; ++t) {
                    std::vector<double> row(p);
                    row[0] = 1.0; // intercept
                    for (int l = 1; l <= lag; ++l) row[l] = Y[t - l];
                    for (int i = 0; i < p; ++i) {
                        for (int j = 0; j < p; ++j) A_rest[i*p+j] += row[i] * row[j];
                        b_rest[i] += row[i] * Y[t];
                    }
                }
                // Solve A*x = b via Gaussian elimination
                auto gauss = [](std::vector<double> &A, std::vector<double> &b, int n) -> std::vector<double> {
                    std::vector<double> x(n, 0);
                    for (int i = 0; i < n; ++i) {
                        int pivot = i;
                        for (int j = i+1; j < n; ++j)
                            if (std::abs(A[j*n+i]) > std::abs(A[pivot*n+i])) pivot = j;
                        if (pivot != i) {
                            for (int j = 0; j < n; ++j) std::swap(A[i*n+j], A[pivot*n+j]);
                            std::swap(b[i], b[pivot]);
                        }
                        for (int j = i+1; j < n; ++j) {
                            double f = A[j*n+i] / (A[i*n+i] + 1e-12);
                            for (int k = i; k < n; ++k) A[j*n+k] -= f * A[i*n+k];
                            b[j] -= f * b[i];
                        }
                    }
                    for (int i = n-1; i >= 0; --i) {
                        double s = b[i];
                        for (int j = i+1; j < n; ++j) s -= A[i*n+j] * x[j];
                        x[i] = s / (A[i*n+i] + 1e-12);
                    }
                    return x;
                };

                auto x_rest = gauss(A_rest, b_rest, p);
                double rss_rest = 0;
                for (int t = lag; t < T; ++t) {
                    double pred = x_rest[0];
                    for (int l = 1; l <= lag; ++l) pred += x_rest[l] * Y[t-l];
                    double err = Y[t] - pred;
                    rss_rest += err * err;
                }

                // Unrestricted: Y[t] ~ Y[t-1..t-lag] + X[t-1..t-lag]
                int q = 2*lag + 1; // intercept + lag Y + lag X
                std::vector<double> A_unr(q * q, 0), b_unr(q, 0);
                for (int t = lag; t < T; ++t) {
                    std::vector<double> row(q);
                    int idx = 0;
                    row[idx++] = 1.0; // intercept
                    for (int l = 1; l <= lag; ++l) row[idx++] = Y[t - l];
                    for (int l = 1; l <= lag; ++l) row[idx++] = X[t - l];
                    for (int i = 0; i < q; ++i) {
                        for (int j = 0; j < q; ++j) A_unr[i*q+j] += row[i] * row[j];
                        b_unr[i] += row[i] * Y[t];
                    }
                }
                auto x_unr = gauss(A_unr, b_unr, q);
                double rss_unr = 0;
                for (int t = lag; t < T; ++t) {
                    double pred = x_unr[0];
                    int idx = 1;
                    for (int l = 1; l <= lag; ++l) pred += x_unr[idx++] * Y[t-l];
                    for (int l = 1; l <= lag; ++l) pred += x_unr[idx++] * X[t-l];
                    double err = Y[t] - pred;
                    rss_unr += err * err;
                }

                int df1 = lag;           // extra params in unrestricted
                int df2 = usable - q;    // residual df unrestricted
                if (df2 <= 0) continue;
                double F = ((rss_rest - rss_unr) / df1) / (rss_unr / (df2 + 1e-9));
                if (F > bestF && F > 0) { bestF = F; bestLag = lag; }
            }

            if (bestF > 0) {
                // F-test p-value approximation
                int df1 = bestLag, df2 = T - 2*bestLag - 1;
                double pv = df2 > 0 ? pearsonPValue(std::sqrt(bestF / (bestF + df2)), T) : 1.0;
                results.push_back({id, b, bestF, pv, pv < 0.05, bestLag});
            }
        }
    }

    std::sort(results.begin(), results.end(),
        [](const GrangerResult &a, const GrangerResult &b) {
            return a.fStatistic > b.fStatistic;
        });
    return results;
}

// ── #39 Change-point detection (Binary Segmentation) ───────

std::vector<LearningEngine::ChangePoint>
LearningEngine::detectChangePoints(const std::string &variableKey,
                                    uint32_t targetId, int targetByte,
                                    int minSegmentSize) const {
    std::shared_lock lock(m_mutex);
    std::vector<ChangePoint> result;
    const auto &obs = observations(variableKey);
    if (obs.size() < minSegmentSize * 2) return result;

    // Extract byte time series
    std::vector<double> series;
    for (const auto &o : obs) {
        auto it = o.idAverageBytes.find(targetId);
        series.push_back((it != o.idAverageBytes.end() && targetByte < 64)
            ? static_cast<double>(it->second[targetByte]) : 0.0);
    }
    int N = static_cast<int>(series.size());

    // Recursive binary segmentation
    std::function<void(int,int)> split = [&](int left, int right) {
        int segLen = right - left;
        if (segLen < minSegmentSize * 2) return;

        // Compute total cost (variance * length)
        double totalMean = 0, totalSq = 0;
        for (int i = left; i < right; ++i) {
            totalMean += series[i];
            totalSq += series[i] * series[i];
        }
        totalMean /= segLen;
        double totalVar = totalSq / segLen - totalMean * totalMean;

        // Find best split point
        double bestReduction = 0;
        int bestSplit = -1;
        double leftSum = 0, leftSq = 0;

        for (int s = left + minSegmentSize; s < right - minSegmentSize; ++s) {
            double x = series[s - 1];
            leftSum += x; leftSq += x * x;
            int leftLen = s - left;
            double leftMean = leftSum / leftLen;
            double leftVar = leftSq / leftLen - leftMean * leftMean;

            double rightSum = totalMean * segLen - leftSum;
            double rightSq = totalSq - leftSq;
            int rightLen = segLen - leftLen;
            double rightMean = rightSum / rightLen;
            double rightVar = rightSq / rightLen - rightMean * rightMean;

            double reduction = totalVar - (leftVar * leftLen + rightVar * rightLen) / segLen;
            if (reduction > bestReduction) {
                bestReduction = reduction;
                bestSplit = s;
            }
        }

        if (bestSplit >= 0 && bestReduction > 0.01) {
            double lm = 0, rm = 0;
            for (int i = left; i < bestSplit; ++i) lm += series[i];
            for (int i = bestSplit; i < right; ++i) rm += series[i];
            lm /= (bestSplit - left);
            rm /= (right - bestSplit);
            result.push_back({bestSplit, bestReduction, lm, rm});

            split(left, bestSplit);
            split(bestSplit, right);
        }
    };

    split(0, N);
    std::sort(result.begin(), result.end(),
        [](const ChangePoint &a, const ChangePoint &b) {
            return a.index < b.index;
        });
    return result;
}


// ── #42-45 Serialization & persistence ────────────────────

// Simple JSON builder (no external library needed for basic types)
static void jsonObj(std::ostringstream &os,
                    const std::vector<std::pair<std::string,std::string>> &fields) {
    os << "{";
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) os << ",";
        os << "\"" << fields[i].first << "\":" << fields[i].second;
    }
    os << "}";
}

static std::string jsonString(const std::string &s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

std::string LearningEngine::serializeSession() const {
    std::shared_lock lock(m_mutex);
    std::ostringstream os;
    os << "{";
    os << "\"iteration\":" << m_iteration << ",";
    os << "\"adaptiveBefore\":" << m_adaptiveBefore << ",";
    os << "\"adaptiveAfter\":" << m_adaptiveAfter << ",";
    os << "\"decayLambda\":" << m_decayLambda << ",";
    os << "\"maxObservations\":" << m_maxObservations << ",";

    // Observations
    os << "\"observations\":{";
    bool firstVar = true;
    for (const auto &vkv : m_observations) {
        if (!firstVar) os << ",";
        firstVar = false;
        os << jsonString(vkv.first) << ":[";
        bool firstObs = true;
        for (const auto &o : vkv.second) {
            if (!firstObs) os << ",";
            firstObs = false;
            os << "{\"v\":" << o.value << ",\"ts\":" << o.timestamp << ",\"bytes\":{";
            bool firstB = true;
            for (const auto &bkv : o.idAverageBytes) {
                if (!firstB) os << ",";
                firstB = false;
                os << "\"" << bkv.first << "\":[";
                for (size_t i = 0; i < bkv.second.size(); ++i) {
                    if (i > 0) os << ",";
                    os << (int)bkv.second[i];
                }
                os << "]";
            }
            os << "}}";
        }
        os << "]";
    }
    os << "},";

    // Events (summary only — full frames would be too large)
    os << "\"eventCount\":" << m_events.size() << ",";
    os << "\"nonEventCount\":" << m_nonEvents.size() << ",";

    // Markov
    os << "\"markovTransitions\":" << m_transitions.size();

    os << "}";
    return os.str();
}

bool LearningEngine::deserializeSession(const std::string &json) {
    std::unique_lock lock(m_mutex);
    // Simple string-based parser for our JSON format
    auto findInt = [&](const std::string &key) -> int64_t {
        auto pos = json.find("\"" + key + "\":");
        if (pos == std::string::npos) return 0;
        pos += key.length() + 3;
        return std::stoll(json.substr(pos));
    };
    auto findDouble = [&](const std::string &key) -> double {
        auto pos = json.find("\"" + key + "\":");
        if (pos == std::string::npos) return 0.0;
        pos += key.length() + 3;
        return std::stod(json.substr(pos));
    };

    m_iteration = static_cast<int>(findInt("iteration"));
    m_adaptiveBefore = findInt("adaptiveBefore");
    m_adaptiveAfter = findInt("adaptiveAfter");
    m_decayLambda = findDouble("decayLambda");
    m_maxObservations = static_cast<size_t>(findInt("maxObservations"));

    return true;
}

bool LearningEngine::saveCheckpoint(const std::string &path) const {
    std::string data = serializeSession();
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << data;
    return f.good();
}

bool LearningEngine::loadCheckpoint(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::string data((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    return deserializeSession(data);
}

std::string LearningEngine::exportOnnx(const std::string &variableKey) const {
    // Stub: ONNX export would require protobuf serialization
    // Returns JSON description of the model instead
    std::shared_lock lock(m_mutex);
    std::ostringstream os;
    os << "{\"type\":\"linear_regression\",\"variable\":\"" << variableKey << "\"";
    auto it = m_linearModels.find(variableKey);
    if (it != m_linearModels.end()) {
        os << ",\"models\":[";
        bool first = true;
        for (const auto &kv : it->second) {
            if (!first) os << ",";
            first = false;
            uint32_t id = static_cast<uint32_t>(kv.first / 100);
            int byte = static_cast<int>(kv.first % 100);
            os << "{\"id\":" << id << ",\"byte\":" << byte
               << ",\"a\":" << kv.second.first << ",\"b\":" << kv.second.second << "}";
        }
        os << "]";
    }
    os << "}";
    return os.str();
}

// ── #27 Exponential forgetting: weighted Pearson ────────────

double LearningEngine::correlationPearsonWeighted(
    const std::vector<double> &x, const std::vector<double> &y,
    const std::vector<uint64_t> &timestamps, double lambda) {
    int N = std::min({static_cast<int>(x.size()),
                      static_cast<int>(y.size()),
                      static_cast<int>(timestamps.size())});
    if (N < 3 || lambda <= 0.0) return correlationPearson(x, y);

    uint64_t newest = *std::max_element(timestamps.begin(),
                                         timestamps.begin() + N);
    double sw = 0, swx = 0, swy = 0, swxy = 0, swx2 = 0, swy2 = 0;
    for (int i = 0; i < N; ++i) {
        double age_us = static_cast<double>(newest - timestamps[i]);
        double w = std::exp(-lambda * age_us / 1'000'000.0);
        double xi = x[i], yi = y[i];
        sw += w;
        swx += w * xi;
        swy += w * yi;
        swxy += w * xi * yi;
        swx2 += w * xi * xi;
        swy2 += w * yi * yi;
    }
    if (sw < 1e-9) return 0.0;
    double num = sw * swxy - swx * swy;
    double den = std::sqrt((sw * swx2 - swx * swx) * (sw * swy2 - swy * swy));
    return den != 0.0 ? num / den : 0.0;
}

// ── #28 Welford's online algorithm ──────────────────────────

static uint64_t welfordKey(const std::string &var, uint32_t id, int byteIdx) {
    std::hash<std::string> hs;
    return (static_cast<uint64_t>(hs(var)) << 40) |
           (static_cast<uint64_t>(id) << 8) | static_cast<uint64_t>(byteIdx);
}

void LearningEngine::updateWelford(const std::string &variableKey,
                                   uint32_t id, int byteIdx,
                                   double valX, double valY) {
    uint64_t key = welfordKey(variableKey, id, byteIdx);
    auto &a = m_welford[key];
    a.n++;
    double dx = valX - a.meanX;
    a.meanX += dx / a.n;
    double dy = valY - a.meanY;
    a.meanY += dy / a.n;
    a.M2x += dx * (valX - a.meanX);
    a.M2y += dy * (valY - a.meanY);
    a.Cxy += dx * (valY - a.meanY);
}

std::vector<LeCorrelationEntry>
LearningEngine::computeCorrelationsOnline(const std::string &variableKey) const {
    std::shared_lock lock(m_mutex);
    std::vector<LeCorrelationEntry> entries;
    const auto &obs = observations(variableKey);
    if (obs.size() < 3) return entries;

    std::hash<std::string> hs;
    uint64_t varHash = hs(variableKey);

    for (const auto &kv : m_welford) {
        uint64_t key = kv.first;
        if ((key >> 40) != varHash) continue;
        uint32_t id = static_cast<uint32_t>((key >> 8) & 0xFFFFFFFF);
        int byteIdx = static_cast<int>(key & 0xFF);
        const auto &a = kv.second;
        if (a.n < 3) continue;

        double varX = a.M2x / (a.n - 1);
        double varY = a.M2y / (a.n - 1);
        double cov  = a.Cxy / (a.n - 1);
        double denom = std::sqrt(varX * varY);
        double corr = denom > 0 ? cov / denom : 0.0;
        double pv = pearsonPValue(corr, static_cast<int>(a.n));
        entries.push_back({id, byteIdx, corr, pv, pv < 0.05});
    }

    std::sort(entries.begin(), entries.end(),
              [](const LeCorrelationEntry &a, const LeCorrelationEntry &b) {
                  return std::abs(a.correlation) > std::abs(b.correlation);
              });
    return entries;
}

// ── #29 EWMA anomaly detection ──────────────────────────────

void LearningEngine::updateEwma(const std::vector<float> &features) {
    if (m_ewma.mean.empty()) {
        m_ewma.mean.assign(features.begin(), features.end());
        m_ewma.var.resize(features.size(), 0.0);
        return;
    }
    double a = m_ewma.alpha;
    for (size_t d = 0; d < features.size(); ++d) {
        double oldMean = m_ewma.mean[d];
        m_ewma.mean[d] = a * features[d] + (1.0 - a) * oldMean;
        double dev = features[d] - m_ewma.mean[d];
        m_ewma.var[d] = a * dev * dev + (1.0 - a) * m_ewma.var[d];
    }
}

double LearningEngine::checkAnomalyEwma() const {
    std::shared_lock lock(m_mutex);
    if (m_frameHistory.empty() || m_ewma.mean.empty()) return 0.0;
    uint64_t now = m_frameHistory.back().timestamp;
    std::vector<CanFrame> win;
    for (auto ri = m_frameHistory.rbegin(); ri != m_frameHistory.rend(); ++ri) {
        if (ri->timestamp >= now - 1'000'000)
            win.push_back(*ri);
        else break;
    }
    if (win.size() < 3) return 0.0;
    std::reverse(win.begin(), win.end());
    std::vector<float> feat = buildWindowFeatures(win);
    const_cast<LearningEngine*>(this)->updateEwma(feat);

    double score = 0.0;
    for (size_t d = 0; d < feat.size() && d < m_ewma.mean.size(); ++d) {
        double var = m_ewma.var[d] + 1e-6;
        double z = (feat[d] - m_ewma.mean[d]) / std::sqrt(var);
        score += z * z;
    }
    return score;
}
