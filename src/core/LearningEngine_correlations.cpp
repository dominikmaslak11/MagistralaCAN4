#include "LearningEngine.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <mutex>
#include <numeric>
#include <random>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
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

    auto aiBytes    = m_autoIncrFilterEnabled ? detectAutoIncrementBytes()  : std::unordered_set<uint64_t>{};
    auto noiseBytes = m_noiseFilterEnabled    ? detectCyclicNoiseBytes()    : std::unordered_set<uint64_t>{};

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
            uint64_t fkey = (static_cast<uint64_t>(id) << 8) | b;
            if (aiBytes.count(fkey))    continue;
            if (noiseBytes.count(fkey)) continue;
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
                          static_cast<float>(kv.second) / static_cast<float>(m_events.size())});
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

    auto aiBytes    = m_autoIncrFilterEnabled ? detectAutoIncrementBytes() : std::unordered_set<uint64_t>{};
    auto noiseBytes = m_noiseFilterEnabled    ? detectCyclicNoiseBytes()   : std::unordered_set<uint64_t>{};
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
                if (v1 < 1e-9) continue;
                if (aiBytes.count(ki | b1))    continue;
                if (noiseBytes.count(ki | b1)) continue;
                for (int b2 = 0; b2 < 8; ++b2) {
                    double v2 = varCache[kj | b2];
                    if (v2 < 1e-9) continue;
                    if (aiBytes.count(kj | b2))    continue;
                    if (noiseBytes.count(kj | b2)) continue;
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

// ── Prediction (linear regression) ──────────────────────────

std::vector<LePredictionModel>
LearningEngine::trainPrediction(const std::string &variableKey) {
    std::unique_lock lock(m_mutex);
    std::vector<LePredictionModel> models;
    const auto &obs = observations(variableKey);
    if (obs.size() < 3) return models;

    auto aiBytes    = m_autoIncrFilterEnabled ? detectAutoIncrementBytes() : std::unordered_set<uint64_t>{};
    auto noiseBytes = m_noiseFilterEnabled    ? detectCyclicNoiseBytes()   : std::unordered_set<uint64_t>{};

    std::unordered_set<uint64_t> allPairs;
    for (const auto &o : obs)
        for (const auto &kv : o.idAverageBytes)
            for (int b = 0; b < 64; ++b)
                allPairs.insert(static_cast<uint64_t>(kv.first) * 100 + b);

    auto &modelsForVar = m_linearModels[variableKey];

    for (uint64_t key : allPairs) {
        uint32_t id = static_cast<uint32_t>(key / 100);
        int byteIdx = static_cast<int>(key % 100);
        uint64_t fkey = (static_cast<uint64_t>(id) << 8) | byteIdx;
        if (aiBytes.count(fkey))    continue;
        if (noiseBytes.count(fkey)) continue;
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
        int byteIdx = static_cast<int>(kv.first % 100);
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
    int64_t start = static_cast<int64_t>(m_frameHistory.front().timestamp);
    int64_t end = static_cast<int64_t>(m_frameHistory.back().timestamp);
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
        m_normalMean[d] = static_cast<float>(sum / static_cast<double>(feats.size()));
        m_normalStd[d] = static_cast<float>(
            std::sqrt(sq / static_cast<double>(feats.size()) - m_normalMean[d] * m_normalMean[d]));
        if (m_normalStd[d] < 1e-6f) m_normalStd[d] = 1.0f;
    }
}

double LearningEngine::checkAnomaly(double  /*threshold*/) const {
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

    auto aiBytes    = m_autoIncrFilterEnabled ? detectAutoIncrementBytes() : std::unordered_set<uint64_t>{};
    auto noiseBytes = m_noiseFilterEnabled    ? detectCyclicNoiseBytes()   : std::unordered_set<uint64_t>{};

    for (uint32_t id : commonIds) {
        for (int b = 0; b < 64; ++b) {
            uint64_t fkey = (static_cast<uint64_t>(id) << 8) | b;
            if (aiBytes.count(fkey))    continue;
            if (noiseBytes.count(fkey)) continue;
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

    auto aiBytesMic    = m_autoIncrFilterEnabled ? detectAutoIncrementBytes() : std::unordered_set<uint64_t>{};
    auto noiseBytesMic = m_noiseFilterEnabled    ? detectCyclicNoiseBytes()   : std::unordered_set<uint64_t>{};

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
            uint64_t fkey = (static_cast<uint64_t>(id) << 8) | b;
            if (aiBytesMic.count(fkey))    continue;
            if (noiseBytesMic.count(fkey)) continue;
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


