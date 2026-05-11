#pragma once
#include "CanFrame.h"
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
};

struct LeMiEntry {
    uint32_t id;
    int byte;
    double mi;
    double pearson;
    std::string comparison; // "Nieliniowa", "Silna", "Slaba"
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

    // ── Clustering ─────────────────────────────────────────
    static int kMeans(const std::vector<std::vector<float>> &data,
                      int K, std::vector<int> &assignments);
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
        std::vector<double> pc1;       // first principal component
        std::vector<double> pc2;       // second principal component
        double eig1, eig2;             // eigenvalues
        double varianceExplained;
        std::vector<std::pair<double,double>> projected; // (x,y) points
        std::vector<int> clusterAssignments;             // k-means on 2D
    };
    PcaResult runPcaClustering() const;

    // ── Prediction (linear regression) ────────────────────
    std::vector<LePredictionModel>
        trainPrediction(const std::string &variableKey);
    std::vector<LeRealtimePrediction>
        predictRealtime(const std::string &variableKey) const;

    // ── Anomaly detection ─────────────────────────────────
    void buildNormalModel();
    double checkAnomaly(double threshold) const;
    bool  modelBuilt() const { return !m_normalMean.empty(); }

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
    int64_t adaptiveBefore() const { return m_adaptiveBefore; }
    int64_t adaptiveAfter()  const { return m_adaptiveAfter;  }

    // ── Serialization (return JSON-compatible strings) ────
    std::string serializeSession() const;   // full state → JSON string
    bool deserializeSession(const std::string &json);          // JSON → full state

    // ── Getters for UI ────────────────────────────────────
    const std::vector<LeEventRecord> &events() const { return m_events; }
    const std::vector<LeEventRecord> &nonEvents() const { return m_nonEvents; }
    const std::unordered_map<std::string,
        std::unordered_map<uint32_t, std::pair<double,double>>> &
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

    // ── Observations (variable → observations) ────────────
    std::unordered_map<std::string, std::vector<LeValueObservation>> m_observations;

    // ── Models ────────────────────────────────────────────
    // linearModels: variableKey → (id,byte) → (a,b)
    std::unordered_map<std::string,
        std::unordered_map<uint32_t, std::pair<double,double>>> m_linearModels;

    // Markov: fromId → toId → count
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, int>> m_transitions;
    std::unordered_map<uint32_t, uint32_t> m_markovBestNext;
    std::unordered_map<uint32_t, double> m_markovProb;

    // Anomaly normal model
    std::vector<float> m_normalMean;
    std::vector<float> m_normalStd;

    // Neural network weights (same structure as original)
    struct NnWeights {
        std::vector<std::vector<double>> w1, w2, w3;
        std::vector<double> b1, b2;
        double b3 = 0;
        bool trained = false;
    };
    NnWeights m_nnWeights;
    int m_nnInputDim = 16;

    // ── Internal helpers ──────────────────────────────────
    std::vector<CanFrame> extractWindow() const;
    static double correlationPearson(const std::vector<double> &x,
                                     const std::vector<double> &y);
    static double correlationPearson(const std::vector<float> &x,
                                     const std::vector<float> &y);
};
