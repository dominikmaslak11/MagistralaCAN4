#pragma once
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QTableView>
#include <QTableWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QTimer>
#include <QHash>
#include <deque>
#include <vector>
#include <utility>
#include "CanFrame.h"
#include "CandidateModel.h"
#include "GpuCorrelator.h"

struct EventRecord {
    QVector<CanFrame> windowFrames;
    QHash<uint32_t, QVector<float>> idFeatures;
};

struct ValueObservation {
    double value;
    QHash<uint32_t, std::vector<uint8_t>> idAverageBytes;
};

class AssociativeLearner : public QWidget {
    Q_OBJECT
public:
    explicit AssociativeLearner(QWidget *parent = nullptr);
    ~AssociativeLearner() override;

public slots:
    void processFrame(const CanFrame &frame);
    void markEvent();
    void resetLearning();
    void addObservation();
    void saveSession();
    void loadSession();
    void clusterWindows();
    void trainPrediction();
    void updatePredictionDisplay();

    // Anomalie
    void startAnomalyMonitoring();
    void stopAnomalyMonitoring();
    void checkAnomaly();

signals:
    void eventMarked(int iteration);

private:
    void updateCandidates();
    void updateCorrelationTable();
    void updateSequenceTable();
    void updateCrossByteTable();
    void recalcAdaptiveWindow();
    QHash<uint32_t, QVector<float>> buildFeatureVectors(const QVector<CanFrame> &window);

    QVector<float> buildWindowFeatures(const QVector<CanFrame> &window);
    int kMeans(const QVector<QVector<float>> &data, int K, QVector<int> &assignments);
    void buildNormalModel();

    static constexpr int64_t DEFAULT_BEFORE = 500000;
    static constexpr int64_t DEFAULT_AFTER  = 200000;

    int64_t m_adaptiveBefore = DEFAULT_BEFORE;
    int64_t m_adaptiveAfter  = DEFAULT_AFTER;

    QPushButton *m_markEventBtn;
    QPushButton *m_resetBtn;
    QLabel      *m_iterationLabel;
    QTableView  *m_candidatesView;

    QLineEdit    *m_valueInput;
    QPushButton  *m_addObsBtn;
    QTableWidget *m_correlationTable;

    QComboBox    *m_ngramCombo;
    QTableWidget *m_sequenceTable;

    QTableWidget *m_crossByteTable;

    QPushButton  *m_clusterBtn;
    QTableWidget *m_clusterTable;

    QPushButton  *m_trainPredictionBtn;
    QTableWidget *m_predictionTable;
    QTimer       *m_predictionTimer;

    // --- Anomalie ---
    QPushButton  *m_anomalyToggleBtn;
    QLineEdit    *m_anomalyThreshold;
    QTableWidget *m_anomalyTable;
    QTimer       *m_anomalyTimer;
    bool          m_monitoring = false;

    QVector<float> m_normalMean;
    QVector<float> m_normalStd;
    double         m_anomalyThresholdValue = 10.0;

    QPushButton  *m_saveBtn;
    QPushButton  *m_loadBtn;

    CandidateModel *m_candidateModel;
    GpuCorrelator   m_correlator;

    std::deque<CanFrame> m_frameHistory;
    static constexpr int HISTORY_MAX = 20000;

    QVector<EventRecord> m_events;
    QVector<ValueObservation> m_observations;
    int m_iteration = 0;

    QHash<QPair<uint32_t,int>, QPair<double,double>> m_linearModels;
};
