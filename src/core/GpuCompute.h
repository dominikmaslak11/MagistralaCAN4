#pragma once
#include <vector>
#include <string>
#include <cstdint>

// ── GpuCompute: STL-based GPU accelerator (OpenCL) ──────────

class GpuCompute {
public:
    GpuCompute();
    ~GpuCompute();

    bool isAvailable() const { return m_available; }

    // ── Correlation matrix (PCA, feature similarity) ────────
    // Input: features[N][dim] row-major
    // Output: corr[N][N] symmetric
    std::vector<std::vector<float>>
        correlationMatrix(const std::vector<std::vector<float>> &features);

    // ── Batch Pearson (many (x,y) pairs at once) ───────────
    // Input: xs[P][N], ys[P][N] — P pairs, each N samples
    // Output: correlations[P]
    std::vector<double>
        pearsonBatch(const std::vector<std::vector<double>> &xs,
                     const std::vector<std::vector<double>> &ys);

    // ── k-Means distance (points to centroids) ─────────────
    // Input: points[N][dim], centroids[K][dim]
    // Output: distances[N][K]
    std::vector<std::vector<float>>
        kmeansDistances(const std::vector<std::vector<float>> &points,
                        const std::vector<std::vector<float>> &centroids);

    // ── DBSCAN range query batch ───────────────────────────
    // Input: points[N][dim], queries[Q][dim], eps2 (squared radius)
    // Output: for each query, indices of points within eps2
    std::vector<std::vector<int>>
        dbscanBatchQuery(const std::vector<std::vector<float>> &points,
                         const std::vector<std::vector<float>> &queries,
                         float eps2);

    // ── MLP forward pass (single batch) ────────────────────
    // Input: X[batch][D], w1[D][H1], b1[H1], w2[H1][H2], b2[H2], w3[H2][1], b3
    // Output: predictions[batch]
    std::vector<double>
        mlpForward(const std::vector<std::vector<double>> &X,
                   const std::vector<std::vector<double>> &w1,
                   const std::vector<double> &b1,
                   const std::vector<std::vector<double>> &w2,
                   const std::vector<double> &b2,
                   const std::vector<std::vector<double>> &w3,
                   double b3);

private:
    bool m_available = false;
    bool initOpenCL();
    void cpuFallback(const char *operation);

#ifdef HAS_OPENCL
    void *m_context = nullptr;
    void *m_queue = nullptr;
    void *m_progCorr = nullptr;
    void *m_kernCorr = nullptr;
    void *m_progPearson = nullptr;
    void *m_kernPearson = nullptr;
    void *m_progKmeans = nullptr;
    void *m_kernKmeans = nullptr;
#endif
};
