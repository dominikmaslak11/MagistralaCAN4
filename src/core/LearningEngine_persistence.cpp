#include "LearningEngine.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <sstream>
// ── #42-45 Serialization & persistence ────────────────────

namespace {

// Simple JSON builder (no external library needed for basic types)
void jsonObj(std::ostringstream &os,
             const std::vector<std::pair<std::string,std::string>> &fields) {
    os << "{";
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) os << ",";
        os << "\"" << fields[i].first << "\":" << fields[i].second;
    }
    os << "}";
}

std::string jsonString(const std::string &s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

} // namespace

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
                    os << static_cast<int>(bkv.second[i]);
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

namespace {

uint64_t welfordKey(const std::string &var, uint32_t id, int byteIdx) {
    std::hash<std::string> hs;
    return (static_cast<uint64_t>(hs(var)) << 40) |
           (static_cast<uint64_t>(id) << 8) | static_cast<uint64_t>(byteIdx);
}

} // namespace

void LearningEngine::updateWelford(const std::string &variableKey,
                                   uint32_t id, int byteIdx,
                                   double valX, double valY) {
    uint64_t key = welfordKey(variableKey, id, byteIdx);
    auto &a = m_welford[key];
    a.n++;
    double dx = valX - a.meanX;
    a.meanX += dx / static_cast<double>(a.n);
    double dy = valY - a.meanY;
    a.meanY += dy / static_cast<double>(a.n);
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
    uint64_t varHash = hs(variableKey) & 0xFFFFFFULL;  // match welfordKey truncation
    auto aiBytes    = m_autoIncrFilterEnabled ? detectAutoIncrementBytes() : std::unordered_set<uint64_t>{};
    auto noiseBytes = m_noiseFilterEnabled    ? detectCyclicNoiseBytes()   : std::unordered_set<uint64_t>{};

    for (const auto &kv : m_welford) {
        uint64_t key = kv.first;
        if ((key >> 40) != varHash) continue;
        uint32_t id = static_cast<uint32_t>((key >> 8) & 0xFFFFFFFF);
        int byteIdx = static_cast<int>(key & 0xFF);
        const auto &a = kv.second;
        if (a.n < 3) continue;
        uint64_t fkey = (static_cast<uint64_t>(id) << 8) | byteIdx;
        if (aiBytes.count(fkey))    continue;
        if (noiseBytes.count(fkey)) continue;

        double varX = a.M2x / static_cast<double>(a.n - 1);
        double varY = a.M2y / static_cast<double>(a.n - 1);
        double cov  = a.Cxy / static_cast<double>(a.n - 1);
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
