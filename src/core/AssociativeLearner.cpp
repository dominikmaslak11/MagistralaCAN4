#include "Logger.h"
#include "AssociativeLearner.h"
#include "DbcParser.h"
#include "J1939Parser.h"
#include <QOpenGLWidget>
#include <QDir>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QFile>
#include <QFileDialog>
#include <QTextStream>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <numeric>
#include <cmath>
#include <set>
#include <random>
#include <complex>
#include <QRandomGenerator>

AssociativeLearner::AssociativeLearner(QWidget *parent) : QWidget(parent) {
    auto *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    auto *scrollWidget = new QWidget;
    auto *mainLayout = new QVBoxLayout(scrollWidget);
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0,0,0,0);
    outerLayout->addWidget(scrollArea);
    scrollArea->setWidget(scrollWidget);

    m_markEventBtn = new QPushButton("🔴 Zarejestruj zdarzenie");
    m_resetBtn = new QPushButton("Resetuj uczenie");
    m_iterationLabel = new QLabel("Liczba iteracji: 0");
    m_iterationLabel->setStyleSheet("color: #00ffaa; font-weight: bold;");
    mainLayout->addWidget(m_markEventBtn);
    m_markNonEventBtn = new QPushButton("⛔ Brak zdarzenia");
    mainLayout->addWidget(m_markNonEventBtn);
    mainLayout->addWidget(m_resetBtn);
    mainLayout->addWidget(m_iterationLabel);

    m_candidateModel = new CandidateModel(this);
    m_candidatesView = new QTableView;
    m_candidatesView->setModel(m_candidateModel);
    m_candidatesView->verticalHeader()->hide();
    m_candidatesView->horizontalHeader()->setStretchLastSection(true);
    m_candidatesView->setShowGrid(false);
    m_candidatesView->setAlternatingRowColors(false);
    m_candidatesView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_candidatesView->setMinimumHeight(400);
    mainLayout->addWidget(m_candidatesView);

    auto addHLine = [&]() { auto *f = new QFrame; f->setFrameShape(QFrame::HLine); f->setStyleSheet("background-color: #e94560;"); mainLayout->addWidget(f); };
    addHLine();

    auto *varLayout = new QHBoxLayout;
    varLayout->addWidget(new QLabel("Zmienna:"));
    m_variableCombo = new QComboBox; m_variableCombo->setMinimumWidth(140);
    varLayout->addWidget(m_variableCombo);
    varLayout->addWidget(new QLabel("Nazwa:"));
    m_newVariableName = new QLineEdit; m_newVariableName->setPlaceholderText("np. temperatura");
    m_addVariableBtn = new QPushButton("Nowa");
    varLayout->addWidget(m_newVariableName); varLayout->addWidget(m_addVariableBtn);
    mainLayout->addLayout(varLayout);

    auto *valLayout = new QHBoxLayout;
    valLayout->addWidget(new QLabel("Wartość:"));
    m_valueInput = new QLineEdit; m_valueInput->setPlaceholderText("0.0");
    m_addObsBtn = new QPushButton("Dodaj obserwację");
    valLayout->addWidget(m_valueInput); valLayout->addWidget(m_addObsBtn);
    mainLayout->addLayout(valLayout);

    auto makeTable = [&](QTableWidget *&tbl, const QStringList &headers) {
        tbl = new QTableWidget(0, headers.size());
        tbl->setHorizontalHeaderLabels(headers);
        tbl->verticalHeader()->hide(); tbl->horizontalHeader()->setStretchLastSection(true);
        tbl->setShowGrid(false); tbl->setAlternatingRowColors(false);
        tbl->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tbl->setMinimumHeight(400);
        mainLayout->addWidget(tbl);
    };

    makeTable(m_correlationTable, {"CAN ID","Bajt","Korelacja","p-value","Istotna?","Sygnał DBC"});
    m_significanceFilter = new QCheckBox("Tylko istotne statystycznie (p < 0.05)");
    m_significanceFilter->setStyleSheet("color: #ffaa00; font-weight: bold;");
    connect(m_significanceFilter, &QCheckBox::toggled, this, &AssociativeLearner::applySignificanceFilter);
    mainLayout->addWidget(m_significanceFilter);

    // Auto-discovery
    auto *autoDiscLayout = new QHBoxLayout;
    m_autoDiscoveryCheck = new QCheckBox("Auto-discovery: ciągłe odkrywanie predykcyjnych sygnałów");
    m_autoDiscoveryCheck->setStyleSheet("color: #00ffaa; font-weight: bold; font-size: 13px;");
    autoDiscLayout->addWidget(m_autoDiscoveryCheck);
    autoDiscLayout->addStretch();
    m_autoDiscoveryLabel = new QLabel("Nieaktywne");
    m_autoDiscoveryLabel->setStyleSheet("color: #ffaa00;");
    autoDiscLayout->addWidget(m_autoDiscoveryLabel);
    mainLayout->addLayout(autoDiscLayout);
    m_autoDiscoveryTable = new QTableWidget(0, 5);
    m_autoDiscoveryTable->setHorizontalHeaderLabels({"CAN ID","Bajt","Korelacja","p-value","Opis"});
    m_autoDiscoveryTable->verticalHeader()->hide(); m_autoDiscoveryTable->horizontalHeader()->setStretchLastSection(true);
    m_autoDiscoveryTable->setShowGrid(false); m_autoDiscoveryTable->setAlternatingRowColors(false);
    m_autoDiscoveryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_autoDiscoveryTable->setMinimumHeight(250);
    mainLayout->addWidget(m_autoDiscoveryTable);
    m_autoDiscoveryTimer = new QTimer(this);
    m_autoDiscoveryTimer->setInterval(2000);
    connect(m_autoDiscoveryTimer, &QTimer::timeout, this, &AssociativeLearner::updateAutoDiscovery);
    connect(m_autoDiscoveryCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (on) m_autoDiscoveryTimer->start(); else { m_autoDiscoveryTimer->stop(); m_autoDiscoveryLabel->setText("Nieaktywne"); }
    });

    // Neural network prediction
    auto *nnLayout = new QHBoxLayout;
    m_trainNnBtn = new QPushButton("Trenuj sieć neuronową (MLP)");
    m_trainNnBtn->setStyleSheet("QPushButton { color: #ff66cc; font-size: 13px; }");
    nnLayout->addWidget(m_trainNnBtn);
    m_nnStatusLabel = new QLabel("Model: nie wytrenowany");
    m_nnStatusLabel->setStyleSheet("color: #ffaa00; font-weight: bold;");
    nnLayout->addWidget(m_nnStatusLabel);
    nnLayout->addStretch();
    m_nnPredictionLabel = new QLabel("");
    m_nnPredictionLabel->setStyleSheet("color: #00ffaa; font-size: 15px; font-weight: bold;");
    nnLayout->addWidget(m_nnPredictionLabel);
    mainLayout->addLayout(nnLayout);
    m_nnTimer = new QTimer(this);
    m_nnTimer->setInterval(1000);
    connect(m_nnTimer, &QTimer::timeout, this, &AssociativeLearner::updateNnPrediction);
    connect(m_trainNnBtn, &QPushButton::clicked, this, [this]() {
        runAsync([this]{
            // Train neural network via engine
            auto corr = m_engine.computeCorrelations(m_currentVariable.toStdString());
            auto result = m_engine.trainNeuralNetwork(corr, m_currentVariable.toStdString());
            if (result.trained) {
                m_nnStatusLabel->setText(QString::fromStdString(result.status));
                m_nnTimer->start();
            } else {
                m_nnStatusLabel->setText(QString::fromStdString(result.status));
            }
        }, m_trainNnBtn, "Trenuj sieć neuronową (MLP)");
    });
    addHLine();

    auto *seqLayout = new QHBoxLayout;
    seqLayout->addWidget(new QLabel("Długość sekwencji:"));
    m_ngramCombo = new QComboBox; m_ngramCombo->addItems({"Bigram (2)","Trigram (3)"});
    seqLayout->addWidget(m_ngramCombo); seqLayout->addStretch();
    mainLayout->addLayout(seqLayout);
    makeTable(m_sequenceTable, {"Sekwencja ID","Wystąpienia","Pewność"});
    addHLine();

    mainLayout->addWidget(new QLabel("Korelacja międzybajtowa"));
    makeTable(m_crossByteTable, {"ID1","Bajt1","ID2","Bajt2","Korelacja"});
    addHLine();

    auto *clusterLayout = new QHBoxLayout;
    clusterLayout->addWidget(new QLabel("Klastrowanie okien"));
    m_clusterBtn = new QPushButton("Uruchom k-means");
    clusterLayout->addWidget(m_clusterBtn); clusterLayout->addStretch();
    mainLayout->addLayout(clusterLayout);

    // ── DBSCAN clustering ──
    auto *dbscanLayout = new QHBoxLayout;
    dbscanLayout->addWidget(new QLabel("DBSCAN:"));
    m_dbscanEps = new QLineEdit("1.0"); m_dbscanEps->setMaximumWidth(50);
    m_dbscanEps->setPlaceholderText("eps");
    m_dbscanMinPts = new QLineEdit("3"); m_dbscanMinPts->setMaximumWidth(50);
    m_dbscanMinPts->setPlaceholderText("minPts");
    m_dbscanBtn = new QPushButton("Uruchom DBSCAN");
    dbscanLayout->addWidget(new QLabel("eps:")); dbscanLayout->addWidget(m_dbscanEps);
    dbscanLayout->addWidget(new QLabel("minPts:")); dbscanLayout->addWidget(m_dbscanMinPts);
    dbscanLayout->addWidget(m_dbscanBtn);
    dbscanLayout->addStretch();
    mainLayout->addLayout(dbscanLayout);
    m_dbscanTable = new QTableWidget(0, 4);
    m_dbscanTable->setHorizontalHeaderLabels({"Klaster","Śr. liczba ramek","Dominujące ID","Liczba okien"});
    m_dbscanTable->verticalHeader()->hide(); m_dbscanTable->horizontalHeader()->setStretchLastSection(true);
    m_dbscanTable->setShowGrid(false); m_dbscanTable->setAlternatingRowColors(false);
    m_dbscanTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dbscanTable->setMinimumHeight(200);
    mainLayout->addWidget(m_dbscanTable);
    connect(m_dbscanBtn, &QPushButton::clicked, this, &AssociativeLearner::dbscanClustering);

    auto *pcaLayout = new QHBoxLayout;
    pcaLayout->addWidget(new QLabel("PCA + k-means:"));
    m_pcaBtn = new QPushButton("Uruchom PCA i k-średnich");
    pcaLayout->addWidget(m_pcaBtn);
    pcaLayout->addStretch();
    mainLayout->addLayout(pcaLayout);
    m_pcaChart = new QChart();
    m_pcaChart->setTitle("Rzutowanie PCA (2 składowe)");
    m_pcaSeries = new QScatterSeries();
    m_pcaSeries->setName("Dane");
    m_pcaSeries->setMarkerSize(8.0);
    m_pcaSeries->setColor(QColor("#00ffaa"));
    m_pcaChart->addSeries(m_pcaSeries);
    m_pcaChart->createDefaultAxes();
    m_pcaChartView = new QChartView(m_pcaChart);
    m_pcaChartView->setViewport(new QOpenGLWidget());
    m_pcaChartView->setRenderHint(QPainter::Antialiasing);
    m_pcaChartView->setMinimumHeight(300);
    mainLayout->addWidget(m_pcaChartView);
    connect(m_pcaBtn, &QPushButton::clicked, this, &AssociativeLearner::runPcaClustering);
    makeTable(m_clusterTable, {"Klaster","Śr. liczba ramek","Dominujące ID","Liczba okien"});
    addHLine();

    // ── t-SNE (#41) ───────────────────────────────────────
    {
        auto *tsneHeader = new QHBoxLayout;
        tsneHeader->addWidget(new QLabel("t-SNE 2D:"));
        tsneHeader->addWidget(new QLabel("Perplexity:"));
        m_tsnePerplexityEdit = new QLineEdit("15"); m_tsnePerplexityEdit->setMaximumWidth(45);
        tsneHeader->addWidget(m_tsnePerplexityEdit);
        tsneHeader->addWidget(new QLabel("Iteracje:"));
        m_tsneIterEdit = new QLineEdit("500"); m_tsneIterEdit->setMaximumWidth(55);
        tsneHeader->addWidget(m_tsneIterEdit);
        m_tsneBtn = new QPushButton("Uruchom t-SNE");
        tsneHeader->addWidget(m_tsneBtn);
        tsneHeader->addStretch();
        mainLayout->addLayout(tsneHeader);

        m_tsneStatusLabel = new QLabel("Gotowy");
        m_tsneStatusLabel->setStyleSheet("color: #aaaaaa;");
        mainLayout->addWidget(m_tsneStatusLabel);

        m_tsneChart = new QChart();
        m_tsneChart->setTitle("Wizualizacja t-SNE (2D)");
        m_tsneChart->setTheme(QChart::ChartThemeDark);
        const QColor clusterColors[3] = {QColor("#00ffaa"), QColor("#e94560"), QColor("#f5a623")};
        for (int k = 0; k < 3; ++k) {
            m_tsneSeries[k] = new QScatterSeries();
            m_tsneSeries[k]->setName(QString("Klaster %1").arg(k+1));
            m_tsneSeries[k]->setMarkerSize(8.0);
            m_tsneSeries[k]->setColor(clusterColors[k]);
            m_tsneChart->addSeries(m_tsneSeries[k]);
        }
        m_tsneChart->createDefaultAxes();
        m_tsneChartView = new QChartView(m_tsneChart);
        m_tsneChartView->setViewport(new QOpenGLWidget());
        m_tsneChartView->setRenderHint(QPainter::Antialiasing);
        m_tsneChartView->setMinimumHeight(320);
        mainLayout->addWidget(m_tsneChartView);
        connect(m_tsneBtn, &QPushButton::clicked, this, [this]() {
            runAsync([this]{ runTsne(); }, m_tsneBtn, "Uruchom t-SNE");
        });
    }
    addHLine();

    auto *predLayout = new QHBoxLayout;
    predLayout->addWidget(new QLabel("Predykcja wartości"));
    m_trainPredictionBtn = new QPushButton("Trenuj predykcję");
    predLayout->addWidget(m_trainPredictionBtn); predLayout->addStretch();
    mainLayout->addLayout(predLayout);
    makeTable(m_predictionTable, {"CAN ID","Bajt","Wsp. kier. (a)","Wyraz wolny (b)","Bieżąca prognoza"});

    m_predictionTimer = new QTimer(this);
    connect(m_predictionTimer, &QTimer::timeout, this, &AssociativeLearner::updatePredictionDisplay);
    addHLine();

    auto *anomalyLayout = new QHBoxLayout;
    m_anomalyToggleBtn = new QPushButton("▶ Rozpocznij monitorowanie anomalii");
    m_autoEventCheck = new QCheckBox("Auto-wykrywanie zdarzeń");
    m_autoEventThreshold = new QLineEdit("0.1");
    m_autoEventThreshold->setMaximumWidth(60);
    m_autoEventLabel = new QLabel("Próg gradientu:");
    auto *autoLayout = new QHBoxLayout;
    autoLayout->addWidget(m_autoEventCheck);
    autoLayout->addWidget(m_autoEventLabel);
    autoLayout->addWidget(m_autoEventThreshold);
    mainLayout->addLayout(autoLayout);
    m_autoEventTimer = new QTimer(this);
    connect(m_autoEventCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) m_autoEventTimer->start(500);
        else m_autoEventTimer->stop();
    });
    connect(m_autoEventTimer, &QTimer::timeout, this, &AssociativeLearner::checkAutoEvent);
    m_anomalyThreshold = new QLineEdit("10.0"); m_anomalyThreshold->setMaximumWidth(60);
    anomalyLayout->addWidget(m_anomalyToggleBtn);
    anomalyLayout->addWidget(new QLabel("Próg:")); anomalyLayout->addWidget(m_anomalyThreshold);
    mainLayout->addLayout(anomalyLayout);
    makeTable(m_anomalyTable, {"Czas (s)", "Wynik", "Opis"});

    m_anomalyTimer = new QTimer(this);
    connect(m_anomalyTimer, &QTimer::timeout, this, &AssociativeLearner::checkAnomaly);
    connect(m_anomalyToggleBtn, &QPushButton::clicked, this, [this]() {
        if (m_monitoring) stopAnomalyMonitoring(); else startAnomalyMonitoring();
    });

    // --- Automatyczny dobór K (łokieć) ---
    auto *autoKLayout = new QHBoxLayout;
    autoKLayout->addWidget(new QLabel("Auto K (łokieć):"));
    m_autoKBtn = new QPushButton("Znajdź optymalne K");
    autoKLayout->addWidget(m_autoKBtn);
    autoKLayout->addStretch();
    mainLayout->addLayout(autoKLayout);
    m_elbowChart = new QChart();
    m_elbowChart->setTitle("Metoda łokcia (WCSS)");
    m_elbowSeries = new QLineSeries();
    m_elbowSeries->setName("WCSS");
    m_elbowChart->addSeries(m_elbowSeries);
    m_elbowChart->createDefaultAxes();
    m_elbowChartView = new QChartView(m_elbowChart);
    m_elbowChartView->setViewport(new QOpenGLWidget());
    m_elbowChartView->setRenderHint(QPainter::Antialiasing);
    m_elbowChartView->setMinimumHeight(250);
    mainLayout->addWidget(m_elbowChartView);
    connect(m_autoKBtn, &QPushButton::clicked, this, [this]() {
        runAsync([this]{ autoKMeans(); }, m_autoKBtn, "Znajdź optymalne K");
    });
    m_chart = new QChart(); m_chart->setTitle("Wartość od bajtu");

    // --- Predykcja sekwencji (Markov) ---
    auto *markovLayout = new QHBoxLayout;
    markovLayout->addWidget(new QLabel("Predykcja następnej ramki:"));
    m_trainMarkovBtn = new QPushButton("Trenuj model Markowa");
    markovLayout->addWidget(m_trainMarkovBtn);
    markovLayout->addStretch();
    mainLayout->addLayout(markovLayout);
    m_markovTable = new QTableWidget(0,3);
    m_markovTable->setHorizontalHeaderLabels({"Ostatnie ID","Przewidywane ID","Prawdopodobieństwo"});
    m_markovTable->verticalHeader()->hide(); m_markovTable->horizontalHeader()->setStretchLastSection(true);
    m_markovTable->setShowGrid(false); m_markovTable->setAlternatingRowColors(false);
    m_markovTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_markovTable->setMinimumHeight(200);
    mainLayout->addWidget(m_markovTable);
    m_markovTimer = new QTimer(this);

    // --- Macierz korelacji zmiennych ---
    auto *crossVarLayout = new QHBoxLayout;
    crossVarLayout->addWidget(new QLabel("Macierz korelacji zmiennych:"));
    m_crossVarBtn = new QPushButton("Pokaż macierz korelacji zmiennych");
    crossVarLayout->addWidget(m_crossVarBtn);
    crossVarLayout->addStretch();
    mainLayout->addLayout(crossVarLayout);
    m_crossVarTable = new QTableWidget(0,0);
    m_crossVarTable->verticalHeader()->hide();
    m_crossVarTable->horizontalHeader()->setStretchLastSection(true);
    m_crossVarTable->setShowGrid(false);
    m_crossVarTable->setMinimumHeight(500);
    m_crossVarTable->setAlternatingRowColors(false);
    m_crossVarTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_crossVarTable->setMinimumHeight(300);
    mainLayout->addWidget(m_crossVarTable);
    connect(m_crossVarBtn, &QPushButton::clicked, this, [this]() {
        runAsync([this]{ updateCrossVariableMatrix(); }, m_crossVarBtn, "Pokaż macierz korelacji zmiennych");
    });

    // --- Mutual Information ---
    auto *miLayout = new QHBoxLayout;
    miLayout->addWidget(new QLabel("Zależności nieliniowe (MI):"));
    m_miBtn = new QPushButton("Oblicz Mutual Information");
    miLayout->addWidget(m_miBtn);
    miLayout->addStretch();
    mainLayout->addLayout(miLayout);
    m_miTable = new QTableWidget(0,4);
    m_miTable->setHorizontalHeaderLabels({"CAN ID","Bajt","MI (nat)","Porównanie"});
    m_miTable->verticalHeader()->hide(); m_miTable->horizontalHeader()->setStretchLastSection(true);
    m_miTable->setShowGrid(false); m_miTable->setAlternatingRowColors(false);
    m_miTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_miTable->setMinimumHeight(500);

    // --- Maximal Information Coefficient ---
    auto *micLayout = new QHBoxLayout;
    micLayout->addWidget(new QLabel("Maximal Information Coefficient (MIC):"));
    m_micBtn = new QPushButton("Oblicz MIC");
    micLayout->addWidget(m_micBtn);
    micLayout->addStretch();
    mainLayout->addLayout(micLayout);
    m_micTable = new QTableWidget(0,3);
    m_micTable->setHorizontalHeaderLabels({"CAN ID","Bajt","MIC"});
    m_micTable->verticalHeader()->hide(); m_micTable->horizontalHeader()->setStretchLastSection(true);
    m_micTable->setShowGrid(false); m_micTable->setAlternatingRowColors(false);
    m_micTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_micTable->setMinimumHeight(500);
    mainLayout->addWidget(m_micTable);
    connect(m_micBtn, &QPushButton::clicked, this, &AssociativeLearner::computeMIC);
    mainLayout->addWidget(m_miTable);
    connect(m_miBtn, &QPushButton::clicked, this, [this]() {
        runAsync([this]{ computeMutualInformation(); }, m_miBtn, "Oblicz Mutual Information");
    });
    connect(m_markovTimer, &QTimer::timeout, this, &AssociativeLearner::predictNextFrames);
    connect(m_trainMarkovBtn, &QPushButton::clicked, this, [this]() {
        trainMarkovModel();
        auto entries = m_engine.predictNextFrames();
        if (!entries.empty()) m_markovTimer->start(1000);
    });

    m_scatterSeries = new QScatterSeries(); m_scatterSeries->setMarkerSize(8.0); m_scatterSeries->setColor(QColor("#00ffaa"));
    m_chart->addSeries(m_scatterSeries); m_chart->createDefaultAxes();
    m_chartView = new QChartView(m_chart); m_chartView->setViewport(new QOpenGLWidget()); m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setMinimumHeight(300);
    mainLayout->addWidget(m_chartView);

    // --- Wykres czasowy (zmienna + bajty CAN w czasie) ---
    addHLine();
    auto *timeChartLayout = new QHBoxLayout;
    timeChartLayout->addWidget(new QLabel("Wykres w czasie:"));
    m_timeIdCombo = new QComboBox; m_timeIdCombo->setMinimumWidth(120);
    m_timeByteCombo = new QComboBox; m_timeByteCombo->setMinimumWidth(80);
    for (int b = 0; b < 64; ++b) m_timeByteCombo->addItem(QString("Bajt %1").arg(b), b);
    timeChartLayout->addWidget(m_timeIdCombo);
    timeChartLayout->addWidget(m_timeByteCombo);
    timeChartLayout->addStretch();
    mainLayout->addLayout(timeChartLayout);

    m_timeChart = new QChart();
    m_timeChart->setTitle("Zmienna i bajt CAN w czasie");
    m_varTimeSeries = new QLineSeries(); m_varTimeSeries->setName("Zmienna"); m_varTimeSeries->setColor(QColor("#00ffaa"));
    m_byteTimeSeries = new QLineSeries(); m_byteTimeSeries->setName("Bajt CAN"); m_byteTimeSeries->setColor(QColor("#ff66cc"));
    m_timeChart->addSeries(m_varTimeSeries);
    m_timeChart->addSeries(m_byteTimeSeries);
    m_timeChart->createDefaultAxes();
    m_timeChartView = new QChartView(m_timeChart); m_timeChartView->setViewport(new QOpenGLWidget()); m_timeChartView->setRenderHint(QPainter::Antialiasing);
    m_timeChartView->setMinimumHeight(300);
    mainLayout->addWidget(m_timeChartView);

    connect(m_timeIdCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int) { updateTimeChart(); });
    connect(m_timeByteCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int) { updateTimeChart(); });

    // --- Auto-save ---
    auto *autoSaveLayout = new QHBoxLayout;
    m_autoSaveCheck = new QCheckBox("Auto-zapis co 5 min");
    m_autoSaveCheck->setStyleSheet("color: #00ffaa; font-weight: bold;");
    autoSaveLayout->addWidget(m_autoSaveCheck);
    autoSaveLayout->addStretch();
    mainLayout->addLayout(autoSaveLayout);

    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setInterval(5 * 60 * 1000);
    connect(m_autoSaveTimer, &QTimer::timeout, this, &AssociativeLearner::autoSave);
    connect(m_autoSaveCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (on) m_autoSaveTimer->start();
        else m_autoSaveTimer->stop();
    });
    m_autoSavePath = QDir::currentPath() + "/autosave_learner.json";
    // ── Zaawansowana analiza: Granger causality ──────────
    addHLine();
    auto *grangerLayout = new QHBoxLayout;
    grangerLayout->addWidget(new QLabel("Przyczynowosc Granger:"));
    m_grangerBtn = new QPushButton("Test przyczynowosci Granger");
    m_grangerBtn->setStyleSheet("QPushButton { color: #ff66cc; font-size: 13px; }");
    grangerLayout->addWidget(m_grangerBtn);
    grangerLayout->addStretch();
    mainLayout->addLayout(grangerLayout);
    m_grangerTable = new QTableWidget(0, 5);
    m_grangerTable->setHorizontalHeaderLabels({"CAN ID","Bajt","F-stat","p-value","Przyczynowy?"});
    m_grangerTable->verticalHeader()->hide(); m_grangerTable->horizontalHeader()->setStretchLastSection(true);
    m_grangerTable->setShowGrid(false); m_grangerTable->setAlternatingRowColors(false);
    m_grangerTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_grangerTable->setMinimumHeight(200);
    mainLayout->addWidget(m_grangerTable);
    connect(m_grangerBtn, &QPushButton::clicked, this, [this]() {
        runAsync([this]{ runGrangerCausality(); }, m_grangerBtn, "Test przyczynowosci Granger");
    });

    // ── Change-point detection ────────────────────────────
    auto *cpLayout = new QHBoxLayout;
    cpLayout->addWidget(new QLabel("Punkty zmiany: ID:"));
    m_cpIdCombo = new QComboBox; m_cpIdCombo->setMinimumWidth(100);
    m_cpIdCombo->setPlaceholderText("Wybierz ID...");
    cpLayout->addWidget(m_cpIdCombo);
    m_cpByteCombo = new QComboBox; m_cpByteCombo->setMinimumWidth(80);
    for (int b = 0; b < 8; ++b) m_cpByteCombo->addItem(QString("Bajt %1").arg(b), b);
    cpLayout->addWidget(m_cpByteCombo);
    m_changePointBtn = new QPushButton("Wykryj punkty zmiany");
    cpLayout->addWidget(m_changePointBtn);
    cpLayout->addStretch();
    mainLayout->addLayout(cpLayout);
    m_changePointTable = new QTableWidget(0, 4);
    m_changePointTable->setHorizontalHeaderLabels({"Indeks","Redukcja kosztu","Srednia przed","Srednia po"});
    m_changePointTable->verticalHeader()->hide(); m_changePointTable->horizontalHeader()->setStretchLastSection(true);
    m_changePointTable->setShowGrid(false); m_changePointTable->setAlternatingRowColors(false);
    m_changePointTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_changePointTable->setMinimumHeight(200);
    mainLayout->addWidget(m_changePointTable);
    connect(m_changePointBtn, &QPushButton::clicked, this, [this]() {
        runAsync([this]{ runChangePointDetection(); }, m_changePointBtn, "Wykryj punkty zmiany");
    });

    // ── Cross-correlation lag ─────────────────────────────
    auto *lagLayout = new QHBoxLayout;
    lagLayout->addWidget(new QLabel("Korelacja z przesunieciem:"));
    m_lagCorrBtn = new QPushButton("Oblicz korelacje z lagiem");
    lagLayout->addWidget(m_lagCorrBtn);
    lagLayout->addStretch();
    mainLayout->addLayout(lagLayout);
    m_lagCorrTable = new QTableWidget(0, 4);
    m_lagCorrTable->setHorizontalHeaderLabels({"CAN ID","Bajt","Lag","Korelacja"});
    m_lagCorrTable->verticalHeader()->hide(); m_lagCorrTable->horizontalHeader()->setStretchLastSection(true);
    m_lagCorrTable->setShowGrid(false); m_lagCorrTable->setAlternatingRowColors(false);
    m_lagCorrTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_lagCorrTable->setMinimumHeight(200);
    mainLayout->addWidget(m_lagCorrTable);
    connect(m_lagCorrBtn, &QPushButton::clicked, this, [this]() {
        runAsync([this]{ runCrossCorrelationLag(); }, m_lagCorrBtn, "Oblicz korelacje z lagiem");
    });

    // ── GBT (Gradient Boosted Trees) ──────────────────────
    addHLine();
    auto *gbtLayout = new QHBoxLayout;
    gbtLayout->addWidget(new QLabel("Gradient Boosted Trees:"));
    m_gbtBtn = new QPushButton("Trenuj GBT");
    m_gbtBtn->setStyleSheet("QPushButton { color: #ff66cc; font-size: 13px; }");
    gbtLayout->addWidget(m_gbtBtn);
    m_gbtStatusLabel = new QLabel("Model: nie wytrenowany");
    m_gbtStatusLabel->setStyleSheet("color: #ffaa00; font-weight: bold;");
    gbtLayout->addWidget(m_gbtStatusLabel);
    gbtLayout->addStretch();
    mainLayout->addLayout(gbtLayout);
    m_gbtPredTable = new QTableWidget(0, 3);
    m_gbtPredTable->setHorizontalHeaderLabels({"CAN ID","Bajt","Predykcja GBT"});
    m_gbtPredTable->verticalHeader()->hide(); m_gbtPredTable->horizontalHeader()->setStretchLastSection(true);
    m_gbtPredTable->setShowGrid(false); m_gbtPredTable->setAlternatingRowColors(false);
    m_gbtPredTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_gbtPredTable->setMinimumHeight(200);
    mainLayout->addWidget(m_gbtPredTable);
    connect(m_gbtBtn, &QPushButton::clicked, this, [this]() {
        runAsync([this]{ trainGbtModel(); }, m_gbtBtn, "Trenuj GBT");
    });

    // ── Online learning checkbox ──────────────────────────
    auto *onlineLayout = new QHBoxLayout;
    m_onlineLearningCheck = new QCheckBox("Online learning (Welford)");
    m_onlineLearningCheck->setStyleSheet("color: #00ffaa; font-weight: bold;");
    onlineLayout->addWidget(m_onlineLearningCheck);
    onlineLayout->addStretch();
    mainLayout->addLayout(onlineLayout);
    connect(m_onlineLearningCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_engine.setOnlineLearning(on);
    });

    // ── EWMA anomaly checkbox ─────────────────────────────
    auto *ewmaLayout = new QHBoxLayout;
    m_ewmaAnomalyCheck = new QCheckBox("Anomalie EWMA (adaptacyjne)");
    m_ewmaAnomalyCheck->setStyleSheet("color: #ffaa00; font-weight: bold;");
    ewmaLayout->addWidget(m_ewmaAnomalyCheck);
    ewmaLayout->addStretch();
    mainLayout->addLayout(ewmaLayout);
    connect(m_ewmaAnomalyCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_engine.setEwmaAnomaly(on);
    });

    // ── Cyclic noise filter checkbox ──────────────────────
    auto *noiseFilterLayout = new QHBoxLayout;
    m_noiseFilterCheck = new QCheckBox("Filtr zaszumienia cyklicznego (bity 0↔1)");
    m_noiseFilterCheck->setChecked(true);
    m_noiseFilterCheck->setStyleSheet("color: #ff6688; font-weight: bold;");
    noiseFilterLayout->addWidget(m_noiseFilterCheck);
    noiseFilterLayout->addStretch();
    mainLayout->addLayout(noiseFilterLayout);
    connect(m_noiseFilterCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_engine.setNoiseFilterEnabled(on);
    });

    // ── FFT / analiza częstotliwości ──
    addHLine();
    auto *fftHeader = new QHBoxLayout;
    fftHeader->addWidget(new QLabel("Analiza częstotliwości (FFT):"));
    m_fftIdCombo = new QComboBox; m_fftIdCombo->setMinimumWidth(120);
    m_fftIdCombo->setPlaceholderText("Wybierz CAN ID...");
    m_fftByteCombo = new QComboBox; m_fftByteCombo->setMinimumWidth(80);
    for (int b = 0; b < 64; ++b) m_fftByteCombo->addItem(QString("Bajt %1").arg(b), b);
    m_fftBtn = new QPushButton("Uruchom FFT");
    fftHeader->addWidget(m_fftIdCombo);
    fftHeader->addWidget(m_fftByteCombo);
    fftHeader->addWidget(m_fftBtn);
    fftHeader->addStretch();
    mainLayout->addLayout(fftHeader);

    m_fftChart = new QChart();
    m_fftChart->setTitle("Widmo częstotliwości (FFT)");
    m_fftSeries = new QLineSeries(); m_fftSeries->setName("Magnituda"); m_fftSeries->setColor(QColor("#e94560"));
    m_fftChart->addSeries(m_fftSeries);
    m_fftChart->createDefaultAxes();
    m_fftChart->axes(Qt::Horizontal).first()->setTitleText("Częstotliwość (Hz)");
    m_fftChart->axes(Qt::Vertical).first()->setTitleText("|Amplituda|");
    m_fftChartView = new QChartView(m_fftChart);
    m_fftChartView->setViewport(new QOpenGLWidget());
    m_fftChartView->setRenderHint(QPainter::Antialiasing);
    m_fftChartView->setMinimumHeight(300);
    mainLayout->addWidget(m_fftChartView);

    m_fftPeakTable = new QTableWidget(0, 4);
    m_fftPeakTable->setHorizontalHeaderLabels({"Częstotliwość (Hz)","Okres (ms)","|Amplituda|","Opis"});
    m_fftPeakTable->verticalHeader()->hide(); m_fftPeakTable->horizontalHeader()->setStretchLastSection(true);
    m_fftPeakTable->setShowGrid(false); m_fftPeakTable->setAlternatingRowColors(false);
    m_fftPeakTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fftPeakTable->setMinimumHeight(200);
    mainLayout->addWidget(m_fftPeakTable);

    connect(m_fftBtn, &QPushButton::clicked, this, [this]() {
        runAsync([this]{ runFftAnalysis(); }, m_fftBtn, "Uruchom FFT");
    });

    auto *serLayout = new QHBoxLayout;
    m_saveBtn = new QPushButton("💾 Zapisz sesję"); m_loadBtn = new QPushButton("📂 Wczytaj sesję");
    m_exportModelsBtn = new QPushButton("📤 Eksportuj modele");
    m_importModelsBtn = new QPushButton("📥 Importuj modele");
    m_generateLuaBtn = new QPushButton("📝 Generuj skrypt Lua");
    m_exportReportBtn = new QPushButton("📄 Eksportuj raport HTML");
    serLayout->addWidget(m_saveBtn); serLayout->addWidget(m_loadBtn);
    serLayout->addWidget(m_exportModelsBtn); serLayout->addWidget(m_importModelsBtn);
    serLayout->addWidget(m_generateLuaBtn);
    serLayout->addWidget(m_exportReportBtn);
    mainLayout->addLayout(serLayout);

    connect(m_markEventBtn, &QPushButton::clicked, this, &AssociativeLearner::markEvent);
    connect(m_markNonEventBtn, &QPushButton::clicked, this, &AssociativeLearner::markNonEvent);
    connect(m_resetBtn, &QPushButton::clicked, this, &AssociativeLearner::resetLearning);
    connect(m_addObsBtn, &QPushButton::clicked, this, &AssociativeLearner::addObservation);
    connect(m_saveBtn, &QPushButton::clicked, this, &AssociativeLearner::saveSession);
    connect(m_loadBtn, &QPushButton::clicked, this, &AssociativeLearner::loadSession);
    connect(m_exportModelsBtn, &QPushButton::clicked, this, &AssociativeLearner::exportModels);
    connect(m_importModelsBtn, &QPushButton::clicked, this, &AssociativeLearner::importModels);
    connect(m_generateLuaBtn, &QPushButton::clicked, this, &AssociativeLearner::generateLuaScript);
    connect(m_exportReportBtn, &QPushButton::clicked, this, &AssociativeLearner::exportHtmlReport);
    connect(m_ngramCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int) { updateSequenceTable(); });
    connect(m_clusterBtn, &QPushButton::clicked, this, [this]() {
        runAsync([this]{ clusterWindows(); }, m_clusterBtn, "Uruchom k-means");
    });
    connect(m_trainPredictionBtn, &QPushButton::clicked, this, [this]() {
        runAsync([this]{ trainPrediction(); }, m_trainPredictionBtn, "Trenuj predykcję");
    });
    connect(m_addVariableBtn, &QPushButton::clicked, this, &AssociativeLearner::addNewVariable);
    connect(m_variableCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AssociativeLearner::onVariableChanged);

    addVariable("temperatura");
}

// ── Async helper (QtConcurrent) ──────────────────────────────

template<typename Func>
void AssociativeLearner::runAsync(Func &&compute, QPushButton *btn, const QString &restoreText) {
    btn->setEnabled(false);
    btn->setText("Obliczanie...");
    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [btn, restoreText, watcher]() {
        btn->setEnabled(true);
        btn->setText(restoreText);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(std::forward<Func>(compute)));
}

AssociativeLearner::~AssociativeLearner() {
    m_predictionTimer->stop();
    m_anomalyTimer->stop();
    m_autoSaveTimer->stop();
    if (m_autoSaveCheck->isChecked())
        autoSave();
}

void AssociativeLearner::addVariable(const QString &name) {
    QString key = name.toLower().trimmed();
    if (key.isEmpty()) return;
    std::string skey = key.toStdString();
    if (m_engine.hasVariable(skey)) return;
    m_engine.addVariable(skey);
    m_variableCombo->addItem(name, key);
    if (m_variableCombo->count() == 1) {
        m_variableCombo->setCurrentIndex(0);
        m_currentVariable = key;
    }
}

void AssociativeLearner::addNewVariable() {
    QString name = m_newVariableName->text().trimmed();
    if (!name.isEmpty()) { addVariable(name); m_newVariableName->clear(); }
}

void AssociativeLearner::onVariableChanged(int idx) {
    if (idx >= 0) {
        m_currentVariable = m_variableCombo->itemData(idx).toString();
        updateCorrelationTable();
        updateCrossByteTable();
        updateChart();
    }
    if (m_autoEventCheck->isChecked()) checkAutoEvent();
}

void AssociativeLearner::processFrame(const CanFrame &frame) {
    m_engine.processFrame(frame);
}

void AssociativeLearner::markEvent() {
    m_engine.markEvent(m_engine.adaptiveBefore(), m_engine.adaptiveAfter());
    int iter = m_engine.iterationCount();
    m_iterationLabel->setText(QString("Liczba iteracji: %1").arg(iter));
    emit eventMarked(iter);
    updateCandidates();
    updateSequenceTable();
}

void AssociativeLearner::markNonEvent() {
    m_engine.markNonEvent(m_engine.adaptiveBefore(), m_engine.adaptiveAfter());
}

void AssociativeLearner::resetLearning() {
    m_engine.resetLearning();
    m_iterationLabel->setText("Liczba iteracji: 0");
    m_candidateModel->clear();
    m_correlationTable->setRowCount(0);
    m_sequenceTable->setRowCount(0);
    m_crossByteTable->setRowCount(0);
    m_clusterTable->setRowCount(0);
    m_predictionTable->setRowCount(0);
    m_predictionTimer->stop();
    stopAnomalyMonitoring();
    m_anomalyTable->setRowCount(0);
    m_variableCombo->clear();
    m_currentVariable.clear();
    m_scatterSeries->clear();
    addVariable("temperatura");
}

void AssociativeLearner::addObservation() {
    if (m_currentVariable.isEmpty()) return;
    bool ok;
    double v = m_valueInput->text().toDouble(&ok);
    if (!ok) {
        m_iterationLabel->setText("Blad: wpisz liczbe (np. 25.5)");
        return;
    }

    m_engine.addObservation(m_currentVariable.toStdString(), v,
                            m_engine.adaptiveBefore(), m_engine.adaptiveAfter());
    m_valueInput->clear();

    // Show observation count
    auto obs = m_engine.observations(m_currentVariable.toStdString());
    m_iterationLabel->setText(QString("%1: %2 obs. (ostatnia: %3)")
        .arg(m_currentVariable)
        .arg(obs.size())
        .arg(v, 0, 'f', 1));

    updateCorrelationTable();
    updateCrossByteTable();
    updateChart();
    updateTimeChart();
    if (m_autoEventCheck->isChecked()) checkAutoEvent();
}

// ── Candidate ranking (UI wrapper) ──────────────────────────

void AssociativeLearner::updateCandidates() {
    auto cands = m_engine.computeCandidates();
    if (cands.empty()) { m_candidateModel->clear(); return; }

    QVector<Candidate> qtCands;
    for (const auto &c : cands) {
        QString desc = QString::fromStdString(c.description);
        // Enrich with DBC/J1939
        if (m_dbc) {
            DbcMessage dm = m_dbc->messageForId(c.canId);
            if (dm.id != 0) {
                desc = dm.name;
                if (!dm.sigList.isEmpty())
                    desc += " (" + dm.sigList.first().name + "...)";
            }
        }
        if (m_j1939 && (c.canId & 0x80000000)) {
            uint32_t pf = (c.canId >> 16) & 0xFF;
            uint32_t ps = (c.canId >> 8) & 0xFF;
            uint32_t dp = (c.canId >> 24) & 0x1;
            uint32_t r  = (c.canId >> 25) & 0x1;
            uint32_t pgn = (r << 17) | (dp << 16) | (pf << 8) | (pf < 240 ? ps : 0);
            desc += " | " + m_j1939->pgnName(pgn);
        }
        qtCands.append({c.canId, desc, c.score, c.occurrences});
    }
    m_candidateModel->setCandidates(qtCands);
}

// ── Correlation table (Pearson) UI ─────────────────────────

void AssociativeLearner::updateCorrelationTable() {
    auto obs = m_engine.observations(m_currentVariable.toStdString());
    auto entries = m_engine.computeCorrelations(m_currentVariable.toStdString());
    m_correlationTable->setRowCount(static_cast<int>(entries.size()));

    // Show diagnostic info
    if (obs.size() >= 3 && entries.empty()) {
        m_iterationLabel->setText(m_iterationLabel->text() +
            " | Brak wspolnych ID miedzy obserwacjami!");
    }
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const auto &e = entries[i];
        m_correlationTable->setItem(i, 0, new QTableWidgetItem(
            QString("0x%1").arg(e.id, 3, 16, QChar('0')).toUpper()));
        m_correlationTable->setItem(i, 1, new QTableWidgetItem(QString::number(e.byte)));
        m_correlationTable->setItem(i, 2, new QTableWidgetItem(
            QString::number(e.correlation, 'f', 3)));
        auto *pItem = new QTableWidgetItem(QString::number(e.pValue, 'e', 2));
        m_correlationTable->setItem(i, 3, pItem);
        auto *sigItem = new QTableWidgetItem(e.significant ? "TAK" : "nie");
        sigItem->setForeground(e.significant ? QColor("#00ffaa") : QColor("#ff66cc"));
        m_correlationTable->setItem(i, 4, sigItem);
        // DBC signal name
        QString sigName;
        if (m_dbc) {
            DbcMessage dm = m_dbc->messageForId(e.id);
            if (dm.id != 0) {
                for (const auto &sig : dm.sigList) {
                    int startBit = sig.startBit;
                    if (e.byte >= startBit / 8 && e.byte < (startBit + sig.length + 7) / 8) {
                        sigName = sig.name;
                        break;
                    }
                }
            }
        }
        m_correlationTable->setItem(i, 5, new QTableWidgetItem(sigName));
    }
    applySignificanceFilter();
}

void AssociativeLearner::applySignificanceFilter() {
    bool filter = m_significanceFilter->isChecked();
    for (int i = 0; i < m_correlationTable->rowCount(); ++i) {
        auto *pItem = m_correlationTable->item(i, 3);
        if (!pItem) continue;
        double pv = pItem->text().toDouble();
        m_correlationTable->setRowHidden(i, filter && pv >= 0.05);
    }
}

// ── Sequences ───────────────────────────────────────────────

void AssociativeLearner::updateSequenceTable() {
    int ngram = m_ngramCombo->currentIndex() == 0 ? 2 : 3;
    auto entries = m_engine.computeSequences(ngram);
    m_sequenceTable->setRowCount(static_cast<int>(entries.size()));
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        m_sequenceTable->setItem(i, 0, new QTableWidgetItem(
            QString::fromStdString(entries[i].sequence)));
        m_sequenceTable->setItem(i, 1, new QTableWidgetItem(
            QString::number(entries[i].occurrences)));
        m_sequenceTable->setItem(i, 2, new QTableWidgetItem(
            QString::number(entries[i].confidence, 'f', 2)));
    }
}

// ── Cross-byte correlation ─────────────────────────────────

void AssociativeLearner::updateCrossByteTable() {
    auto entries = m_engine.computeCrossByte(m_currentVariable.toStdString());
    m_crossByteTable->setRowCount(static_cast<int>(entries.size()));
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        m_crossByteTable->setItem(i, 0, new QTableWidgetItem(
            QString("0x%1").arg(entries[i].id1, 3, 16, QChar('0')).toUpper()));
        m_crossByteTable->setItem(i, 1, new QTableWidgetItem(
            QString::number(entries[i].byte1)));
        m_crossByteTable->setItem(i, 2, new QTableWidgetItem(
            QString("0x%1").arg(entries[i].id2, 3, 16, QChar('0')).toUpper()));
        m_crossByteTable->setItem(i, 3, new QTableWidgetItem(
            QString::number(entries[i].byte2)));
        m_crossByteTable->setItem(i, 4, new QTableWidgetItem(
            QString::number(entries[i].correlation, 'f', 3)));
    }
}

// ── Clustering ──────────────────────────────────────────────

void AssociativeLearner::clusterWindows() {
    auto stats = m_engine.clusterWindows(3);
    m_clusterTable->setRowCount(static_cast<int>(stats.size()));
    for (int c = 0; c < static_cast<int>(stats.size()); ++c) {
        m_clusterTable->setItem(c, 0, new QTableWidgetItem(
            QString("Klaster %1").arg(stats[c].clusterId)));
        m_clusterTable->setItem(c, 1, new QTableWidgetItem(
            QString::number(stats[c].avgFrameCount, 'f', 1)));
        QStringList top;
        for (auto id : stats[c].dominantIds)
            top.append(QString("0x%1").arg(id, 3, 16, QChar('0')).toUpper());
        m_clusterTable->setItem(c, 2, new QTableWidgetItem(top.join(", ")));
        m_clusterTable->setItem(c, 3, new QTableWidgetItem(
            QString::number(stats[c].windowCount)));
    }
}

void AssociativeLearner::dbscanClustering() {
    bool ok;
    float eps = m_dbscanEps->text().toFloat(&ok);
    if (!ok || eps <= 0) { eps = 1.0f; m_dbscanEps->setText("1.0"); }
    int minPts = m_dbscanMinPts->text().toInt(&ok);
    if (!ok || minPts < 2) { minPts = 3; m_dbscanMinPts->setText("3"); }

    auto stats = m_engine.dbscanClustering(eps, minPts);
    int noiseCnt = 0;
    int K = 0;
    for (const auto &s : stats) {
        if (s.clusterId < 0) noiseCnt = s.windowCount;
        else K = std::max(K, s.clusterId);
    }

    int rows = K + (noiseCnt > 0 ? 1 : 0);
    m_dbscanTable->setRowCount(rows);
    for (int c = 0; c < K; ++c) {
        m_dbscanTable->setItem(c, 0, new QTableWidgetItem(
            QString("Klaster %1").arg(stats[c].clusterId)));
        m_dbscanTable->setItem(c, 1, new QTableWidgetItem(
            QString::number(stats[c].avgFrameCount, 'f', 1)));
        QStringList top;
        for (auto id : stats[c].dominantIds)
            top.append(QString("0x%1").arg(id, 3, 16, QChar('0')).toUpper());
        m_dbscanTable->setItem(c, 2, new QTableWidgetItem(top.join(", ")));
        m_dbscanTable->setItem(c, 3, new QTableWidgetItem(
            QString::number(stats[c].windowCount)));
    }
    if (noiseCnt > 0) {
        int r = K;
        m_dbscanTable->setItem(r, 0, new QTableWidgetItem("Szum"));
        m_dbscanTable->setItem(r, 1, new QTableWidgetItem("-"));
        m_dbscanTable->setItem(r, 2, new QTableWidgetItem("-"));
        m_dbscanTable->setItem(r, 3, new QTableWidgetItem(
            QString::number(noiseCnt)));
    }
}

void AssociativeLearner::autoKMeans() {
    auto wcssHistory = m_engine.autoKMeans(10);
    m_elbowSeries->clear();
    for (const auto &p : wcssHistory)
        m_elbowSeries->append(p.k, p.wcss);

    // Find best K (elbow)
    if (wcssHistory.size() >= 3) {
        double bestCurvature = 0.0;
        for (size_t i = 1; i < wcssHistory.size() - 1; ++i) {
            double curvature = wcssHistory[i-1].wcss + wcssHistory[i+1].wcss
                             - 2 * wcssHistory[i].wcss;
            if (curvature > bestCurvature) bestCurvature = curvature;
        }
        clusterWindows();
    }
}

void AssociativeLearner::runPcaClustering() {
    auto result = m_engine.runPcaClustering();
    if (result.projected.empty()) return;

    m_pcaSeries->clear();
    for (const auto &pt : result.projected)
        m_pcaSeries->append(pt.first, pt.second);
}

void AssociativeLearner::runTsne() {
    int perplexity = m_tsnePerplexityEdit->text().toInt();
    int maxIter    = m_tsneIterEdit->text().toInt();
    if (perplexity < 2)  perplexity = 2;
    if (perplexity > 50) perplexity = 50;
    if (maxIter < 50)    maxIter = 50;
    if (maxIter > 1000)  maxIter = 1000;

    auto result = m_engine.runTsne(perplexity, maxIter);

    QMetaObject::invokeMethod(this, [this, result]() {
        for (int k = 0; k < 3; ++k) m_tsneSeries[k]->clear();

        int N = static_cast<int>(result.projected.size());
        for (int i = 0; i < N; ++i) {
            int cluster = (i < (int)result.clusterAssignments.size())
                          ? std::clamp(result.clusterAssignments[i], 0, 2)
                          : 0;
            m_tsneSeries[cluster]->append(result.projected[i].first,
                                          result.projected[i].second);
        }
        m_tsneChart->createDefaultAxes();
        m_tsneStatusLabel->setText(QString::fromStdString(result.status));
    }, Qt::QueuedConnection);
}

// ── Prediction ──────────────────────────────────────────────

void AssociativeLearner::trainPrediction() {
    auto models = m_engine.trainPrediction(m_currentVariable.toStdString());
    m_predictionTable->setRowCount(static_cast<int>(models.size()));
    int row = 0;
    for (const auto &m : models) {
        m_predictionTable->setItem(row, 0, new QTableWidgetItem(
            QString("0x%1").arg(m.id, 3, 16, QChar('0')).toUpper()));
        m_predictionTable->setItem(row, 1, new QTableWidgetItem(
            QString::number(m.byte)));
        m_predictionTable->setItem(row, 2, new QTableWidgetItem(
            QString::number(m.a, 'f', 4)));
        m_predictionTable->setItem(row, 3, new QTableWidgetItem(
            QString::number(m.b, 'f', 4)));
        m_predictionTable->setItem(row, 4, new QTableWidgetItem("—"));
        row++;
    }
    if (!models.empty()) m_predictionTimer->start(500);
}

void AssociativeLearner::updatePredictionDisplay() {
    auto preds = m_engine.predictRealtime(m_currentVariable.toStdString());
    if (preds.empty()) return;
    for (int row = 0; row < m_predictionTable->rowCount(); ++row) {
        auto *idItem = m_predictionTable->item(row, 0);
        auto *bItem  = m_predictionTable->item(row, 1);
        if (!idItem || !bItem) continue;
        uint32_t id = idItem->text().toUInt(nullptr, 16);
        int byte = bItem->text().toInt();
        for (const auto &p : preds) {
            if (p.id == id && p.byte == byte) {
                m_predictionTable->item(row, 4)->setText(
                    QString::number(p.predictedValue, 'f', 2));
                break;
            }
        }
    }
}

// ── Anomaly ─────────────────────────────────────────────────

void AssociativeLearner::startAnomalyMonitoring() {
    m_engine.buildNormalModel();
    if (!m_engine.modelBuilt()) return;
    m_monitoring = true;
    m_anomalyToggleBtn->setText("■ Zatrzymaj monitorowanie anomalii");
    m_anomalyTimer->start(1000);
}

void AssociativeLearner::stopAnomalyMonitoring() {
    m_monitoring = false;
    m_anomalyToggleBtn->setText("▶ Rozpocznij monitorowanie anomalii");
    m_anomalyTimer->stop();
}

void AssociativeLearner::checkAnomaly() {
    if (!m_monitoring) return;
    double threshold = m_anomalyThreshold->text().toDouble();
    double score = m_engine.checkAnomaly(threshold);
    if (score > threshold) {
        int row = m_anomalyTable->rowCount();
        m_anomalyTable->insertRow(row);
        m_anomalyTable->setItem(row, 0, new QTableWidgetItem("now"));
        m_anomalyTable->setItem(row, 1, new QTableWidgetItem(
            QString::number(score, 'f', 2)));
        m_anomalyTable->setItem(row, 2, new QTableWidgetItem("Anomalia wykryta"));
        m_anomalyTable->scrollToBottom();
        emit anomalyDetected();
    }
}

void AssociativeLearner::checkAutoEvent() {
    // Auto-event detection: check if variable gradient exceeds threshold
    if (m_currentVariable.isEmpty()) return;
    auto obs = m_engine.observations(m_currentVariable.toStdString());
    if (obs.size() < 2) return;

    double threshold = m_autoEventThreshold->text().toDouble();
    double lastVal = obs.back().value;
    double prevVal = obs[obs.size() - 2].value;
    double gradient = std::abs(lastVal - prevVal);

    if (gradient > threshold) {
        markEvent();
    }
}

// ── Markov ─────────────────────────────────────────────────

void AssociativeLearner::trainMarkovModel() {
    m_engine.trainMarkovModel();
}

void AssociativeLearner::predictNextFrames() {
    auto entries = m_engine.predictNextFrames();
    if (entries.empty()) {
        m_markovTable->setRowCount(0);
        return;
    }
    m_markovTable->setRowCount(static_cast<int>(entries.size()));
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        m_markovTable->setItem(i, 0, new QTableWidgetItem(
            QString("0x%1").arg(entries[i].fromId, 3, 16, QChar('0')).toUpper()));
        m_markovTable->setItem(i, 1, new QTableWidgetItem(
            entries[i].toId ? QString("0x%1").arg(entries[i].toId, 3, 16, QChar('0')).toUpper() : "?"));
        m_markovTable->setItem(i, 2, new QTableWidgetItem(
            QString("%1%").arg(entries[i].probability * 100.0, 0, 'f', 1)));
        QColor col = (entries[i].probability > 0.7) ? QColor("#00ffaa")
                   : (entries[i].probability > 0.4) ? QColor("#ffaa00")
                   : QColor("#ff66cc");
        m_markovTable->item(i, 0)->setForeground(QColor("#c0c0c0"));
        m_markovTable->item(i, 1)->setForeground(col);
        m_markovTable->item(i, 2)->setForeground(col);
    }
}

// ── Cross-variable matrix ───────────────────────────────────

void AssociativeLearner::updateCrossVariableMatrix() {
    auto matrix = m_engine.computeCrossVariableMatrix();
    auto names = m_engine.variableNames();
    int N = static_cast<int>(names.size());
    if (N < 2) { m_crossVarTable->setRowCount(0); m_crossVarTable->setColumnCount(0); return; }

    QStringList qtNames;
    for (const auto &n : names) qtNames.append(QString::fromStdString(n));

    m_crossVarTable->setRowCount(N);
    m_crossVarTable->setColumnCount(N);
    m_crossVarTable->setHorizontalHeaderLabels(qtNames);
    m_crossVarTable->setVerticalHeaderLabels(qtNames);

    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            double corr = matrix[i][j];
            QTableWidgetItem *item = new QTableWidgetItem(
                QString::number(corr, 'f', 2));
            int r, g, b;
            if (corr < 0) {
                r = static_cast<int>(255 * (1 + corr));
                g = static_cast<int>(255 * (1 + corr));
                b = 255;
            } else {
                r = static_cast<int>(255 * (1 - corr));
                g = 255;
                b = static_cast<int>(255 * (1 - corr));
            }
            item->setBackground(QColor(r, g, b));
            m_crossVarTable->setItem(i, j, item);
        }
    }
}

// ── Mutual Information ──────────────────────────────────────

void AssociativeLearner::computeMutualInformation() {
    auto entries = m_engine.computeMutualInformation(m_currentVariable.toStdString());
    m_miTable->setRowCount(static_cast<int>(entries.size()));
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const auto &e = entries[i];
        m_miTable->setItem(i, 0, new QTableWidgetItem(
            QString("0x%1").arg(e.id, 3, 16, QChar('0')).toUpper()));
        m_miTable->setItem(i, 1, new QTableWidgetItem(QString::number(e.byte)));
        m_miTable->setItem(i, 2, new QTableWidgetItem(
            QString::number(e.mi, 'f', 4)));
        m_miTable->setItem(i, 3, new QTableWidgetItem(
            QString::fromStdString(e.comparison)));
        QColor col = (e.mi > 0.1) ? QColor("#00ffaa")
                   : (e.mi > 0.05) ? QColor("#ffaa00") : QColor("#ff66cc");
        m_miTable->item(i, 0)->setForeground(QColor("#c0c0c0"));
        m_miTable->item(i, 1)->setForeground(QColor("#c0c0c0"));
        m_miTable->item(i, 2)->setForeground(col);
        m_miTable->item(i, 3)->setForeground(col);
    }
}

void AssociativeLearner::computeMIC() {
    auto entries = m_engine.computeMIC(m_currentVariable.toStdString());
    m_micTable->setRowCount(static_cast<int>(entries.size()));
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const auto &e = entries[i];
        m_micTable->setItem(i, 0, new QTableWidgetItem(
            QString("0x%1").arg(e.id, 3, 16, QChar('0')).toUpper()));
        m_micTable->setItem(i, 1, new QTableWidgetItem(QString::number(e.byte)));
        m_micTable->setItem(i, 2, new QTableWidgetItem(
            QString::number(e.mic, 'f', 4)));
    }
}

// ── FFT ─────────────────────────────────────────────────────

void AssociativeLearner::runFftAnalysis() {
    QString idText = m_fftIdCombo->currentText().trimmed();
    if (idText.isEmpty()) return;
    if (idText.startsWith("0x", Qt::CaseInsensitive))
        idText = idText.mid(2);
    bool ok;
    uint32_t targetId = idText.toUInt(&ok, 16);
    if (!ok) return;
    int byteIdx = m_fftByteCombo->currentData().toInt();

    auto result = m_engine.runFftAnalysis(targetId, byteIdx);

    // Update chart
    m_fftSeries->clear();
    double magMax = 0.0;
    for (size_t i = 1; i < result.magnitudes.size(); ++i) {
        m_fftSeries->append(result.frequencies[i], result.magnitudes[i]);
        if (result.magnitudes[i] > magMax) magMax = result.magnitudes[i];
    }
    if (magMax < 0.01) magMax = 1.0;
    double fMax = result.frequencies.empty() ? result.fsHz / 2.0 : result.frequencies.back();
    if (fMax < 1.0) fMax = result.fsHz / 2.0;
    m_fftChart->axes(Qt::Horizontal).first()->setRange(0, fMax);
    m_fftChart->axes(Qt::Vertical).first()->setRange(0, magMax * 1.1);
    m_fftChart->setTitle(QString("Widmo ID 0x%1 bajt %2 (fs=≈%3 Hz, N=%4)")
        .arg(targetId, 3, 16, QChar('0')).arg(byteIdx)
        .arg(result.fsHz, 0, 'f', 1).arg(result.sampleCount));

    // Peak table
    m_fftPeakTable->setRowCount(static_cast<int>(result.peaks.size()));
    for (size_t i = 0; i < result.peaks.size(); ++i) {
        const auto &p = result.peaks[i];
        m_fftPeakTable->setItem(i, 0, new QTableWidgetItem(
            QString::number(p.frequency, 'f', 2)));
        m_fftPeakTable->setItem(i, 1, new QTableWidgetItem(
            p.periodMs > 0 ? QString::number(p.periodMs, 'f', 1) : "∞"));
        m_fftPeakTable->setItem(i, 2, new QTableWidgetItem(
            QString::number(p.magnitude, 'f', 2)));
        m_fftPeakTable->setItem(i, 3, new QTableWidgetItem(
            QString::fromStdString(p.description)));
    }

    // Populate ID combo if empty
    if (m_fftIdCombo->count() == 0) {
        auto snapshot = m_engine.frameHistorySnapshot();
        QSet<uint32_t> ids;
        for (const auto &f : snapshot) ids.insert(f.id);
        QList<uint32_t> sorted = ids.values();
        std::sort(sorted.begin(), sorted.end());
        for (uint32_t id : sorted)
            m_fftIdCombo->addItem(QString("0x%1").arg(id, 3, 16, QChar('0')).toUpper(), id);
        int idx = m_fftIdCombo->findData(targetId);
        if (idx >= 0) m_fftIdCombo->setCurrentIndex(idx);
    }

    Logger::log(QString("FFT: ID=0x%1 byte=%2 N=%3 fs=%4 Hz peaks=%5")
        .arg(targetId, 3, 16, QChar('0')).arg(byteIdx).arg(result.sampleCount)
        .arg(result.fsHz, 0, 'f', 1).arg(static_cast<int>(result.peaks.size())));
}

// ── Charts ──────────────────────────────────────────────────

void AssociativeLearner::updateChart() {
    m_scatterSeries->clear();
    auto obs = m_engine.observations(m_currentVariable.toStdString());
    if (obs.size() < 2) return;

    uint32_t anyId = 0;
    bool found = false;
    for (const auto &o : obs) {
        if (!o.idAverageBytes.empty()) {
            anyId = o.idAverageBytes.begin()->first;
            found = true;
            break;
        }
    }
    if (!found) return;

    for (const auto &o : obs) {
        auto it = o.idAverageBytes.find(anyId);
        if (it != o.idAverageBytes.end())
            m_scatterSeries->append(static_cast<double>(it->second[0]), o.value);
    }
}

void AssociativeLearner::updateTimeChart() {
    m_varTimeSeries->clear();
    m_byteTimeSeries->clear();
    auto obs = m_engine.observations(m_currentVariable.toStdString());
    if (obs.size() < 2) return;

    QSet<uint32_t> ids;
    for (const auto &o : obs)
        for (const auto &kv : o.idAverageBytes)
            ids.insert(kv.first);
    m_timeIdCombo->blockSignals(true);
    m_timeIdCombo->clear();
    for (uint32_t id : ids)
        m_timeIdCombo->addItem(QString("0x%1").arg(id, 3, 16, QChar('0')).toUpper(), id);
    m_timeIdCombo->blockSignals(false);

    uint32_t selId = m_timeIdCombo->currentData().toUInt();
    int selByte = m_timeByteCombo->currentData().toInt();

    for (size_t i = 0; i < obs.size(); ++i) {
        double x = static_cast<double>(i);
        m_varTimeSeries->append(x, obs[i].value);
        auto it = obs[i].idAverageBytes.find(selId);
        if (it != obs[i].idAverageBytes.end() && selByte < 64)
            m_byteTimeSeries->append(x, static_cast<double>(it->second[selByte]));
    }
}

// ── Auto-discovery ──────────────────────────────────────────

void AssociativeLearner::updateAutoDiscovery() {
    auto entries = m_engine.autoDiscovery(m_currentVariable.toStdString());
    int rows = std::min(static_cast<int>(entries.size()), 30);
    m_autoDiscoveryTable->setRowCount(rows);
    for (int i = 0; i < rows; ++i) {
        const auto &e = entries[i];
        m_autoDiscoveryTable->setItem(i, 0, new QTableWidgetItem(
            QString("0x%1").arg(e.id, 3, 16, QChar('0')).toUpper()));
        m_autoDiscoveryTable->setItem(i, 1, new QTableWidgetItem(QString::number(e.byte)));
        m_autoDiscoveryTable->setItem(i, 2, new QTableWidgetItem(
            QString::number(e.correlation, 'f', 3)));
        m_autoDiscoveryTable->setItem(i, 3, new QTableWidgetItem(
            QString::number(e.pValue, 'e', 2)));
        QString desc;
        if (m_dbc) {
            DbcMessage dm = m_dbc->messageForId(e.id);
            if (dm.id != 0) desc = dm.name;
        }
        m_autoDiscoveryTable->setItem(i, 4, new QTableWidgetItem(desc));

        QColor col = (std::abs(e.correlation) > 0.8) ? QColor("#00ffaa")
                   : (std::abs(e.correlation) > 0.6) ? QColor("#ffaa00")
                   : QColor("#ff66cc");
        for (int c = 0; c < 5; ++c)
            if (auto *it = m_autoDiscoveryTable->item(i, c))
                it->setForeground(c == 4 ? QColor("#ffaa00") : col);
    }
    m_autoDiscoveryLabel->setText(QString("Aktywne – %1 kandydatów | Ostatnia aktualizacja: teraz")
                                  .arg(entries.size()));
}

// ── Neural network prediction ───────────────────────────────

void AssociativeLearner::updateNnPrediction() {
    auto obs = m_engine.observations(m_currentVariable.toStdString());
    if (obs.empty()) return;

    // Build input vector from correlation table features
    std::vector<double> input;
    int D = std::min(m_correlationTable->rowCount(), 16);
    for (int f = 0; f < D; ++f) {
        auto *idItem = m_correlationTable->item(f, 0);
        auto *bItem  = m_correlationTable->item(f, 1);
        if (!idItem || !bItem) { input.push_back(0.0); continue; }
        uint32_t id = idItem->text().toUInt(nullptr, 16);
        int b = bItem->text().toInt();

        const auto &lastObs = obs.back();
        auto it = lastObs.idAverageBytes.find(id);
        if (it != lastObs.idAverageBytes.end() && b < 64)
            input.push_back(static_cast<double>(it->second[b]) / 255.0);
        else
            input.push_back(0.0);
    }

    double predNorm = m_engine.predictNeural(input);
    m_nnPredictionLabel->setText(QString("Predykcja NN: %1").arg(predNorm, 0, 'f', 2));
}

// ── Granger causality UI ──────────────────────────────────

void AssociativeLearner::runGrangerCausality() {
    auto results = m_engine.computeGrangerCausality(m_currentVariable.toStdString());
    m_grangerTable->setRowCount(static_cast<int>(results.size()));
    for (int i = 0; i < static_cast<int>(results.size()); ++i) {
        const auto &r = results[i];
        m_grangerTable->setItem(i, 0, new QTableWidgetItem(
            QString("0x%1").arg(r.id, 3, 16, QChar('0')).toUpper()));
        m_grangerTable->setItem(i, 1, new QTableWidgetItem(QString::number(r.byte)));
        m_grangerTable->setItem(i, 2, new QTableWidgetItem(QString::number(r.fStatistic, 'f', 2)));
        m_grangerTable->setItem(i, 3, new QTableWidgetItem(QString::number(r.pValue, 'e', 2)));
        auto *causalItem = new QTableWidgetItem(r.isCausal ? "TAK" : "nie");
        causalItem->setForeground(r.isCausal ? QColor("#00ffaa") : QColor("#ff66cc"));
        m_grangerTable->setItem(i, 4, causalItem);
    }
}

// ── Change-point detection UI ──────────────────────────────

void AssociativeLearner::runChangePointDetection() {
    bool ok;
    QString idText = m_cpIdCombo->currentText().trimmed();
    if (idText.isEmpty()) return;
    if (idText.startsWith("0x", Qt::CaseInsensitive)) idText = idText.mid(2);
    uint32_t targetId = idText.toUInt(&ok, 16);
    if (!ok) return;
    int targetByte = m_cpByteCombo->currentData().toInt();

    // Populate ID combo lazily (initially empty, has only placeholder)
    if (m_cpIdCombo->count() == 0) {
        auto obs = m_engine.observations(m_currentVariable.toStdString());
        QSet<uint32_t> ids;
        for (const auto &o : obs)
            for (const auto &kv : o.idAverageBytes)
                ids.insert(kv.first);
        for (uint32_t id : ids)
            m_cpIdCombo->addItem(QString("0x%1").arg(id, 3, 16, QChar('0')).toUpper(), id);
        int idx = m_cpIdCombo->findData(targetId);
        if (idx >= 0) m_cpIdCombo->setCurrentIndex(idx);
    }

    auto points = m_engine.detectChangePoints(m_currentVariable.toStdString(), targetId, targetByte);
    m_changePointTable->setRowCount(static_cast<int>(points.size()));
    for (int i = 0; i < static_cast<int>(points.size()); ++i) {
        m_changePointTable->setItem(i, 0, new QTableWidgetItem(QString::number(points[i].index)));
        m_changePointTable->setItem(i, 1, new QTableWidgetItem(QString::number(points[i].costReduction, 'f', 4)));
        m_changePointTable->setItem(i, 2, new QTableWidgetItem(QString::number(points[i].meanBefore, 'f', 2)));
        m_changePointTable->setItem(i, 3, new QTableWidgetItem(QString::number(points[i].meanAfter, 'f', 2)));
    }
}

// ── Cross-correlation lag UI ───────────────────────────────

void AssociativeLearner::runCrossCorrelationLag() {
    auto entries = m_engine.computeCrossCorrelationLag(m_currentVariable.toStdString());
    m_lagCorrTable->setRowCount(static_cast<int>(entries.size()));
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        m_lagCorrTable->setItem(i, 0, new QTableWidgetItem(
            QString("0x%1").arg(entries[i].id, 3, 16, QChar('0')).toUpper()));
        m_lagCorrTable->setItem(i, 1, new QTableWidgetItem(QString::number(entries[i].byte)));
        m_lagCorrTable->setItem(i, 2, new QTableWidgetItem(QString::number(entries[i].lag)));
        auto *corrItem = new QTableWidgetItem(QString::number(entries[i].correlation, 'f', 3));
        QColor col = (std::abs(entries[i].correlation) > 0.7) ? QColor("#00ffaa") :
                     (std::abs(entries[i].correlation) > 0.4) ? QColor("#ffaa00") : QColor("#ff66cc");
        corrItem->setForeground(col);
        m_lagCorrTable->setItem(i, 3, corrItem);
    }
}

// ── GBT UI ─────────────────────────────────────────────────

void AssociativeLearner::trainGbtModel() {
    auto model = m_engine.trainGbt(m_currentVariable.toStdString());
    if (model.trees.empty()) {
        m_gbtStatusLabel->setText("Za malo danych do GBT");
        return;
    }
    m_gbtStatusLabel->setText(QString("GBT: %1 drzew, base=%2")
        .arg(model.trees.size())
        .arg(model.basePrediction, 0, 'f', 2));
    updateGbtDisplay();
}

void AssociativeLearner::updateGbtDisplay() {
    auto obs = m_engine.observations(m_currentVariable.toStdString());
    if (obs.empty()) return;

    auto model = m_engine.trainGbt(m_currentVariable.toStdString());
    if (model.trees.empty()) return;

    // Reconstruct feature list matching trainGbt internal ordering
    std::vector<std::pair<uint32_t, int>> featureList;
    std::unordered_set<uint32_t> seenIds;
    for (const auto &o : obs)
        for (const auto &kv : o.idAverageBytes)
            if (!seenIds.count(kv.first)) {
                seenIds.insert(kv.first);
                for (int b = 0; b < 64; ++b)
                    featureList.push_back({kv.first, b});
            }

    if (featureList.empty()) return;

    const auto &lastObs = obs.back();
    std::vector<double> features(featureList.size());
    for (size_t f = 0; f < featureList.size(); ++f) {
        auto it = lastObs.idAverageBytes.find(featureList[f].first);
        features[f] = (it != lastObs.idAverageBytes.end())
            ? static_cast<double>(it->second[featureList[f].second]) : 0.0;
    }

    double pred = m_engine.predictGbt(model, features);

    m_gbtPredTable->setRowCount(1);
    m_gbtPredTable->setItem(0, 0, new QTableWidgetItem("—"));
    m_gbtPredTable->setItem(0, 1, new QTableWidgetItem("—"));
    m_gbtPredTable->setItem(0, 2, new QTableWidgetItem(QString::number(pred, 'f', 2)));
}

// ── Serialization ────────────────────────────────────────────

// ── autoSave: called by timer or destructor ───────────────

void AssociativeLearner::autoSave() {
    QFile file(m_autoSavePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&file);
    out << QString::fromStdString(m_engine.serializeSession());
    file.close();
}

// ── saveSession: interactive save via file dialog ─────────

void AssociativeLearner::saveSession() {
    QString path = QFileDialog::getSaveFileName(this, "Zapisz sesje",
        QDir::homePath() + "/magistrala_session.json",
        "JSON (*.json);;Wszystkie pliki (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Blad", "Nie mozna zapisac pliku: " + path);
        return;
    }
    QTextStream out(&file);
    out << QString::fromStdString(m_engine.serializeSession());
    file.close();

    m_autoSavePath = path;
    m_iterationLabel->setText(m_iterationLabel->text() + " | Sesja zapisana");
}

// ── loadSession: interactive load + UI refresh ────────────

void AssociativeLearner::loadSession() {
    QString path = QFileDialog::getOpenFileName(this, "Wczytaj sesje",
        QDir::homePath() + "/magistrala_session.json",
        "JSON (*.json);;Wszystkie pliki (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Blad", "Nie mozna odczytac pliku: " + path);
        return;
    }
    QTextStream in(&file);
    QString json = in.readAll();
    file.close();

    if (!m_engine.deserializeSession(json.toStdString())) {
        QMessageBox::warning(this, "Blad", "Nieprawidlowy format pliku sesji.");
        return;
    }

    // Refresh UI state
    m_variableCombo->clear();
    m_currentVariable.clear();
    auto names = m_engine.variableNames();
    for (const auto &n : names) {
        QString qn = QString::fromStdString(n);
        m_variableCombo->addItem(qn, qn);
    }
    if (!names.empty()) {
        m_variableCombo->setCurrentIndex(0);
        m_currentVariable = QString::fromStdString(names[0]);
    }

    int iter = m_engine.iterationCount();
    m_iterationLabel->setText(QString("Liczba iteracji: %1 (wczytane)").arg(iter));

    m_autoSavePath = path;
    updateCandidates();
    updateCorrelationTable();
    updateSequenceTable();
    updateCrossByteTable();
    updateChart();
    updateTimeChart();
}

// ── exportModels: export linear models to JSON ────────────

void AssociativeLearner::exportModels() {
    auto modelsMap = m_engine.linearModels();
    QJsonObject root;
    for (const auto &varKv : modelsMap) {
        QJsonObject varObj;
        for (const auto &modelKv : varKv.second) {
            uint64_t key = modelKv.first;
            uint32_t id = static_cast<uint32_t>(key >> 8);
            int byte = static_cast<int>(key & 0xFF);
            double a = modelKv.second.first;
            double b = modelKv.second.second;
            QJsonObject modelObj;
            modelObj["id"] = static_cast<int>(id);
            modelObj["byte"] = byte;
            modelObj["a"] = a;
            modelObj["b"] = b;
            varObj[QString::number(key)] = modelObj;
        }
        root[QString::fromStdString(varKv.first)] = varObj;
    }

    QString path = QFileDialog::getSaveFileName(this, "Eksportuj modele",
        QDir::homePath() + "/magistrala_models.json",
        "JSON (*.json);;Wszystkie pliki (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Blad", "Nie mozna zapisac modeli.");
        return;
    }
    QTextStream out(&file);
    out << QJsonDocument(root).toJson(QJsonDocument::Indented);
    file.close();
}

// ── importModels: import linear models from JSON ──────────

void AssociativeLearner::importModels() {
    QString path = QFileDialog::getOpenFileName(this, "Importuj modele",
        QDir::homePath() + "/magistrala_models.json",
        "JSON (*.json);;Wszystkie pliki (*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Blad", "Nie mozna odczytac pliku modeli.");
        return;
    }
    QTextStream in(&file);
    QJsonDocument doc = QJsonDocument::fromJson(in.readAll().toUtf8());
    file.close();

    if (!doc.isObject()) {
        QMessageBox::warning(this, "Blad", "Nieprawidlowy format pliku modeli.");
        return;
    }

    // Note: linear models are read-only via the getter.
    // Full import requires manual retraining.
    QMessageBox::information(this, "Import modeli",
        QString("Wczytano %1 zmiennych z modelami. Aby zastosowac modele, "
                "uzyj 'Trenuj predykcje' dla kazdej zmiennej.")
        .arg(doc.object().size()));
}

// ── generateLuaScript: generate Lua filter script ─────────

void AssociativeLearner::generateLuaScript() {
    QString path = QFileDialog::getSaveFileName(this, "Generuj skrypt Lua",
        QDir::homePath() + "/magistrala_filter.lua",
        "Lua (*.lua);;Wszystkie pliki (*)");
    if (path.isEmpty()) return;

    auto correlations = m_engine.computeCorrelations(m_currentVariable.toStdString());
    auto models = m_engine.trainPrediction(m_currentVariable.toStdString());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Blad", "Nie mozna zapisac skryptu Lua.");
        return;
    }
    QTextStream out(&file);

    out << "-- MagistralaCAN4 auto-generated filter script\n";
    out << "-- Variable: " << m_currentVariable << "\n";
    out << "-- Generated: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n\n";

    out << "local filter = {}\n\n";
    out << "-- Significant CAN IDs (p < 0.05)\n";
    out << "filter.significantIds = {\n";
    int count = 0;
    for (const auto &c : correlations) {
        if (c.significant && count < 20) {
            out << QString("    [0x%1] = { byte = %2, corr = %3 },\n")
                .arg(c.id, 3, 16, QChar('0')).arg(c.byte)
                .arg(c.correlation, 0, 'f', 3);
            count++;
        }
    }
    out << "}\n\n";

    out << "-- Prediction models\n";
    out << "filter.models = {\n";
    for (const auto &m : models) {
        out << QString("    { id = 0x%1, byte = %2, a = %3, b = %4 },\n")
            .arg(m.id, 3, 16, QChar('0')).arg(m.byte)
            .arg(m.a, 0, 'f', 4).arg(m.b, 0, 'f', 4);
    }
    out << "}\n\n";

    out << "-- Apply filter: returns true if frame matches significant pattern\n";
    out << "function filter:match(frame)\n";
    out << "    for id, cfg in pairs(self.significantIds) do\n";
    out << "        if frame.id == id and frame.dlc > cfg.byte then\n";
    out << "            return true\n";
    out << "        end\n";
    out << "    end\n";
    out << "    return false\n";
    out << "end\n\n";
    out << "return filter\n";

    file.close();
}

// ── exportHtmlReport: generate HTML report ────────────────

void AssociativeLearner::exportHtmlReport() {
    QString path = QFileDialog::getSaveFileName(this, "Eksportuj raport HTML",
        QDir::homePath() + "/magistrala_report.html",
        "HTML (*.html);;Wszystkie pliki (*)");
    if (path.isEmpty()) return;

    auto correlations = m_engine.computeCorrelations(m_currentVariable.toStdString());
    auto models = m_engine.trainPrediction(m_currentVariable.toStdString());
    auto obs = m_engine.observations(m_currentVariable.toStdString());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Blad", "Nie mozna zapisac raportu HTML.");
        return;
    }
    QTextStream out(&file);

    out << "<!DOCTYPE html>\n<html><head><meta charset='UTF-8'>\n";
    out << "<title>MagistralaCAN4 - Raport</title>\n";
    out << "<style>body{font-family:Arial;margin:20px;background:#1a1a2e;color:#e0e0e0;}"
        << "h1{color:#e94560;}h2{color:#00ffaa;}"
        << "table{border-collapse:collapse;width:100%;margin:10px 0;}"
        << "th{background:#16213e;padding:8px;text-align:left;}"
        << "td{padding:6px;border-bottom:1px solid #333;}"
        << ".sig{color:#00ffaa;}.warn{color:#ffaa00;}</style>\n";
    out << "</head><body>\n";

    out << "<h1>MagistralaCAN4 - Raport analizy</h1>\n";
    out << "<p>Zmienna: <b>" << m_currentVariable << "</b> | ";
    out << "Data: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm") << " | ";
    out << "Obserwacje: " << obs.size() << "</p>\n";

    out << "<h2>Tabela korelacji (Pearson)</h2>\n";
    out << "<table><tr><th>CAN ID</th><th>Bajt</th><th>Korelacja</th><th>p-value</th><th>Istotna?</th></tr>\n";
    int shown = 0;
    for (const auto &c : correlations) {
        if (shown++ >= 50) break;
        QString sigClass = c.significant ? "sig" : "warn";
        out << "<tr><td>0x" << QString::number(c.id, 16).toUpper()
            << "</td><td>" << c.byte
            << "</td><td>" << QString::number(c.correlation, 'f', 3)
            << "</td><td>" << QString::number(c.pValue, 'e', 2)
            << "</td><td class='" << sigClass << "'>" << (c.significant ? "TAK" : "nie")
            << "</td></tr>\n";
    }
    out << "</table>\n";

    out << "<h2>Modele predykcyjne</h2>\n";
    out << "<table><tr><th>CAN ID</th><th>Bajt</th><th>a (kier.)</th><th>b (wolny)</th></tr>\n";
    for (const auto &m : models) {
        out << "<tr><td>0x" << QString::number(m.id, 16).toUpper()
            << "</td><td>" << m.byte
            << "</td><td>" << QString::number(m.a, 'f', 4)
            << "</td><td>" << QString::number(m.b, 'f', 4)
            << "</td></tr>\n";
    }
    out << "</table>\n";

    out << "<p><i>Wygenerowano przez MagistralaCAN4</i></p>\n";
    out << "</body></html>\n";
    file.close();
}

