#pragma once
#include "CanFrame.h"
#include "GpuCompute.h"
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>
#include <shared_mutex>

// ── STL-compatible data structures (Qt-free) ─────────────────

struct LeEventRecord {
    std::vector<CanFrame> windowFrames;
    std::unordered_map<uint32_t, std::vector<float>> idFeatures;
};

struct LeValueObservation {
    double value;
    uint64_t timestamp;  // #26: microseconds, for ring buffer age + temporal decay
    std::unordered_map<uint32_t, std::vector<uint8_t>> idAverageBytes;
};

// ── Result structs for pure ML computations ─────────────────

struct LeCandidate {
    uint32_t canId;
    std::string description;
    float score;
    int occurrences;
};

struct LeCorrelationEntry {
    uint32_t id;
    int byte;
    double correlation;
    double pValue;
    bool significant;
};

struct LeSequenceEntry {
    std::string sequence;
    int occurrences;
    float confidence;
};

struct LeCrossByteEntry {
    uint32_t id1; int byte1;
    uint32_t id2; int byte2;
    double correlation;
};

struct LeClusterStats {
    int clusterId;
    double avgFrameCount;
    std::vector<uint32_t> dominantIds;
    int windowCount;
};

struct LePredictionModel {
    uint32_t id;
    int byte;
    double a;
    double b;
    double lowerBound = 0;   // #32: 95% confidence lower bound
    double upperBound = 0;   // #32: 95% confidence upper bound
};

struct LeMiEntry {
    uint32_t id;
    int byte;
    double mi;
    double pearson;
    std::string comparison;
};

struct LeMicEntry {
    uint32_t id;
    int byte;
    double mic;
};

struct LeMarkovEntry {
    uint32_t fromId;
    uint32_t toId;
    double probability;
};

struct LeWcssPoint {
    int k;
    double wcss;
};

struct LeAnomalyRecord {
    double timeSec;
    double score;
};

struct LeFftPeak {
    double frequency;
    double magnitude;
    double periodMs;
    std::string description;
};

struct LeRealtimePrediction {
    uint32_t id;
    int byte;
    double predictedValue;
};

// ── LearningEngine: pure C++ ML engine (no Qt) ─────────────

class LearningEngine {
public:
    LearningEngine() = default;
    ~LearningEngine() = default;

    // ── Thread-safe frame ingestion ─────────────────────────
    void processFrame(const CanFrame &frame);
    std::vector<CanFrame> frameHistorySnapshot() const;

    // ── Event management ────────────────────────────────────
    void markEvent(int64_t adaptiveBefore, int64_t adaptiveAfter);
    void markNonEvent(int64_t adaptiveBefore, int64_t adaptiveAfter);
    void resetLearning();
    int iterationCount() const { return m_iteration; }

    // ── Variable & observation management ──────────────────
    void addVariable(const std::string &key);
    void addObservation(const std::string &variableKey, double value,
                        int64_t adaptiveBefore, int64_t adaptiveAfter);
    const std::vector<LeValueObservation> &
        observations(const std::string &variableKey) const;
    std::vector<std::string> variableNames() const;
    bool hasVariable(const std::string &key) const;

    // ── #26 Ring buffer capacity ────────────────────────────
    void setMaxObservations(size_t n) { m_maxObservations = n; }
    size_t maxObservations() const { return m_maxObservations; }

    // ── #27 Temporal decay ─────────────────────────────────
    void setDecayLambda(double lambda) { m_decayLambda = lambda; }
    double decayLambda() const { return m_decayLambda; }

    // ── #28 Online learning ────────────────────────────────
    void setOnlineLearning(bool on) { m_onlineLearning = on; }
    bool onlineLearning() const { return m_onlineLearning; }
    std::vector<LeCorrelationEntry>
        computeCorrelationsOnline(const std::string &variableKey) const;

    // ── Feature extraction ─────────────────────────────────
    std::unordered_map<uint32_t, std::vector<float>>
        buildFeatureVectors(const std::vector<CanFrame> &window) const;
    std::vector<float>
        buildWindowFeatures(const std::vector<CanFrame> &window) const;

    // ── Candidate ranking ──────────────────────────────────
    std::vector<LeCandidate>
        computeCandidates(const std::string *dbcDescription = nullptr,
                          const std::string *j1939Name = nullptr) const;

    // ── Correlation table (Pearson) ────────────────────────
    std::vector<LeCorrelationEntry>
        computeCorrelations(const std::string &variableKey) const;

    // ── Sequence analysis (bigrams/trigrams) ───────────────
    std::vector<LeSequenceEntry>
        computeSequences(int ngramLength) const;

    // ── Cross-byte correlation ─────────────────────────────
    std::vector<LeCrossByteEntry>
        computeCrossByte(const std::string &variableKey) const;

    // ── #40 Cross-correlation with time lag ────────────────
    struct LagCorrEntry {
        uint32_t id; int byte;
        int lag;               // negative = byte leads, positive = byte lags
        double correlation;
    };
    std::vector<LagCorrEntry>
        computeCrossCorrelationLag(const std::string &variableKey,
                                   int maxLag = 10) const;

    // ── #38 Granger causality ──────────────────────────────
    struct GrangerResult {
        uint32_t id; int byte;
        double fStatistic;
        double pValue;
        bool isCausal;         // p < 0.05
        int bestLag;
    };
    std::vector<GrangerResult>
        computeGrangerCausality(const std::string &variableKey,
                                int maxLag = 5) const;

    // ── #39 Change-point detection ─────────────────────────
    struct ChangePoint {
        int index;
        double costReduction;
        double meanBefore;
        double meanAfter;
    };
    std::vector<ChangePoint>
        detectChangePoints(const std::string &variableKey,
                           uint32_t targetId, int targetByte,
                           int minSegmentSize = 10) const;

    // ── Clustering ─────────────────────────────────────────
    static int kMeans(const std::vector<std::vector<float>> &data,
                      int K, std::vector<int> &assignments);
    static int kMeansPP(const std::vector<std::vector<float>> &data,
                        int K, std::vector<int> &assignments);  // #36 k-means++
    static int dbscan(const std::vector<std::vector<float>> &data,
                      float eps, int minPts, std::vector<int> &assignments);

    std::vector<LeClusterStats>
        clusterWindows(int K = 3) const;
    std::vector<LeClusterStats>
        dbscanClustering(float eps, int minPts) const;
    std::vector<LeWcssPoint>
        autoKMeans(int maxK = 10) const;

    // ── PCA clustering ─────────────────────────────────────
    struct PcaResult {
        std::vector<double> pc1;
        std::vector<double> pc2;
        double eig1, eig2;
        double varianceExplained;
        std::vector<std::pair<double,double>> projected;
        std::vector<int> clusterAssignments;
    };
    PcaResult runPcaClustering() const;

    // ── Prediction (linear regression) ────────────────────
    std::vector<LePredictionModel>
        trainPrediction(const std::string &variableKey);
    std::vector<LeRealtimePrediction>
        predictRealtime(const std::string &variableKey) const;

    // ── #33 Multi-target ─────────────────────────────────
    void trainAllPredictions();
    struct MultiPrediction {
        std::string variableName;
        std::vector<LeRealtimePrediction> predictions;
    };
    std::vector<MultiPrediction> predictRealtimeAll() const;

    // ── Anomaly detection ─────────────────────────────────
    void buildNormalModel();
    double checkAnomaly(double threshold) const;
    bool modelBuilt() const { return !m_normalMean.empty(); }

    // ── #29 EWMA anomaly ──────────────────────────────────
    void setEwmaAnomaly(bool on) { m_ewmaAnomaly = on; }
    double checkAnomalyEwma() const;

    // ── Markov chain ───────────────────────────────────────
    void trainMarkovModel();
    std::vector<LeMarkovEntry> predictNextFrames() const;

    // ── Cross-variable correlation matrix ─────────────────
    std::vector<std::vector<double>>
        computeCrossVariableMatrix() const;

    // ── Mutual Information ────────────────────────────────
    std::vector<LeMiEntry>
        computeMutualInformation(const std::string &variableKey) const;

    // ── Maximal Information Coefficient ───────────────────
    std::vector<LeMicEntry>
        computeMIC(const std::string &variableKey) const;

    // ── FFT ────────────────────────────────────────────────
    static void computeDft(const std::vector<double> &signal, double fs,
                           std::vector<double> &mags, std::vector<double> &freqs);
    struct FftResult {
        std::vector<double> frequencies;
        std::vector<double> magnitudes;
        double fsHz;
        std::vector<LeFftPeak> peaks;
        int sampleCount;
    };
    FftResult runFftAnalysis(uint32_t targetId, int byteIdx) const;

    // ── #31 Gradient Boosted Trees ────────────────────────
    struct GbtTree {
        int featureIdx;
        double threshold;
        double leftValue;
        double rightValue;
    };
    struct GbtModel {
        std::vector<GbtTree> trees;
        double basePrediction = 0;
        double learningRate = 0.1;
    };
    GbtModel trainGbt(const std::string &variableKey,
                      int maxTrees = 50, int maxDepth = 3) const;
    double predictGbt(const GbtModel &model,
                      const std::vector<double> &features) const;

    // ── #32 Confidence intervals ───────────────────────────
    std::vector<LePredictionModel>
        trainPredictionWithCI(const std::string &variableKey,
                              int bootstrapSamples = 100);

    // ── Neural network ────────────────────────────────────
    struct NnTrainingResult {
        bool trained = false;
        std::string status;
        int inputDim;
        int hidden1, hidden2;
        int sampleCount;
    };
    NnTrainingResult trainNeuralNetwork(
        const std::vector<LeCorrelationEntry> &correlations,
        const std::string &variableKey,
        int maxInputDim = 16);
    double predictNeural(const std::vector<double> &input) const;

    // ── Auto-discovery ────────────────────────────────────
    std::vector<LeCorrelationEntry>
        autoDiscovery(const std::string &variableKey) const;

    // ── Statistical helpers ────────────────────────────────
    static double pearsonPValue(double r, int n);

    // ── Adaptive window ───────────────────────────────────
    void recalcAdaptiveWindow();
    void recalcAdaptiveWindowLocked();  // internal: assumes mutex already held
    int64_t adaptiveBefore() const { return m_adaptiveBefore; }
    int64_t adaptiveAfter()  const { return m_adaptiveAfter;  }

    // ── #42-45 Serialization & persistence ─────────────────
    std::string serializeSession() const;
    bool deserializeSession(const std::string &json);
    bool saveCheckpoint(const std::string &path) const;        // #43
    bool loadCheckpoint(const std::string &path);              // #45
    std::string exportOnnx(const std::string &variableKey) const; // #44 stub

    // ── Getters for UI ────────────────────────────────────
    const std::vector<LeEventRecord> &events() const { return m_events; }
    const std::vector<LeEventRecord> &nonEvents() const { return m_nonEvents; }
    const std::unordered_map<std::string,
        std::unordered_map<uint64_t, std::pair<double,double>>> &
        linearModels() const { return m_linearModels; }

private:
    // ── Thread safety ──────────────────────────────────────
    mutable std::shared_mutex m_mutex;

    // ── Frame history ──────────────────────────────────────
    std::deque<CanFrame> m_frameHistory;
    static constexpr int HISTORY_MAX = 10000;

    // ── Event data ─────────────────────────────────────────
    std::vector<LeEventRecord> m_events;
    std::vector<LeEventRecord> m_nonEvents;
    int m_iteration = 0;

    // ── Adaptive window ───────────────────────────────────
    int64_t m_adaptiveBefore = 500000;
    int64_t m_adaptiveAfter  = 200000;

    // ── #26 Ring buffer ────────────────────────────────────
    std::unordered_map<std::string, std::vector<LeValueObservation>> m_observations;
    size_t m_maxObservations = 10000;

    // ── #27 Temporal decay ─────────────────────────────────
    double m_decayLambda = 0.0;

    // ── #28 Online learning (Welford) ──────────────────────
    bool m_onlineLearning = false;
    struct WelfordAccum {
        size_t n = 0;
        double meanX = 0, M2x = 0;  // for variable value
        double meanY = 0, M2y = 0;  // for byte value
        double Cxy = 0;             // covariance accumulator
    };
    // key: composite (variableHash << 40) | (id << 8) | byte
    std::unordered_map<uint64_t, WelfordAccum> m_welford;
    void updateWelford(const std::string &variableKey, uint32_t id, int byteIdx,
                       double valX, double valY);

    // ── #29 EWMA anomaly ──────────────────────────────────
    bool m_ewmaAnomaly = false;
    struct EwmaState {
        std::vector<double> mean;    // EWMA of window features
        std::vector<double> var;     // EWMA of squared deviations
        double alpha = 0.1;          // smoothing factor
    };
    mutable EwmaState m_ewma;
    void updateEwma(const std::vector<float> &features);

    // ── Models ────────────────────────────────────────────
    std::unordered_map<std::string,
        std::unordered_map<uint64_t, std::pair<double,double>>> m_linearModels;

    std::unordered_map<uint32_t, std::unordered_map<uint32_t, int>> m_transitions;
    std::unordered_map<uint32_t, uint32_t> m_markovBestNext;
    std::unordered_map<uint32_t, double> m_markovProb;

    std::vector<float> m_normalMean;
    std::vector<float> m_normalStd;

    struct NnWeights {
        std::vector<std::vector<double>> w1, w2, w3;
        std::vector<double> b1, b2;
        double b3 = 0;
        bool trained = false;
        // Adam optimizer state (#30)
        std::vector<std::vector<double>> m_w1, m_w2, m_w3, v_w1, v_w2, v_w3;
        std::vector<double> m_b1, m_b2, v_b1, v_b2;
        double m_b3 = 0, v_b3 = 0;
        int adamStep = 0;
    };
    NnWeights m_nnWeights;
    int m_nnInputDim = 16;

    // ── #37 PCA helpers ──────────────────────────────────
    static void jacobiEigen(const std::vector<std::vector<double>> &mat,
                            std::vector<double> &eigenvalues,
                            std::vector<std::vector<double>> &eigenvectors,
                            int maxIter = 50);

    // ── GPU accelerator ──────────────────────────────────
    static GpuCompute m_gpu;   // shared GPU accelerator

    // ── Internal helpers ──────────────────────────────────
    std::vector<CanFrame> extractWindow() const;
    static double correlationPearson(const std::vector<double> &x,
                                     const std::vector<double> &y);
    static double correlationPearson(const std::vector<float> &x,
                                     const std::vector<float> &y);
    static double correlationPearsonWeighted(
        const std::vector<double> &x, const std::vector<double> &y,
        const std::vector<uint64_t> &timestamps, double lambda);
};
