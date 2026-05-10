#pragma once
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QTableView>
#include <QTableWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QShortcut>
#include <QTimer>
#include <QMap>
#include <QHash>
#include <QReadWriteLock>
#include <deque>
#include <vector>
#include <utility>
#include <QtCharts/QChartView>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QLineSeries>
#include <QtCharts/QChart>
#include "CanFrame.h"
#include "CandidateModel.h"
#include "GpuCorrelator.h"

class DbcParser;
class J1939Parser;

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
    void markNonEvent();
    void resetLearning();
    void addVariable(const QString &name);
    QVector<ValueObservation> currentObservations() const;
    void addObservation();
    void setDbcParser(const DbcParser *dbc) { m_dbc = dbc; }
    void setJ1939Parser(const J1939Parser *j1939) { m_j1939 = j1939; }
    void saveSession();
    void autoSave();
    void loadSession();
    void exportModels();
    void importModels();
    void generateLuaScript();
    void exportHtmlReport();
    void clusterWindows();
    void dbscanClustering();
    void trainPrediction();
    void updatePredictionDisplay();

    void startAnomalyMonitoring();
    void stopAnomalyMonitoring();
    void checkAnomaly();
    void checkAutoEvent();
    void trainMarkovModel();
    void predictNextFrames();
    void updateCrossVariableMatrix();
    void computeMutualInformation();
    void computeMIC();
    void autoKMeans();
    void runPcaClustering();
    void runFftAnalysis();

signals:
    void eventMarked(int iteration);
    void anomalyDetected();

private slots:
    void onVariableChanged(int idx);
    void addNewVariable();

private:
    void updateCandidates();
    void updateCorrelationTable();
    void updateSequenceTable();
    void updateCrossByteTable();
    void updateChart();
    void updateTimeChart();
    void applySignificanceFilter();
    void updateAutoDiscovery();
    static double pearsonPValue(double r, int n);
    void recalcAdaptiveWindow();
    QHash<uint32_t, QVector<float>> buildFeatureVectors(const QVector<CanFrame> &window);

    QVector<float> buildWindowFeatures(const QVector<CanFrame> &window);
    int kMeans(const QVector<QVector<float>> &data, int K, QVector<int> &assignments);
    static int dbscan(const QVector<QVector<float>> &data, float eps, int minPts, QVector<int> &assignments);
    static void computeDft(const QVector<double> &signal, double fs, QVector<double> &mags, QVector<double> &freqs);
    void buildNormalModel();

    static constexpr int64_t DEFAULT_BEFORE = 500000;
    static constexpr int64_t DEFAULT_AFTER  = 200000;

    int64_t m_adaptiveBefore = DEFAULT_BEFORE;
    int64_t m_adaptiveAfter  = DEFAULT_AFTER;

    // UI
    QPushButton *m_markEventBtn;
    QPushButton *m_markNonEventBtn;
    QPushButton *m_resetBtn;
    QLabel      *m_iterationLabel;
    QTableView  *m_candidatesView;

    // Zarządzanie zmiennymi
    QComboBox   *m_variableCombo;
    QLineEdit   *m_newVariableName;
    QPushButton *m_addVariableBtn;
    QLineEdit   *m_valueInput;
    QPushButton *m_addObsBtn;

    QTableWidget *m_correlationTable;
    QCheckBox    *m_significanceFilter;
    QComboBox    *m_ngramCombo;
    QTableWidget *m_sequenceTable;
    QTableWidget *m_crossByteTable;
    QPushButton  *m_clusterBtn;
    QPushButton  *m_dbscanBtn;
    QLineEdit    *m_dbscanEps;
    QLineEdit    *m_dbscanMinPts;
    QTableWidget *m_dbscanTable;
    QPushButton  *m_pcaBtn;
    QChartView   *m_pcaChartView;
    QChart       *m_pcaChart;
    QScatterSeries *m_pcaSeries;
    QTableWidget *m_clusterTable;
    QPushButton  *m_trainPredictionBtn;
    QTableWidget *m_predictionTable;
    QTimer       *m_predictionTimer;

    QPushButton  *m_anomalyToggleBtn;
    QLineEdit    *m_anomalyThreshold;
    QTableWidget *m_anomalyTable;
    QTimer       *m_anomalyTimer;
    QTimer       *m_autoEventTimer;
    QCheckBox   *m_autoEventCheck;
    QLineEdit   *m_autoEventThreshold;
    QLabel      *m_autoEventLabel;
    bool          m_monitoring = false;

    QVector<float> m_normalMean;
    QVector<float> m_normalStd;

    QPushButton  *m_saveBtn;
    QCheckBox    *m_autoSaveCheck;
    QTimer       *m_autoSaveTimer;
    QString       m_autoSavePath;

    QPushButton  *m_exportModelsBtn;
    QPushButton  *m_importModelsBtn;
    QPushButton  *m_generateLuaBtn;
    QPushButton  *m_exportReportBtn;
    QPushButton  *m_loadBtn;

    // Wykres scatter
    QChartView   *m_chartView;
    QChart       *m_chart;
    QScatterSeries *m_scatterSeries;

    // Wykres czasowy (zmienna + bajty CAN w czasie)
    QChartView   *m_timeChartView;
    QChart       *m_timeChart;
    QLineSeries  *m_varTimeSeries;
    QLineSeries  *m_byteTimeSeries;
    QComboBox    *m_timeIdCombo;
    QComboBox    *m_timeByteCombo;

    // FFT / analiza częstotliwości
    QPushButton  *m_fftBtn;
    QComboBox    *m_fftIdCombo;
    QComboBox    *m_fftByteCombo;
    QTableWidget *m_fftPeakTable;
    QChartView   *m_fftChartView;
    QChart       *m_fftChart;
    QLineSeries  *m_fftSeries;

    CandidateModel *m_candidateModel;
    GpuCorrelator   m_correlator;

    std::deque<CanFrame> m_frameHistory;
    static constexpr int HISTORY_MAX = 10000;
    mutable QReadWriteLock m_historyLock;

    QVector<EventRecord> m_events;
    QVector<EventRecord> m_nonEvents;   // okna bez zdarzenia (tło)
    int m_iteration = 0;

    // Mapowanie zmiennych
    QMap<QString, QVector<ValueObservation>> m_observationsMap;
    QString m_currentVariable;

    QHash<QPair<uint32_t,int>, QPair<double,double>> m_linearModels;
    // Metoda łokcia (Auto K)
    QPushButton *m_autoKBtn;
    QChartView  *m_elbowChartView;
    QChart      *m_elbowChart;
    QLineSeries *m_elbowSeries;
    // Mutual Information
    QPushButton  *m_miBtn;
    QPushButton  *m_micBtn;
    QTableWidget *m_micTable;
    QTableWidget *m_miTable;
    // Macierz korelacji zmiennych
    QPushButton  *m_crossVarBtn;
    QTableWidget *m_crossVarTable;
    // Predykcja sekwencji (Markov)
    QPushButton  *m_trainMarkovBtn;
    QTableWidget *m_markovTable;
    QTimer       *m_markovTimer;
    QHash<uint32_t, QHash<uint32_t, int>> m_transitions; // fromId -> toId -> count
    QHash<uint32_t, uint32_t> m_markovBestNext;        // fromId -> best next id
    QHash<uint32_t, double>   m_markovProb;            // fromId -> probability

    // Auto-discovery
    QCheckBox    *m_autoDiscoveryCheck;
    QTableWidget *m_autoDiscoveryTable;
    QTimer       *m_autoDiscoveryTimer;
    QLabel       *m_autoDiscoveryLabel;

    // Neural network
    QPushButton  *m_trainNnBtn;
    QLabel       *m_nnStatusLabel;
    QLabel       *m_nnPredictionLabel;
    QTimer       *m_nnTimer;
    struct NnWeights {
        QVector<QVector<double>> w1, w2, w3; // input→h1, h1→h2, h2→out
        QVector<double> b1, b2;
        double b3 = 0;
    };
    NnWeights m_nnWeights;
    bool m_nnTrained = false;
    int  m_nnInputDim = 16;
    void trainNeuralNetwork();
    double predictNeural(const QVector<double> &input) const;
    void updateNnPrediction();

    // Async computation helper (button feedback + QtConcurrent::run)
    template<typename Func>
    void runAsync(Func &&compute, QPushButton *btn, const QString &restoreText);

    const DbcParser   *m_dbc   = nullptr;
    const J1939Parser *m_j1939 = nullptr;
};
