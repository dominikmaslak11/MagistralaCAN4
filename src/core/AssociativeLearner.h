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
#include "LearningEngine.h"

class DbcParser;
class J1939Parser;

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
    void updateNnPrediction();
    void runGrangerCausality();
    void runChangePointDetection();
    void runCrossCorrelationLag();
    void trainGbtModel();
    void updateGbtDisplay();

    // ── ML engine (pure C++, no Qt) ───────────────────────
    LearningEngine m_engine;

    // ── UI state only ─────────────────────────────────────
    QString m_currentVariable;

    // ── DBC/J1939 enrichment (owned externally) ───────────
    const DbcParser   *m_dbc   = nullptr;
    const J1939Parser *m_j1939 = nullptr;

    // ── UI widgets ────────────────────────────────────────
    QPushButton *m_markEventBtn;
    QPushButton *m_markNonEventBtn;
    QPushButton *m_resetBtn;
    QLabel      *m_iterationLabel;
    QTableView  *m_candidatesView;

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

    QPushButton  *m_saveBtn;
    QCheckBox    *m_autoSaveCheck;
    QTimer       *m_autoSaveTimer;
    QString       m_autoSavePath;

    QPushButton  *m_exportModelsBtn;
    QPushButton  *m_importModelsBtn;
    QPushButton  *m_generateLuaBtn;
    QPushButton  *m_exportReportBtn;
    QPushButton  *m_loadBtn;

    // Charts
    QChartView   *m_chartView;
    QChart       *m_chart;
    QScatterSeries *m_scatterSeries;

    QChartView   *m_timeChartView;
    QChart       *m_timeChart;
    QLineSeries  *m_varTimeSeries;
    QLineSeries  *m_byteTimeSeries;
    QComboBox    *m_timeIdCombo;
    QComboBox    *m_timeByteCombo;

    QPushButton  *m_fftBtn;
    QComboBox    *m_fftIdCombo;
    QComboBox    *m_fftByteCombo;
    QTableWidget *m_fftPeakTable;
    QChartView   *m_fftChartView;
    QChart       *m_fftChart;
    QLineSeries  *m_fftSeries;

    CandidateModel *m_candidateModel;

    QPushButton *m_autoKBtn;
    QChartView  *m_elbowChartView;
    QChart      *m_elbowChart;
    QLineSeries *m_elbowSeries;

    QPushButton  *m_miBtn;
    QPushButton  *m_micBtn;
    QTableWidget *m_micTable;
    QTableWidget *m_miTable;

    QPushButton  *m_crossVarBtn;
    QTableWidget *m_crossVarTable;

    QPushButton  *m_trainMarkovBtn;
    QTableWidget *m_markovTable;
    QTimer       *m_markovTimer;

    QCheckBox    *m_autoDiscoveryCheck;
    QTableWidget *m_autoDiscoveryTable;
    QTimer       *m_autoDiscoveryTimer;
    QLabel       *m_autoDiscoveryLabel;

    QPushButton  *m_trainNnBtn;
    QLabel       *m_nnStatusLabel;
    QLabel       *m_nnPredictionLabel;
    QTimer       *m_nnTimer;

    // ── Zaawansowana analiza ────────────────────────────
    QPushButton  *m_grangerBtn;
    QTableWidget *m_grangerTable;
    QPushButton  *m_changePointBtn;
    QComboBox    *m_cpIdCombo;
    QComboBox    *m_cpByteCombo;
    QTableWidget *m_changePointTable;
    QPushButton  *m_lagCorrBtn;
    QTableWidget *m_lagCorrTable;
    QPushButton  *m_gbtBtn;
    QLabel       *m_gbtStatusLabel;
    QTableWidget *m_gbtPredTable;
    QCheckBox    *m_onlineLearningCheck;
    QCheckBox    *m_ewmaAnomalyCheck;

    // ── Async helper ──────────────────────────────────────
    template<typename Func>
    void runAsync(Func &&compute, QPushButton *btn, const QString &restoreText);
};
