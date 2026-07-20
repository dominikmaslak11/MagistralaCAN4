#include "LearningEngine.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <mutex>
#include <shared_mutex>
#include <iomanip>
#include <sstream>
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
    int pointIdx{};
    int splitDim{};
    float splitVal{};
    KdNode *left = nullptr;
    KdNode *right = nullptr;
    ~KdNode() { delete left; delete right; }
};

namespace {

KdNode* buildKdTree(const std::vector<std::vector<float>> &data,
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

void kdRangeQuery(KdNode *node, const std::vector<std::vector<float>> &data,
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

} // namespace

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

namespace {

std::vector<std::vector<CanFrame>>
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

} // namespace

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
            srt.emplace_back(kv.first, kv.second);
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
            srt.emplace_back(kv.first, kv.second);
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
        result.projected.emplace_back(x, y);
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

// ── #41 t-SNE ────────────────────────────────────────────────

LearningEngine::TsneResult
LearningEngine::runTsne(int perplexity, int maxIter, double /*theta*/) const {
    TsneResult result;
    std::shared_lock lock(m_mutex);

    auto windows = splitWindows(m_frameHistory, 500000);
    int N = static_cast<int>(windows.size());
    if (N < perplexity + 1) {
        result.status = "Za malo okien (wymagane >" + std::to_string(perplexity) + ")";
        return result;
    }
    N = std::min(N, 300);
    const int DIM = 4;

    // Build + standardize feature matrix
    std::vector<std::vector<double>> X(N, std::vector<double>(DIM, 0.0));
    for (int i = 0; i < N; ++i) {
        auto f = buildWindowFeatures(windows[i]);
        for (int d = 0; d < DIM; ++d) X[i][d] = f[d];
    }
    for (int d = 0; d < DIM; ++d) {
        double mu = 0;
        for (int i = 0; i < N; ++i) mu += X[i][d];
        mu /= N;
        double sigma = 0;
        for (int i = 0; i < N; ++i) sigma += (X[i][d] - mu) * (X[i][d] - mu);
        sigma = std::sqrt(sigma / N + 1e-8);
        for (int i = 0; i < N; ++i) X[i][d] = (X[i][d] - mu) / sigma;
    }

    // Pairwise squared distances in high-dim space
    std::vector<std::vector<double>> D2(N, std::vector<double>(N, 0.0));
    for (int i = 0; i < N; ++i)
        for (int j = i + 1; j < N; ++j) {
            double d2 = 0;
            for (int d = 0; d < DIM; ++d) { double diff = X[i][d]-X[j][d]; d2 += diff*diff; }
            D2[i][j] = D2[j][i] = d2;
        }

    // Compute symmetric P via binary search for σ_i (target: H(P_i) = ln(perplexity))
    const double logPerp = std::log(static_cast<double>(perplexity));
    std::vector<std::vector<double>> P(N, std::vector<double>(N, 0.0));
    for (int i = 0; i < N; ++i) {
        double betaMin = -1e15, betaMax = 1e15, beta = 1.0;
        for (int s = 0; s < 50; ++s) {
            double sumP = 0.0;
            for (int j = 0; j < N; ++j) {
                P[i][j] = (i == j) ? 0.0 : std::exp(-beta * D2[i][j]);
                sumP += P[i][j];
            }
            if (sumP < 1e-14) sumP = 1e-14;
            double H = 0.0;
            for (int j = 0; j < N; ++j) {
                if (i == j) continue;
                double pij = P[i][j] / sumP;
                if (pij > 1e-14) H -= pij * std::log(pij);
            }
            double diff = H - logPerp;
            if (std::abs(diff) < 1e-5) break;
            if (diff > 0) { betaMin = beta; beta = (betaMax < 1e14) ? (beta+betaMax)*0.5 : beta*2.0; }
            else          { betaMax = beta; beta = (betaMin > -1e14) ? (beta+betaMin)*0.5 : beta*0.5; }
            beta = std::max(1e-15, std::min(beta, 1e15));
        }
        double sumP = 0;
        for (int j = 0; j < N; ++j) sumP += P[i][j];
        if (sumP < 1e-14) sumP = 1e-14;
        for (int j = 0; j < N; ++j) P[i][j] /= sumP;
    }
    // Symmetrize + clamp
    for (int i = 0; i < N; ++i)
        for (int j = i+1; j < N; ++j) {
            double pij = std::max((P[i][j] + P[j][i]) / (2.0 * N), 1e-12);
            P[i][j] = P[j][i] = pij;
        }

    // Initialize 2D embedding with tiny Gaussian noise
    std::mt19937 rng(42);
    std::normal_distribution<double> nd(0.0, 1e-4);
    std::vector<std::vector<double>> Y(N, std::vector<double>(2));
    for (int i = 0; i < N; ++i) { Y[i][0] = nd(rng); Y[i][1] = nd(rng); }

    std::vector<std::vector<double>> dY(N, std::vector<double>(2, 0.0));
    std::vector<std::vector<double>> iY(N, std::vector<double>(2, 0.0));  // velocity
    std::vector<std::vector<double>> gains(N, std::vector<double>(2, 1.0));
    const double eta = 200.0;
    double momentum = 0.5;

    // Gradient descent
    for (int iter = 0; iter < maxIter; ++iter) {
        if (iter == 250) momentum = 0.8;
        const double pMult = (iter < 100) ? 12.0 : 1.0;  // early exaggeration

        // Low-dim weights W_ij = 1/(1+||y_i-y_j||²) and their sum
        std::vector<std::vector<double>> W(N, std::vector<double>(N, 0.0));
        double sumQ = 0.0;
        for (int i = 0; i < N; ++i)
            for (int j = i+1; j < N; ++j) {
                double dx = Y[i][0]-Y[j][0], dy = Y[i][1]-Y[j][1];
                double w = 1.0 / (1.0 + dx*dx + dy*dy);
                W[i][j] = W[j][i] = w;
                sumQ += 2.0 * w;
            }
        if (sumQ < 1e-14) sumQ = 1e-14;

        // Gradient: dC/dy_i = 4 * Σ_j (P_ij - Q_ij) * W_ij * (y_i - y_j)
        for (int i = 0; i < N; ++i) {
            dY[i][0] = dY[i][1] = 0.0;
            for (int j = 0; j < N; ++j) {
                if (i == j) continue;
                double mult = 4.0 * (P[i][j]*pMult - W[i][j]/sumQ) * W[i][j];
                dY[i][0] += mult * (Y[i][0] - Y[j][0]);
                dY[i][1] += mult * (Y[i][1] - Y[j][1]);
            }
        }

        // Update: momentum + adaptive gains (van der Maaten 2008)
        for (int i = 0; i < N; ++i)
            for (int d = 0; d < 2; ++d) {
                bool sameSign = (dY[i][d] > 0) == (iY[i][d] > 0);
                gains[i][d] = sameSign ? std::max(0.01, gains[i][d]*0.8) : gains[i][d]+0.2;
                iY[i][d] = momentum * iY[i][d] - eta * gains[i][d] * dY[i][d];
                Y[i][d] += iY[i][d];
            }

        // Re-center around origin
        double mx = 0, my = 0;
        for (int i = 0; i < N; ++i) { mx += Y[i][0]; my += Y[i][1]; }
        mx /= N; my /= N;
        for (int i = 0; i < N; ++i) { Y[i][0] -= mx; Y[i][1] -= my; }
    }

    // Final KL divergence
    {
        double sumQ = 0.0;
        std::vector<std::vector<double>> W(N, std::vector<double>(N, 0.0));
        for (int i = 0; i < N; ++i)
            for (int j = i+1; j < N; ++j) {
                double dx = Y[i][0]-Y[j][0], dy = Y[i][1]-Y[j][1];
                double w = 1.0 / (1.0 + dx*dx + dy*dy);
                W[i][j] = W[j][i] = w; sumQ += 2.0*w;
            }
        if (sumQ < 1e-14) sumQ = 1e-14;
        double kl = 0.0;
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j) {
                if (i == j || P[i][j] < 1e-12) continue;
                kl += P[i][j] * std::log(P[i][j] / std::max(W[i][j]/sumQ, 1e-12));
            }
        result.finalKl = kl;
    }

    // Fill result
    result.projected.reserve(N);
    for (int i = 0; i < N; ++i) result.projected.emplace_back(Y[i][0], Y[i][1]);
    result.iterationsRun = maxIter;

    std::ostringstream ss;
    ss << "OK — " << N << " punktow, KL=" << std::fixed << std::setprecision(3) << result.finalKl;
    result.status = ss.str();

    // K-means++ on 2D for cluster coloring
    std::vector<std::vector<float>> data2D(N, std::vector<float>(2));
    for (int i = 0; i < N; ++i) {
        data2D[i][0] = static_cast<float>(Y[i][0]);
        data2D[i][1] = static_cast<float>(Y[i][1]);
    }
    kMeansPP(data2D, std::min(3, N), result.clusterAssignments);

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

// ── Isolation Forest ─────────────────────────────────────────

namespace {

// Average path length in unsuccessful BST search (Liu et al. 2008)
double iforestC(double n) {
    if (n <= 1.0) return 0.0;
    if (n == 2.0) return 1.0;
    // c(n) = 2*(ln(n-1) + 0.5772156649) - 2*(n-1)/n
    return 2.0 * (std::log(n - 1.0) + 0.5772156649) - 2.0 * (n - 1.0) / n;
}

// Build one isolation tree recursively into tree.nodes; returns node index
int iforestBuildNode(
    LearningEngine::IsoTree &tree,
    const std::vector<std::vector<float>> &data,
    std::vector<int> &indices,
    int depth, int maxDepth,
    std::mt19937 &rng)
{
    int nodeIdx = static_cast<int>(tree.nodes.size());
    tree.nodes.push_back({});
    LearningEngine::IsoNode &node = tree.nodes[nodeIdx];
    node.nodeSize = static_cast<int>(indices.size());

    if (depth >= maxDepth || indices.size() <= 1) {
        node.featIdx = -1;  // leaf
        return nodeIdx;
    }

    int D = static_cast<int>(data[0].size());

    // Pick random feature then random split within [min, max]
    // Try up to D features to find one with nonzero range
    std::uniform_int_distribution<int> featDist(0, D - 1);
    int feat = -1;
    float fMin = 0, fMax = 0;
    for (int attempt = 0; attempt < D; ++attempt) {
        int f = featDist(rng);
        float mn = data[indices[0]][f], mx = mn;
        for (int idx : indices) {
            mn = std::min(mn, data[idx][f]);
            mx = std::max(mx, data[idx][f]);
        }
        if (mx > mn) { feat = f; fMin = mn; fMax = mx; break; }
    }
    if (feat == -1) { node.featIdx = -1; return nodeIdx; }  // all same — leaf

    std::uniform_real_distribution<float> splitDist(fMin, fMax);
    float splitVal = splitDist(rng);

    node.featIdx  = feat;
    node.splitVal = splitVal;

    std::vector<int> leftIdx, rightIdx;
    for (int idx : indices)
        (data[idx][feat] < splitVal ? leftIdx : rightIdx).push_back(idx);

    // Ensure neither side is empty (edge case: all equal to splitVal)
    if (leftIdx.empty() || rightIdx.empty()) {
        node.featIdx = -1;
        return nodeIdx;
    }

    // Reserve space (prevent reallocations that would invalidate &node)
    tree.nodes.reserve(tree.nodes.size() + indices.size() * 2);

    node.left  = iforestBuildNode(tree, data, leftIdx,  depth + 1, maxDepth, rng);
    node.right = iforestBuildNode(tree, data, rightIdx, depth + 1, maxDepth, rng);
    // Re-fetch reference after possible reallocation
    tree.nodes[nodeIdx].left  = node.left;
    tree.nodes[nodeIdx].right = node.right;

    return nodeIdx;
}

// Path length for point x in one tree
double iforestPathLength(
    const LearningEngine::IsoTree &tree,
    const std::vector<float> &x,
    int nodeIdx, int depth)
{
    const LearningEngine::IsoNode &node = tree.nodes[nodeIdx];
    if (node.featIdx == -1)
        return static_cast<double>(depth) + iforestC(node.nodeSize);

    int next = (x[node.featIdx] < static_cast<float>(node.splitVal))
               ? node.left : node.right;
    return iforestPathLength(tree, x, next, depth + 1);
}

} // namespace

void LearningEngine::trainIsolationForest(int nTrees, int sampleSize) {
    std::unique_lock lock(m_mutex);
    auto windows = splitWindows(m_frameHistory, 500000);
    if (windows.size() < 5) return;

    std::vector<std::vector<float>> features;
    features.reserve(windows.size());
    for (const auto &w : windows)
        features.push_back(buildWindowFeatures(w));

    int N    = static_cast<int>(features.size());
    int psi  = std::min(sampleSize, N);         // subsample size
    int maxD = static_cast<int>(std::ceil(std::log2(psi)));
    int D    = static_cast<int>(features[0].size());

    m_iforest.trees.clear();
    m_iforest.trees.resize(nTrees);
    m_iforest.nFeatures  = D;
    m_iforest.sampleSize = psi;

    std::mt19937 rng(42);
    std::vector<int> allIdx(N);
    std::iota(allIdx.begin(), allIdx.end(), 0);

    for (int t = 0; t < nTrees; ++t) {
        // Subsample without replacement
        std::vector<int> sample = allIdx;
        std::shuffle(sample.begin(), sample.end(), rng);
        sample.resize(psi);

        m_iforest.trees[t].nodes.clear();
        m_iforest.trees[t].nodes.reserve(psi * 2);
        iforestBuildNode(m_iforest.trees[t], features, sample, 0, maxD, rng);
    }

    m_iforest.trained = true;
}

double LearningEngine::scoreLatestWindow() const {
    std::shared_lock lock(m_mutex);
    if (!m_iforest.trained || m_iforest.trees.empty()) return 0.0;
    if (m_frameHistory.empty()) return 0.0;

    auto windows = splitWindows(m_frameHistory, 500000);
    if (windows.empty()) return 0.0;

    auto feat = buildWindowFeatures(windows.back());
    if (static_cast<int>(feat.size()) != m_iforest.nFeatures) return 0.0;

    double cn = iforestC(static_cast<double>(m_iforest.sampleSize));
    if (cn < 1e-9) return 0.0;

    double sumH = 0.0;
    for (const auto &tree : m_iforest.trees)
        sumH += iforestPathLength(tree, feat, 0, 0);
    double avgH = sumH / static_cast<double>(m_iforest.trees.size());
    return std::pow(2.0, -avgH / cn);
}

std::vector<LearningEngine::IForestResult>
LearningEngine::scoreIsolationForest(double threshold) const {
    std::shared_lock lock(m_mutex);
    std::vector<IForestResult> results;
    if (!m_iforest.trained || m_iforest.trees.empty()) return results;

    auto windows = splitWindows(m_frameHistory, 500000);
    if (windows.empty()) return results;

    double cn = iforestC(static_cast<double>(m_iforest.sampleSize));
    if (cn < 1e-9) return results;

    for (int i = 0; i < static_cast<int>(windows.size()); ++i) {
        auto feat = buildWindowFeatures(windows[i]);
        if (static_cast<int>(feat.size()) != m_iforest.nFeatures) continue;

        double sumH = 0.0;
        for (const auto &tree : m_iforest.trees)
            sumH += iforestPathLength(tree, feat, 0, 0);
        double avgH = sumH / static_cast<double>(m_iforest.trees.size());
        double score = std::pow(2.0, -avgH / cn);

        IForestResult r;
        r.windowIndex = i;
        r.score       = score;
        r.isAnomaly   = (score >= threshold);
        results.push_back(r);
    }

    std::sort(results.begin(), results.end(),
        [](const IForestResult &a, const IForestResult &b){
            return a.score > b.score;
        });
    return results;
}
