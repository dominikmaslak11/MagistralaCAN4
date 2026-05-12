#include "LearningEngine.h"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <numeric>
#include <random>
#include <shared_mutex>
#include <sstream>
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


// ── Regularized incomplete beta (power series + symmetry) ──

static double regIncompleteBeta(double a, double b, double x) {
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    // Use symmetry: I_x(a,b) = 1 - I_{1-x}(b,a) when x is large
    if (x > 0.5) return 1.0 - regIncompleteBeta(b, a, 1.0 - x);
    // Power series: I_x(a,b) = [x^a (1-x)^b / (a B(a,b))] * sum_{n=0} d_n
    // where d_0 = 1, d_{n+1} = d_n * (a+b+n)/(a+1+n) * x
    double lnBeta = std::lgamma(a + b) - std::lgamma(a) - std::lgamma(b);
    double front = std::exp(lnBeta + a * std::log(x) + b * std::log(1.0 - x));
    double sum = 1.0;
    double term = 1.0;
    for (int n = 0; n < 500; ++n) {
        term *= (a + b + n) / (a + 1.0 + n) * x;
        double prev = sum;
        sum += term;
        if (sum == prev) break;  // converged
    }
    return front * sum / a;
}

// ── Statistical helpers ─────────────────────────────────────

double LearningEngine::pearsonPValue(double r, int n) {
    if (n <= 2) return 1.0;
    if (std::abs(r) >= 1.0) return 0.0;
    double t = std::abs(r) * std::sqrt((n - 2) / (1.0 - r * r));
    double df = n - 2.0;
    double x = df / (df + t * t);
    // Two-tailed p-value: p = I_x(df/2, 0.5)
    return regIncompleteBeta(df / 2.0, 0.5, x);
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



