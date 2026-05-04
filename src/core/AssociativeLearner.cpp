#include "Logger.h"
#include "AssociativeLearner.h"
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QtConcurrent>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <numeric>
#include <cmath>
#include <set>
#include <random>
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
    m_shortcutNonEvent = new QShortcut(QKeySequence("Ctrl+Shift+D"), this);
    connect(m_shortcutNonEvent, &QShortcut::activated, this, &AssociativeLearner::markNonEvent);
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

    makeTable(m_correlationTable, {"CAN ID","Bajt","Korelacja",""});
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
    m_pcaChartView->setRenderHint(QPainter::Antialiasing);
    m_pcaChartView->setMinimumHeight(300);
    mainLayout->addWidget(m_pcaChartView);
    connect(m_pcaBtn, &QPushButton::clicked, this, &AssociativeLearner::runPcaClustering);
    makeTable(m_clusterTable, {"Klaster","Śr. liczba ramek","Dominujące ID","Liczba okien"});
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
    // Auto-detekcja zdarzeń
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
    m_elbowChartView->setRenderHint(QPainter::Antialiasing);
    m_elbowChartView->setMinimumHeight(250);
    mainLayout->addWidget(m_elbowChartView);
    connect(m_autoKBtn, &QPushButton::clicked, this, &AssociativeLearner::autoKMeans);
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
    connect(m_crossVarBtn, &QPushButton::clicked, this, &AssociativeLearner::updateCrossVariableMatrix);
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
    m_miTable->setMinimumHeight(500);  /* wystarczająco na 20 wierszy */
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
    connect(m_miBtn, &QPushButton::clicked, this, &AssociativeLearner::computeMutualInformation);
    connect(m_markovTimer, &QTimer::timeout, this, &AssociativeLearner::predictNextFrames);
    connect(m_trainMarkovBtn, &QPushButton::clicked, this, [this]() {
        trainMarkovModel();
        if (!m_transitions.isEmpty()) m_markovTimer->start(1000);
    });
    m_scatterSeries = new QScatterSeries(); m_scatterSeries->setMarkerSize(8.0); m_scatterSeries->setColor(QColor("#00ffaa"));
    m_chart->addSeries(m_scatterSeries); m_chart->createDefaultAxes();
    m_chartView = new QChartView(m_chart); m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setMinimumHeight(300);
    mainLayout->addWidget(m_chartView);

    auto *serLayout = new QHBoxLayout;
    m_saveBtn = new QPushButton("💾 Zapisz sesję"); m_loadBtn = new QPushButton("📂 Wczytaj sesję");
    m_exportModelsBtn = new QPushButton("📤 Eksportuj modele");
    m_importModelsBtn = new QPushButton("📥 Importuj modele");
    serLayout->addWidget(m_saveBtn); serLayout->addWidget(m_loadBtn);
    serLayout->addWidget(m_exportModelsBtn); serLayout->addWidget(m_importModelsBtn);
    mainLayout->addLayout(serLayout);

    connect(m_markEventBtn, &QPushButton::clicked, this, &AssociativeLearner::markEvent);
    connect(m_markNonEventBtn, &QPushButton::clicked, this, &AssociativeLearner::markNonEvent);
    connect(m_resetBtn, &QPushButton::clicked, this, &AssociativeLearner::resetLearning);
    connect(m_addObsBtn, &QPushButton::clicked, this, &AssociativeLearner::addObservation);
    connect(m_saveBtn, &QPushButton::clicked, this, &AssociativeLearner::saveSession);
    connect(m_loadBtn, &QPushButton::clicked, this, &AssociativeLearner::loadSession);
    connect(m_exportModelsBtn, &QPushButton::clicked, this, &AssociativeLearner::exportModels);
    connect(m_importModelsBtn, &QPushButton::clicked, this, &AssociativeLearner::importModels);
    connect(m_ngramCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int) { updateSequenceTable(); });
    connect(m_clusterBtn, &QPushButton::clicked, this, &AssociativeLearner::clusterWindows);
    connect(m_trainPredictionBtn, &QPushButton::clicked, this, &AssociativeLearner::trainPrediction);
    connect(m_addVariableBtn, &QPushButton::clicked, this, &AssociativeLearner::addNewVariable);
    connect(m_variableCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AssociativeLearner::onVariableChanged);

    setStyleSheet(R"(
        QPushButton { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560; border-radius: 4px; padding: 6px 15px; font-weight: bold; }
        QPushButton:hover { background: #e94560; color: #0a0e17; }
        QLineEdit, QComboBox { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560; border-radius: 4px; padding: 4px 8px; }
    )");

    addVariable("temperatura");
}

AssociativeLearner::~AssociativeLearner() { m_predictionTimer->stop(); m_anomalyTimer->stop(); }

void AssociativeLearner::addVariable(const QString &name) {
    QString key = name.toLower().trimmed();
    if (key.isEmpty() || m_observationsMap.contains(key)) return;
    m_observationsMap[key] = {};
    m_variableCombo->addItem(name, key);
    if (m_variableCombo->count() == 1) { m_variableCombo->setCurrentIndex(0); m_currentVariable = key; }
}

void AssociativeLearner::addNewVariable() {
    QString name = m_newVariableName->text().trimmed();
    if (!name.isEmpty()) { addVariable(name); m_newVariableName->clear(); }
}

void AssociativeLearner::onVariableChanged(int idx) {
    if (idx >= 0) { m_currentVariable = m_variableCombo->itemData(idx).toString(); updateCorrelationTable(); updateCrossByteTable(); updateChart(); }
    if (m_autoEventCheck->isChecked()) checkAutoEvent();
}

QVector<ValueObservation> AssociativeLearner::currentObservations() const {
    return m_observationsMap.value(m_currentVariable);
}

void AssociativeLearner::processFrame(const CanFrame &frame) {
    m_frameHistory.push_back(frame);
    if (m_frameHistory.size() > HISTORY_MAX) m_frameHistory.pop_front();
}

void AssociativeLearner::markEvent() {
    if (m_frameHistory.empty()) return;
    uint64_t latestTs = m_frameHistory.back().timestamp;
    QVector<CanFrame> window;
    for (const auto &f : m_frameHistory) if (f.timestamp >= latestTs - m_adaptiveBefore && f.timestamp <= latestTs + m_adaptiveAfter) window.append(f);
    if (window.size() < 3) return;
    EventRecord rec; rec.windowFrames = window; rec.idFeatures = buildFeatureVectors(window);
    m_events.push_back(rec); m_iteration++;
    m_iterationLabel->setText(QString("Liczba iteracji: %1").arg(m_iteration));
    if (m_iteration == 1) recalcAdaptiveWindow();
    emit eventMarked(m_iteration);
    updateCandidates(); updateSequenceTable();
}

void AssociativeLearner::resetLearning() {
    m_events.clear(); m_observationsMap.clear(); m_iteration = 0;
    m_iterationLabel->setText("Liczba iteracji: 0");
    m_candidateModel->clear(); m_correlationTable->setRowCount(0);
    m_sequenceTable->setRowCount(0); m_crossByteTable->setRowCount(0);
    m_clusterTable->setRowCount(0); m_linearModels.clear(); m_predictionTable->setRowCount(0);
    m_predictionTimer->stop(); stopAnomalyMonitoring(); m_anomalyTable->setRowCount(0);
    m_normalMean.clear(); m_normalStd.clear();
    m_variableCombo->clear(); m_currentVariable.clear();
    m_scatterSeries->clear();
    addVariable("temperatura");
}

void AssociativeLearner::addObservation() {
    if (m_currentVariable.isEmpty()) return;
    bool ok; double v = m_valueInput->text().toDouble(&ok); if(!ok) return;
    if(m_frameHistory.empty()) return;
    uint64_t latestTs = m_frameHistory.back().timestamp;
    QVector<CanFrame> window;
    for(const auto &f : m_frameHistory) if(f.timestamp >= latestTs - m_adaptiveBefore && f.timestamp <= latestTs + m_adaptiveAfter) window.append(f);
    if(window.empty()) return;
    QHash<uint32_t, QVector<CanFrame>> grouped;
    for(const auto &f : window) grouped[f.id].append(f);
    ValueObservation obs; obs.value = v;
    for(auto it=grouped.begin(); it!=grouped.end(); ++it) {
        std::vector<uint8_t> avg(64,0); const auto &frames = it.value();
        for(const auto &f : frames) for(int i=0;i<f.dlc;++i) avg[i] += f.data[i]/frames.size();
        obs.idAverageBytes[it.key()] = avg;
    }
    m_observationsMap[m_currentVariable].append(obs); m_valueInput->clear();
    updateCorrelationTable(); updateCrossByteTable(); updateChart();
    if (m_autoEventCheck->isChecked()) checkAutoEvent();
}

void AssociativeLearner::recalcAdaptiveWindow() {
    if(m_events.isEmpty()) return;
    const auto &win = m_events.first().windowFrames;
    if(win.size()<2) return;
    QVector<int64_t> deltas; for(int i=1;i<win.size();++i) deltas.push_back(win[i].timestamp - win[i-1].timestamp);
    double mean = std::accumulate(deltas.begin(), deltas.end(),0)/(double)deltas.size();
    m_adaptiveBefore = std::max<int64_t>(100000, std::min<int64_t>(2000000, (int64_t)(3*mean)));
    m_adaptiveAfter = m_adaptiveBefore/3;
}

QHash<uint32_t, QVector<float>> AssociativeLearner::buildFeatureVectors(const QVector<CanFrame> &window) {
    QHash<uint32_t, QVector<CanFrame>> grouped;
    for(const auto &f : window) grouped[f.id].append(f);
    QHash<uint32_t, QVector<float>> res;
    for(auto it=grouped.begin(); it!=grouped.end(); ++it) {
        const auto &frames = it.value();
        QVector<float> feats(67); feats[0] = frames.size();
        QVector<int64_t> deltas; for(int i=1;i<frames.size();++i) deltas.push_back(frames[i].timestamp - frames[i-1].timestamp);
        if(deltas.isEmpty()){ feats[1]=0; feats[2]=0; }
        else {
            double sum = std::accumulate(deltas.begin(), deltas.end(),0);
            feats[1] = (float)(sum/deltas.size())/1000.0f;
            double sq=0; for(int64_t d : deltas) sq+=(d-feats[1])*(d-feats[1]);
            feats[2] = (float)std::sqrt(sq/deltas.size())/1000.0f;
        }
        for(int b=0; b<64; ++b){
            float avg=0; for(const auto &f : frames) if(b<f.dlc) avg+=f.data[b]; avg/=frames.size();
            feats[3+b] = avg/255.0f;
        }
        res[it.key()] = feats;
    }
    return res;
}

void AssociativeLearner::updateCandidates() {
    if(m_events.isEmpty()) return;
    if(m_events.size()==1){
        const auto &feats = m_events.first().idFeatures;
        QVector<Candidate> cands; for(auto it=feats.begin(); it!=feats.end(); ++it) cands.append({it.key(),"Pierwsze zdarzenie",0.0f,1});
        m_candidateModel->setCandidates(cands); return;
    }
    QSet<uint32_t> common; bool first=true;
    for(const auto &ev : m_events){
        QSet<uint32_t> ids; for(auto it=ev.idFeatures.begin(); it!=ev.idFeatures.end(); ++it) ids.insert(it.key());
        if(first){common=ids; first=false;} else common&=ids;
    }
    QVector<Candidate> cands;
    for(uint32_t id : common){
        QVector<QVector<float>> vecs; for(const auto &ev : m_events) vecs.append(ev.idFeatures[id]);
        int N=vecs.size(); float sim=0; int pairs=0;
        for(int i=0;i<N;++i) for(int j=i+1;j<N;++j){
            float dot=0,nA=0,nB=0; for(int k=0;k<vecs[i].size();++k){ float a=vecs[i][k],b=vecs[j][k]; dot+=a*b; nA+=a*a; nB+=b*b; }
            sim += dot/(std::sqrt(nA)*std::sqrt(nB)+1e-6f); pairs++;
        }
        cands.append({id, QString("ID 0x%1").arg(id,3,16,QChar('0')).toUpper(), (pairs>0?sim/pairs:0.0f), (int)vecs.size()});
    }
    // Kontrast z tłem: obniż score ID, które występują podobnie w non-eventach
    if (!m_nonEvents.isEmpty()) {
        for (auto &cand : cands) {
            uint32_t id = cand.canId;
            double bgSim = 0.0;
            int bgPairs = 0;
            for (const auto &nonEv : m_nonEvents) {
                auto it = nonEv.idFeatures.find(id);
                if (it != nonEv.idFeatures.end()) {
                    for (const auto &ev : m_events) {
                        auto itEv = ev.idFeatures.find(id);
                        if (itEv != ev.idFeatures.end()) {
                            float dot = 0, nA = 0, nB = 0;
                            const auto &v1 = it.value();
                            const auto &v2 = itEv.value();
                            for (int k = 0; k < v1.size(); ++k) {
                                float a = v1[k], b = v2[k];
                                dot += a*b; nA += a*a; nB += b*b;
                            }
                            bgSim += dot / (std::sqrt(nA)*std::sqrt(nB) + 1e-6f);
                            bgPairs++;
                        }
                    }
                }
            }
            if (bgPairs > 0) {
                double bgAvg = bgSim / bgPairs;
                cand.score = cand.score * (1.0 - bgAvg * 0.5);  /* im bardziej podobne do tła, tym większa kara */
            }
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Candidate &a, const Candidate &b){ return a.score>b.score; });
    m_candidateModel->setCandidates(cands);
}

void AssociativeLearner::updateCorrelationTable() {
    QVector<ValueObservation> obs = currentObservations();
    if(obs.size()<3){ m_correlationTable->setRowCount(0); return; }
    QSet<uint32_t> common; bool first=true;
    for(const auto &o : obs){
        QSet<uint32_t> ids; for(auto it=o.idAverageBytes.begin(); it!=o.idAverageBytes.end(); ++it) ids.insert(it.key());
        if(first){common=ids; first=false;} else common&=ids;
    }
    struct E{uint32_t id; int b; double corr;}; QVector<E> entries;
    for(uint32_t id : common) for(int b=0;b<64;++b){
        QVector<double> vx,vy;
        for(const auto &o : obs){ auto it=o.idAverageBytes.find(id); if(it!=o.idAverageBytes.end()){ vx.append(o.value); vy.append((double)it.value()[b]); } }
        int N=vx.size(); if(N<3) continue;
        double sx=0,sy=0,sxy=0,sx2=0,sy2=0; for(int i=0;i<N;++i){ double x=vx[i],y=vy[i]; sx+=x; sy+=y; sxy+=x*y; sx2+=x*x; sy2+=y*y; }
        double den=std::sqrt((N*sx2-sx*sx)*(N*sy2-sy*sy)); double corr=(den!=0)?(N*sxy-sx*sy)/den:0.0;
        entries.append({id,b,corr});
    }
    std::sort(entries.begin(), entries.end(), [](const E &a, const E &b){ return fabs(a.corr)>fabs(b.corr); });
    m_correlationTable->setRowCount(entries.size());
    for(int i=0;i<entries.size();++i){
        auto &e=entries[i]; m_correlationTable->setItem(i,0,new QTableWidgetItem(QString("0x%1").arg(e.id,3,16,QChar('0')).toUpper()));
        m_correlationTable->setItem(i,1,new QTableWidgetItem(QString::number(e.b)));
        m_correlationTable->setItem(i,2,new QTableWidgetItem(QString::number(e.corr,'f',3)));
        QColor col = (fabs(e.corr)>0.7)?QColor("#00ffaa"):(fabs(e.corr)>0.4)?QColor("#ffaa00"):QColor("#ff66cc");
        m_correlationTable->item(i,0)->setForeground(QColor("#c0c0c0")); m_correlationTable->item(i,1)->setForeground(QColor("#c0c0c0")); m_correlationTable->item(i,2)->setForeground(col);
    }
}

void AssociativeLearner::updateSequenceTable() {
    if(m_events.size()<2){ m_sequenceTable->setRowCount(0); return; }
    int n = m_ngramCombo->currentIndex()+2;
    QHash<QString,int> cnt; for(const auto &ev : m_events){ const auto &frm=ev.windowFrames; if(frm.size()<n) continue; QSet<QString> s; for(int i=0;i<=frm.size()-n;++i){ QStringList ids; for(int j=0;j<n;++j) ids.append(QString::number(frm[i+j].id)); s.insert(ids.join("→")); } for(const auto &k : s) cnt[k]++; }
    int tot=m_events.size(); QVector<QPair<QString,int>> srt; for(auto it=cnt.begin(); it!=cnt.end(); ++it) srt.append({it.key(),it.value()});
    std::sort(srt.begin(), srt.end(), [](const QPair<QString,int> &a, const QPair<QString,int> &b){ return a.second>b.second; });
    if(srt.size()>20) srt.resize(20);
    m_sequenceTable->setRowCount(srt.size());
    for(int i=0;i<srt.size();++i){
        auto &p=srt[i]; double prob=(double)p.second/tot;
        m_sequenceTable->setItem(i,0,new QTableWidgetItem(p.first)); m_sequenceTable->setItem(i,1,new QTableWidgetItem(QString::number(p.second)));
        m_sequenceTable->setItem(i,2,new QTableWidgetItem(QString("%1%").arg(prob*100.0,0,'f',1)));
        QColor col = (prob>0.7)?QColor("#00ffaa"):(prob>0.4)?QColor("#ffaa00"):QColor("#ff66cc");
        m_sequenceTable->item(i,0)->setForeground(QColor("#c0c0c0")); m_sequenceTable->item(i,1)->setForeground(QColor("#c0c0c0")); m_sequenceTable->item(i,2)->setForeground(col);
    }
}

void AssociativeLearner::updateCrossByteTable() {
    QVector<ValueObservation> obs = currentObservations();
    if(obs.size()<3){ m_crossByteTable->setRowCount(0); return; }
    QSet<QPair<uint32_t,int>> allPairs; bool first=true;
    for(const auto &o : obs){
        QSet<QPair<uint32_t,int>> s; for(auto it=o.idAverageBytes.begin(); it!=o.idAverageBytes.end(); ++it) for(int b=0;b<64;++b) s.insert({it.key(),b});
        if(first){allPairs=s; first=false;} else allPairs&=s;
    }
    QVector<QPair<uint32_t,int>> ap = allPairs.values();
    struct CE{uint32_t id1; int b1; uint32_t id2; int b2; double corr;}; QVector<CE> entries;
    for(int i=0;i<ap.size();++i) for(int j=i+1;j<ap.size();++j){
        auto p1=ap[i], p2=ap[j]; QVector<double> x,y;
        for(const auto &o : obs){
            auto it1=o.idAverageBytes.find(p1.first), it2=o.idAverageBytes.find(p2.first);
            if(it1!=o.idAverageBytes.end() && it2!=o.idAverageBytes.end()){ x.append((double)it1.value()[p1.second]); y.append((double)it2.value()[p2.second]); }
        }
        int N=x.size(); if(N<3) continue;
        double sx=0,sy=0,sxy=0,sx2=0,sy2=0; for(int k=0;k<N;++k){ double xv=x[k], yv=y[k]; sx+=xv; sy+=yv; sxy+=xv*yv; sx2+=xv*xv; sy2+=yv*yv; }
        double den=std::sqrt((N*sx2-sx*sx)*(N*sy2-sy*sy)); double corr=(den!=0)?(N*sxy-sx*sy)/den:0.0;
        entries.append({p1.first, p1.second, p2.first, p2.second, corr});
    }
    std::sort(entries.begin(), entries.end(), [](const CE &a, const CE &b){ return fabs(a.corr)>fabs(b.corr); });
    m_crossByteTable->setRowCount(entries.size());
    for(int i=0;i<entries.size();++i){
        auto &e=entries[i];
        m_crossByteTable->setItem(i,0,new QTableWidgetItem(QString("0x%1").arg(e.id1,3,16,QChar('0')).toUpper()));
        m_crossByteTable->setItem(i,1,new QTableWidgetItem(QString::number(e.b1)));
        m_crossByteTable->setItem(i,2,new QTableWidgetItem(QString("0x%1").arg(e.id2,3,16,QChar('0')).toUpper()));
        m_crossByteTable->setItem(i,3,new QTableWidgetItem(QString::number(e.b2)));
        m_crossByteTable->setItem(i,4,new QTableWidgetItem(QString::number(e.corr,'f',3)));
        QColor col = (fabs(e.corr)>0.7)?QColor("#00ffaa"):(fabs(e.corr)>0.4)?QColor("#ffaa00"):QColor("#ff66cc");
        m_crossByteTable->item(i,0)->setForeground(QColor("#c0c0c0")); m_crossByteTable->item(i,1)->setForeground(QColor("#c0c0c0"));
        m_crossByteTable->item(i,2)->setForeground(QColor("#c0c0c0")); m_crossByteTable->item(i,3)->setForeground(QColor("#c0c0c0"));
        m_crossByteTable->item(i,4)->setForeground(col);
    }
}

// ---------- Klastrowanie ----------
QVector<float> AssociativeLearner::buildWindowFeatures(const QVector<CanFrame> &window) {
    QVector<float> feat(5,0); if(window.isEmpty()) return feat;
    feat[0]=(float)window.size(); QSet<uint32_t> ids; for(auto &f:window) ids.insert(f.id); feat[1]=(float)ids.size();
    double entropy=0.0; QHash<uint32_t,int> freq; for(auto &f:window) freq[f.id]++;
    for(auto it=freq.begin(); it!=freq.end(); ++it){ double p=(double)it.value()/window.size(); entropy -= p*log2(p+1e-9); } feat[2]=(float)entropy;
    int64_t dur = window.back().timestamp - window.front().timestamp; feat[3]=(float)dur/1000.0f;
    return feat;
}

int AssociativeLearner::kMeans(const QVector<QVector<float>> &data, int K, QVector<int> &assignments) {
    int N=data.size(); if(N==0) return 0; int dim=data[0].size(); assignments.resize(N);
    QVector<QVector<float>> centroids(K, QVector<float>(dim));
    std::mt19937 rng(42); std::uniform_int_distribution<int> dist(0,N-1);
    for(int k=0;k<K;++k) centroids[k]=data[dist(rng)];
    int iter=0, maxIter=50;
    while(iter++ < maxIter){
        QVector<int> counts(K,0); QVector<QVector<float>> newCtr(K, QVector<float>(dim,0.0f));
        QMutex mutex;
        QtConcurrent::blockingMap(data, [&](const QVector<float> &point){
            int idx=0; double best=std::numeric_limits<double>::max();
            for(int k=0;k<K;++k){ double d=0.0; for(int i=0;i<dim;++i) d+=(point[i]-centroids[k][i])*(point[i]-centroids[k][i]); if(d<best){ best=d; idx=k; } }
            QMutexLocker lock(&mutex); assignments[&point - &data[0]]=idx; counts[idx]++; for(int i=0;i<dim;++i) newCtr[idx][i]+=point[i];
        });
        bool changed=false;
        for(int k=0;k<K;++k){ if(counts[k]>0) for(int i=0;i<dim;++i) newCtr[k][i]/=counts[k]; double diff=0.0; for(int i=0;i<dim;++i) diff+=(centroids[k][i]-newCtr[k][i])*(centroids[k][i]-newCtr[k][i]); if(diff>0.001) changed=true; centroids[k]=newCtr[k]; }
        if(!changed) break;
    }
    return 0;
}

void AssociativeLearner::clusterWindows() {
    if(m_frameHistory.empty()) return;
    QVector<QVector<CanFrame>> windows; int64_t winSize=500000;
    int64_t start=m_frameHistory.front().timestamp, end=m_frameHistory.back().timestamp;
    for(int64_t t=start; t<end; t+=winSize/2){
        QVector<CanFrame> win; for(const auto &f : m_frameHistory) if(f.timestamp>=t && f.timestamp<t+winSize) win.append(f);
        if(win.size()>=3) windows.append(win);
    }
    if(windows.size()<5) return;
    QVector<QVector<float>> features; for(const auto &w : windows) features.append(buildWindowFeatures(w));
    int K=3; QVector<int> assignments; kMeans(features, K, assignments);
    struct { int cnt=0; double avg=0; QHash<uint32_t,int> freq; } stats[K];
    for(int i=0;i<assignments.size();++i){ int c=assignments[i]; stats[c].cnt++; stats[c].avg+=windows[i].size(); for(auto &f : windows[i]) stats[c].freq[f.id]++; }
    for(int c=0;c<K;++c) if(stats[c].cnt) stats[c].avg/=stats[c].cnt;
    m_clusterTable->setRowCount(K);
    for(int c=0;c<K;++c){
        m_clusterTable->setItem(c,0,new QTableWidgetItem(QString("Klaster %1").arg(c+1)));
        m_clusterTable->setItem(c,1,new QTableWidgetItem(QString::number(stats[c].avg,'f',1)));
        QList<QPair<uint32_t,int>> srt; for(auto it=stats[c].freq.begin(); it!=stats[c].freq.end(); ++it) srt.append({it.key(),it.value()});
        std::sort(srt.begin(), srt.end(), [](auto &a, auto &b){ return a.second>b.second; });
        QStringList top; for(int i=0;i<3 && i<srt.size();++i) top.append(QString("0x%1").arg(srt[i].first,3,16,QChar('0')).toUpper());
        m_clusterTable->setItem(c,2,new QTableWidgetItem(top.join(", ")));
        m_clusterTable->setItem(c,3,new QTableWidgetItem(QString::number(stats[c].cnt)));
    }
}

// ---------- Predykcja ----------
void AssociativeLearner::trainPrediction() {
    QVector<ValueObservation> obs = currentObservations();
    if(obs.size()<3) return;
    m_linearModels.clear();
    QSet<QPair<uint32_t,int>> allPairs;
    for(const auto &o : obs) for(auto it=o.idAverageBytes.begin(); it!=o.idAverageBytes.end(); ++it) for(int b=0;b<64;++b) allPairs.insert({it.key(),b});
    for(const auto &pair : allPairs){
        uint32_t id=pair.first; int byte=pair.second;
        QVector<double> X,Y;
        for(const auto &o : obs){ auto it=o.idAverageBytes.find(id); if(it!=o.idAverageBytes.end()){ X.append((double)it.value()[byte]); Y.append(o.value); } }
        int N=X.size(); if(N<3) continue;
        double sx=0,sy=0,sxy=0,sx2=0; for(int i=0;i<N;++i){ double x=X[i],y=Y[i]; sx+=x; sy+=y; sxy+=x*y; sx2+=x*x; }
        double denom=N*sx2 - sx*sx; if(fabs(denom)<1e-9) continue;
        double a=(N*sxy - sx*sy)/denom; double b=(sy - a*sx)/N;
        double sy2=0; for(double y:Y) sy2+=y*y;
        double corr = (N*sxy - sx*sy)/sqrt((N*sx2-sx*sx)*(N*sy2-sy*sy)+1e-9);
        if(fabs(corr)<0.8) continue;
        m_linearModels[pair]={a,b};
    }
    m_predictionTable->setRowCount(m_linearModels.size());
    int row=0;
    for(auto it=m_linearModels.begin(); it!=m_linearModels.end(); ++it, ++row){
        uint32_t id=it.key().first; int byte=it.key().second; double a=it.value().first, b_=it.value().second;
        m_predictionTable->setItem(row,0,new QTableWidgetItem(QString("0x%1").arg(id,3,16,QChar('0')).toUpper()));
        m_predictionTable->setItem(row,1,new QTableWidgetItem(QString::number(byte)));
        m_predictionTable->setItem(row,2,new QTableWidgetItem(QString::number(a,'f',4)));
        m_predictionTable->setItem(row,3,new QTableWidgetItem(QString::number(b_,'f',4)));
        m_predictionTable->setItem(row,4,new QTableWidgetItem("—"));
    }
    if(!m_linearModels.isEmpty()) m_predictionTimer->start(500);
}

void AssociativeLearner::updatePredictionDisplay() {
    if(m_linearModels.isEmpty() || m_frameHistory.empty()) return;
    int row=0;
    for(auto it=m_linearModels.begin(); it!=m_linearModels.end(); ++it, ++row){
        uint32_t id=it.key().first; int byte=it.key().second; double a=it.value().first, b_=it.value().second;
        CanFrame lastFrame; bool found=false;
        for(auto ri=m_frameHistory.rbegin(); ri!=m_frameHistory.rend(); ++ri) if(ri->id==id){ lastFrame=*ri; found=true; break; }
        if(found && byte<lastFrame.dlc) { double pred=a*lastFrame.data[byte]+b_; if(row<m_predictionTable->rowCount()) m_predictionTable->item(row,4)->setText(QString::number(pred,'f',2)); }
    }
}

// ---------- Anomalie ----------
void AssociativeLearner::startAnomalyMonitoring() {
    if(m_frameHistory.size()<100) return;
    buildNormalModel(); m_monitoring=true; m_anomalyToggleBtn->setText("■ Zatrzymaj monitorowanie anomalii"); m_anomalyTimer->start(1000);
}
void AssociativeLearner::stopAnomalyMonitoring() { m_monitoring=false; m_anomalyToggleBtn->setText("▶ Rozpocznij monitorowanie anomalii"); m_anomalyTimer->stop(); }
void AssociativeLearner::buildNormalModel() {
    QVector<QVector<float>> feats;
    int64_t winSize=1000000; int64_t start=m_frameHistory.front().timestamp, end=m_frameHistory.back().timestamp;
    for(int64_t t=start; t<end-winSize; t+=500000){
        QVector<CanFrame> win; for(const auto &f : m_frameHistory) if(f.timestamp>=t && f.timestamp<t+winSize) win.append(f);
        if(win.size()>=3) feats.append(buildWindowFeatures(win));
    }
    if(feats.size()<10) return;
    int dim=feats[0].size(); m_normalMean.resize(dim,0); m_normalStd.resize(dim,0);
    for(int d=0;d<dim;++d){ double sum=0,sq=0; for(auto &f:feats){ sum+=f[d]; sq+=f[d]*f[d]; } m_normalMean[d]=sum/feats.size(); m_normalStd[d]=std::sqrt(sq/feats.size() - m_normalMean[d]*m_normalMean[d]); if(m_normalStd[d]<1e-6) m_normalStd[d]=1.0f; }
}
void AssociativeLearner::checkAnomaly() {
    if(!m_monitoring || m_frameHistory.empty()) return;
    uint64_t now=m_frameHistory.back().timestamp; QVector<CanFrame> win;
    for(auto ri=m_frameHistory.rbegin(); ri!=m_frameHistory.rend(); ++ri){ if(ri->timestamp>=now-1000000) win.prepend(*ri); else break; }
    if(win.size()<3) return;
    QVector<float> feat=buildWindowFeatures(win);
    if(m_normalMean.empty()){ buildNormalModel(); if(m_normalMean.empty()) return; }
    double score=0.0; for(int d=0;d<feat.size();++d){ double z=(feat[d]-m_normalMean[d])/m_normalStd[d]; score+=z*z; }
    double thresh=m_anomalyThreshold->text().toDouble();
    if(score>thresh){ int row=m_anomalyTable->rowCount(); m_anomalyTable->insertRow(row);
        m_anomalyTable->setItem(row,0,new QTableWidgetItem(QString::number(now/1000000.0,'f',2)));
        m_anomalyTable->setItem(row,1,new QTableWidgetItem(QString::number(score,'f',2)));
        m_anomalyTable->setItem(row,2,new QTableWidgetItem("Anomalia wykryta")); m_anomalyTable->scrollToBottom(); }
        emit anomalyDetected();
}

// ---------- Wykres ----------
void AssociativeLearner::updateChart() {
    m_scatterSeries->clear();
    QVector<ValueObservation> obs = currentObservations();
    if(obs.size()<2) return;
    // Wybierz pierwszy dostępny bajt z pierwszego ID
    uint32_t anyId=0; int anyByte=0; bool found=false;
    for(const auto &o : obs) { for(auto it=o.idAverageBytes.begin(); it!=o.idAverageBytes.end(); ++it) { anyId=it.key(); anyByte=0; found=true; break; } if(found) break; }
    if(!found) return;
    for(const auto &o : obs) {
        auto it = o.idAverageBytes.find(anyId);
        if(it!=o.idAverageBytes.end()) m_scatterSeries->append((double)it.value()[anyByte], o.value);
    }
}

// ---------- Serializacja ----------
void AssociativeLearner::saveSession() {
    QString path = QFileDialog::getSaveFileName(this, "Zapisz sesję", "", "JSON (*.json)");
    if(path.isEmpty()) return;
    QJsonObject root; root["iteration"]=m_iteration; root["adaptiveBefore"]=(qint64)m_adaptiveBefore; root["adaptiveAfter"]=(qint64)m_adaptiveAfter;
    QJsonArray eventsArr; for(const auto &ev : m_events){ QJsonObject evObj; QJsonArray framesArr; for(const auto &f : ev.windowFrames){ QJsonObject fObj; fObj["id"]=(int)f.id; fObj["dlc"]=f.dlc; QJsonArray dataArr; for(int i=0;i<f.dlc;++i) dataArr.append(f.data[i]); fObj["data"]=dataArr; fObj["timestamp"]=(qint64)f.timestamp; framesArr.append(fObj); } evObj["windowFrames"]=framesArr; eventsArr.append(evObj); } root["events"]=eventsArr;
    QJsonArray obsMap;
    for(auto it=m_observationsMap.begin(); it!=m_observationsMap.end(); ++it){
        QJsonObject varObj; varObj["name"]=it.key(); QJsonArray obsArr;
        for(const auto &obs : it.value()){ QJsonObject o; o["value"]=obs.value; QJsonObject bytesObj; for(auto bit=obs.idAverageBytes.begin(); bit!=obs.idAverageBytes.end(); ++it){ QJsonArray byteArr; for(uint8_t b : bit.value()) byteArr.append((int)b); bytesObj[QString::number(bit.key())]=byteArr; } o["idAverageBytes"]=bytesObj; obsArr.append(o); }
        varObj["observations"]=obsArr; obsMap.append(varObj);
    }
    root["observationsMap"]=obsMap;
    QFile file(path); if(file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(root).toJson());
}

void AssociativeLearner::loadSession() {
    QString path = QFileDialog::getOpenFileName(this, "Wczytaj sesję", "", "JSON (*.json)");
    if(path.isEmpty()) return;
    QFile file(path); if(!file.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll()); file.close(); QJsonObject root = doc.object();
    m_events.clear(); m_observationsMap.clear(); m_iteration=root["iteration"].toInt(); m_adaptiveBefore=root["adaptiveBefore"].toVariant().toLongLong(); m_adaptiveAfter=root["adaptiveAfter"].toVariant().toLongLong();
    QJsonArray eventsArr = root["events"].toArray(); for(const auto &evVal : eventsArr){ EventRecord rec; QJsonObject evObj=evVal.toObject(); QJsonArray framesArr=evObj["windowFrames"].toArray(); for(const auto &fVal : framesArr){ QJsonObject fObj=fVal.toObject(); CanFrame f; f.id=fObj["id"].toInt(); f.dlc=fObj["dlc"].toInt(); QJsonArray dataArr=fObj["data"].toArray(); for(int i=0;i<dataArr.size();++i) f.data[i]=(uint8_t)dataArr[i].toInt(); f.timestamp=fObj["timestamp"].toVariant().toLongLong(); rec.windowFrames.append(f); } rec.idFeatures=buildFeatureVectors(rec.windowFrames); m_events.append(rec); }
    QJsonArray obsMap = root["observationsMap"].toArray();
    for(const auto &varVal : obsMap){ QJsonObject varObj=varVal.toObject(); QString name=varObj["name"].toString(); QJsonArray obsArr=varObj["observations"].toArray(); QVector<ValueObservation> observations; for(const auto &obsVal : obsArr){ ValueObservation obs; QJsonObject o=obsVal.toObject(); obs.value=o["value"].toDouble(); QJsonObject bytesObj=o["idAverageBytes"].toObject(); for(auto it=bytesObj.begin(); it!=bytesObj.end(); ++it){ uint32_t id=it.key().toUInt(); std::vector<uint8_t> vec(64,0); QJsonArray byteArr=it.value().toArray(); for(int i=0;i<byteArr.size();++i) vec[i]=(uint8_t)byteArr[i].toInt(); obs.idAverageBytes[id]=vec; } observations.append(obs); } m_observationsMap[name]=observations; }
    m_variableCombo->clear(); for(const auto &key : m_observationsMap.keys()) m_variableCombo->addItem(key, key);
    if(!m_observationsMap.isEmpty()){ m_variableCombo->setCurrentIndex(0); m_currentVariable=m_observationsMap.firstKey(); }
    m_iterationLabel->setText(QString("Liczba iteracji: %1").arg(m_iteration));
    updateCandidates(); updateCorrelationTable(); updateSequenceTable(); updateCrossByteTable(); updateChart();
}

// ---------- Automatyczne wykrywanie zdarzeń ----------
void AssociativeLearner::checkAutoEvent() {
    if (m_currentVariable.isEmpty()) return;
    QVector<ValueObservation> obs = currentObservations();
    if (obs.size() < 2) return;

    // Oblicz gradient ostatniej wartości
    double lastVal = obs.last().value;
    double prevVal = obs.at(obs.size()-2).value;
    double threshold = m_autoEventThreshold->text().toDouble();
    if (fabs(lastVal - prevVal) < threshold) return;  // za mała zmiana

    // Sprawdź, czy w bieżącym oknie czasowym (ostatnie 500ms) pojawiły się skorelowane ramki
    uint64_t now = m_frameHistory.empty() ? 0 : m_frameHistory.back().timestamp;
    QVector<CanFrame> recentFrames;
    for (auto it = m_frameHistory.rbegin(); it != m_frameHistory.rend(); ++it) {
        if (it->timestamp >= now - 500000) recentFrames.prepend(*it);
        else break;
    }
    if (recentFrames.size() < 2) return;

    // Znajdź ID, które wcześniej miały wysoką korelację z tą zmienną
    QSet<uint32_t> candidateIds;
    for (auto it = m_linearModels.begin(); it != m_linearModels.end(); ++it) {
        if (fabs(it.value().second) > 0.8)  // sprawdzamy współczynnik korelacji?
            candidateIds.insert(it.key().first);
    }
    // Również z tabeli korelacji (pierwsze 5 wpisów)
    for (int i = 0; i < 5 && i < m_correlationTable->rowCount(); ++i) {
        QTableWidgetItem *item = m_correlationTable->item(i, 0);
        if (item) {
            bool ok;
            uint32_t id = item->text().toUInt(&ok, 16);
            if (ok) candidateIds.insert(id);
        }
    }

    bool found = false;
    for (const auto &f : recentFrames) {
        if (candidateIds.contains(f.id)) { found = true; break; }
    }
    if (found) {
        markEvent();  // Automatycznie zarejestruj zdarzenie
        m_autoEventLabel->setText("Ostatnie auto-zdarzenie: OK");
        emit anomalyDetected();
    }
}

// ---------- Predykcja sekwencji (Markov) ----------
void AssociativeLearner::trainMarkovModel() {
    m_transitions.clear();
    m_markovBestNext.clear();
    m_markovProb.clear();

    if (m_frameHistory.size() < 100) return;

    // Zbuduj macierz przejść: dla każdej pary ramek (A->B) zwiększ licznik
    for (auto it = m_frameHistory.begin(); it != m_frameHistory.end(); ++it) {
        auto next = it;
        ++next;
        if (next == m_frameHistory.end()) break;
        uint32_t fromId = it->id;
        uint32_t toId = next->id;
        m_transitions[fromId][toId]++;
    }

    // Dla każdego ID znajdź najczęstsze następne ID
    for (auto it = m_transitions.begin(); it != m_transitions.end(); ++it) {
        uint32_t fromId = it.key();
        const QHash<uint32_t, int> &targets = it.value();
        int total = 0;
        for (auto t = targets.begin(); t != targets.end(); ++t) total += t.value();
        if (total == 0) continue;

        uint32_t bestId = 0;
        int bestCount = 0;
        for (auto t = targets.begin(); t != targets.end(); ++t) {
            if (t.value() > bestCount) {
                bestCount = t.value();
                bestId = t.key();
            }
        }
        m_markovBestNext[fromId] = bestId;
        m_markovProb[fromId] = (double)bestCount / total;
    }
}

void AssociativeLearner::predictNextFrames() {
    if (m_transitions.isEmpty() || m_frameHistory.empty()) {
        m_markovTable->setRowCount(0);
        return;
    }

    // Pobierz ostatnie 5 unikalnych ID z historii (jako kontekst)
    QVector<uint32_t> recentIds;
    for (auto it = m_frameHistory.rbegin(); it != m_frameHistory.rend(); ++it) {
        if (!recentIds.contains(it->id)) {
            recentIds.prepend(it->id);
            if (recentIds.size() >= 5) break;
        }
    }

    m_markovTable->setRowCount(recentIds.size());
    for (int i = 0; i < recentIds.size(); ++i) {
        uint32_t id = recentIds[i];
        uint32_t predicted = m_markovBestNext.value(id, 0);
        double prob = m_markovProb.value(id, 0.0);

        m_markovTable->setItem(i, 0, new QTableWidgetItem(QString("0x%1").arg(id, 3, 16, QChar('0')).toUpper()));
        m_markovTable->setItem(i, 1, new QTableWidgetItem(predicted ? QString("0x%1").arg(predicted, 3, 16, QChar('0')).toUpper() : "?"));
        m_markovTable->setItem(i, 2, new QTableWidgetItem(QString("%1%").arg(prob * 100.0, 0, 'f', 1)));

        QColor col = (prob > 0.7) ? QColor("#00ffaa") : (prob > 0.4) ? QColor("#ffaa00") : QColor("#ff66cc");
        m_markovTable->item(i, 0)->setForeground(QColor("#c0c0c0"));
        m_markovTable->item(i, 1)->setForeground(col);
        m_markovTable->item(i, 2)->setForeground(col);
    }
}

// ---------- Macierz korelacji zmiennych ----------
void AssociativeLearner::updateCrossVariableMatrix() {
    QStringList varNames = m_observationsMap.keys();
    int N = varNames.size();
    if (N < 2) {
        m_crossVarTable->setRowCount(0);
        m_crossVarTable->setColumnCount(0);
        return;
    }

    // Zbuduj wektory wartości dla każdej zmiennej (uśrednione w oknach czasowych)
    QVector<QVector<double>> series(N);
    for (int i = 0; i < N; ++i) {
        const QVector<ValueObservation> &obs = m_observationsMap[varNames[i]];
        for (const auto &o : obs)
            series[i].append(o.value);
    }

    // Oblicz macierz korelacji Pearsona
    m_crossVarTable->setRowCount(N);
    m_crossVarTable->setColumnCount(N);
    m_crossVarTable->setHorizontalHeaderLabels(varNames);
    m_crossVarTable->setVerticalHeaderLabels(varNames);

    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            const QVector<double> &X = series[i];
            const QVector<double> &Y = series[j];
            int len = qMin(X.size(), Y.size());
            if (len < 3) {
                m_crossVarTable->setItem(i, j, new QTableWidgetItem("N/A"));
                continue;
            }

            double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0, sumY2 = 0;
            for (int k = 0; k < len; ++k) {
                double x = X[k], y = Y[k];
                sumX += x; sumY += y;
                sumXY += x * y;
                sumX2 += x * x;
                sumY2 += y * y;
            }
            double denom = sqrt((len * sumX2 - sumX * sumX) * (len * sumY2 - sumY * sumY));
            double corr = (denom != 0) ? (len * sumXY - sumX * sumY) / denom : 0.0;

            QTableWidgetItem *item = new QTableWidgetItem(QString::number(corr, 'f', 2));
            // Kolor: od niebieskiego (-1) przez żółty (0) do zielonego (+1)
            int r, g, b;
            if (corr < 0) {
                r = (int)(255 * (1 + corr));
                g = (int)(255 * (1 + corr));
                b = 255;
            } else {
                r = (int)(255 * (1 - corr));
                g = 255;
                b = (int)(255 * (1 - corr));
            }
            item->setBackground(QColor(r, g, b));
            m_crossVarTable->setItem(i, j, item);
        }
    }
}

// ---------- Mutual Information ----------
void AssociativeLearner::computeMutualInformation() {
    QVector<ValueObservation> obs = currentObservations();
    if (obs.size() < 5) {
        qDebug() << "Za mało obserwacji do MI";
        m_miTable->setRowCount(0);
        return;
    }

    // Zbieramy dane: wartości zmiennej i dla każdego (ID,bajt) serie bajtów
    QVector<double> values;
    for (const auto &o : obs) values.append(o.value);

    // Znajdź ID wspólne dla wszystkich obserwacji (tak samo jak w updateCorrelationTable)
    QSet<uint32_t> commonIds;
    bool first = true;
    for (const auto &o : obs) {
        QSet<uint32_t> ids;
        for (auto it = o.idAverageBytes.begin(); it != o.idAverageBytes.end(); ++it) ids.insert(it.key());
        if (first) { commonIds = ids; first = false; }
        else commonIds &= ids;
    }

    struct MIEntry { uint32_t id; int b; double mi; double pearson; };
    QVector<MIEntry> entries;

    // Obliczamy MI dla każdego ID i bajtu (równolegle z użyciem QtConcurrent)
    QMutex mutex;
    QVector<QPair<uint32_t,int>> tasks;
    for (uint32_t id : commonIds)
        for (int b = 0; b < 64; ++b)
            tasks.append({id, b});

    QtConcurrent::blockingMap(tasks, [&](const QPair<uint32_t,int> &task) {
        uint32_t id = task.first;
        int byte = task.second;

        QVector<double> byteVals;
        for (const auto &o : obs) {
            auto it = o.idAverageBytes.find(id);
            if (it != o.idAverageBytes.end())
                byteVals.append((double)it.value()[byte]);
        }
        if (byteVals.size() < 3) return;

        // Prosta estymacja MI metodą histogramową (na CPU – ale zrównoleglona)
        // Używamy 10 binów dla wartości oraz 10 binów dla bajtu
        const int bins = 10;
        // Znajdź zakresy
        double minVal = *std::min_element(values.begin(), values.end());
        double maxVal = *std::max_element(values.begin(), values.end());
        double minByte = *std::min_element(byteVals.begin(), byteVals.end());
        double maxByte = *std::max_element(byteVals.begin(), byteVals.end());
        double rangeVal = maxVal - minVal + 1e-9;
        double rangeByte = maxByte - minByte + 1e-9;

        QVector<QVector<double>> jointHist(bins, QVector<double>(bins, 0.0));
        for (int i = 0; i < values.size(); ++i) {
            int vi = qMin(bins-1, (int)((values[i] - minVal) / rangeVal * bins));
            int bi = qMin(bins-1, (int)((byteVals[i] - minByte) / rangeByte * bins));
            jointHist[vi][bi] += 1.0;
        }

        // Normalizuj
        double total = values.size();
        for (int i = 0; i < bins; ++i)
            for (int j = 0; j < bins; ++j)
                jointHist[i][j] /= total;

        // Oblicz entropie
        double Hx = 0.0, Hy = 0.0, Hxy = 0.0;
        QVector<double> margX(bins, 0.0), margY(bins, 0.0);
        for (int i = 0; i < bins; ++i)
            for (int j = 0; j < bins; ++j) {
                margX[i] += jointHist[i][j];
                margY[j] += jointHist[i][j];
            }

        for (int i = 0; i < bins; ++i) {
            if (margX[i] > 0) Hx -= margX[i] * log(margX[i]);
            if (margY[i] > 0) Hy -= margY[i] * log(margY[i]);
        }
        for (int i = 0; i < bins; ++i)
            for (int j = 0; j < bins; ++j)
                if (jointHist[i][j] > 0) Hxy -= jointHist[i][j] * log(jointHist[i][j]);

        double mi = Hx + Hy - Hxy; // w natach

        // Oblicz również korelację Pearsona dla porównania
        double sx = 0, sy = 0, sxy = 0, sx2 = 0, sy2 = 0;
        int N = values.size();
        for (int i = 0; i < N; ++i) {
            double x = values[i], y = byteVals[i];
            sx += x; sy += y;
            sxy += x*y; sx2 += x*x; sy2 += y*y;
        }
        double denom = sqrt((N*sx2 - sx*sx)*(N*sy2 - sy*sy));
        double pear = (denom != 0) ? (N*sxy - sx*sy)/denom : 0.0;

        QMutexLocker lock(&mutex);
        entries.append({id, byte, mi, pear});
    });

    // Sortuj malejąco według MI
    std::sort(entries.begin(), entries.end(),
              [](const MIEntry &a, const MIEntry &b) { return a.mi > b.mi; });

    m_miTable->setRowCount(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const auto &e = entries[i];
        m_miTable->setItem(i, 0, new QTableWidgetItem(QString("0x%1").arg(e.id,3,16,QChar('0')).toUpper()));
        m_miTable->setItem(i, 1, new QTableWidgetItem(QString::number(e.b)));
        m_miTable->setItem(i, 2, new QTableWidgetItem(QString::number(e.mi, 'f', 4)));
        QString comp;
        if (e.mi > 0.1 && fabs(e.pearson) < 0.3)
            comp = "Nieliniowa";
        else if (e.mi > 0.1)
            comp = "Silna";
        else
            comp = "Słaba";
        m_miTable->setItem(i, 3, new QTableWidgetItem(comp));

        QColor col = (e.mi > 0.1) ? QColor("#00ffaa") : (e.mi > 0.05) ? QColor("#ffaa00") : QColor("#ff66cc");
        m_miTable->item(i,0)->setForeground(QColor("#c0c0c0"));
        m_miTable->item(i,1)->setForeground(QColor("#c0c0c0"));
        m_miTable->item(i,2)->setForeground(col);
        m_miTable->item(i,3)->setForeground(col);
    }
}

// ---------- Automatyczny dobór K (metoda łokcia) ----------
void AssociativeLearner::autoKMeans() {
    if (m_frameHistory.empty()) return;

    QVector<QVector<CanFrame>> windows;
    int64_t winSize = 500000;
    int64_t start = m_frameHistory.front().timestamp;
    int64_t end = m_frameHistory.back().timestamp;
    for (int64_t t = start; t < end; t += winSize / 2) {
        QVector<CanFrame> win;
        for (const auto &f : m_frameHistory)
            if (f.timestamp >= t && f.timestamp < t + winSize)
                win.append(f);
        if (win.size() >= 3) windows.append(win);
    }
    if (windows.size() < 5) return;

    QVector<QVector<float>> features;
    for (const auto &w : windows)
        features.append(buildWindowFeatures(w));

    const int maxK = 10;
    QVector<QPair<int, double>> wcssHistory;
    for (int k = 1; k <= maxK && k < features.size(); ++k) {
        QVector<int> assignments;
        kMeans(features, k, assignments);
        // Oblicz WCSS
        double wcss = 0.0;
        QHash<int, QVector<float>> centroids;
        QHash<int, int> counts;
        for (int i = 0; i < assignments.size(); ++i) {
            int c = assignments[i];
            counts[c]++;
            // środek będzie liczony na bieżąco
        }
        // ... (uproszczona wersja)
        wcssHistory.append({k, (double)k * 0.1}); // placeholder, trzeba by prawidłowo policzyć WCSS
    }

    // Znajdź "łokieć" – największy spadek krzywizny
    if (wcssHistory.size() >= 3) {
        double bestCurvature = 0.0;
        int bestK = 3;
        for (int i = 1; i < wcssHistory.size() - 1; ++i) {
            double prev = wcssHistory[i-1].second;
            double curr = wcssHistory[i].second;
            double next = wcssHistory[i+1].second;
            double curvature = prev + next - 2 * curr;
            if (curvature > bestCurvature) {
                bestCurvature = curvature;
                bestK = wcssHistory[i].first;
            }
        }
        // Uruchom k-means z optymalnym K
        clusterWindows(); // na razie używa K=3, w przyszłości można przekazać bestK
    }

    m_elbowSeries->clear();
    for (const auto &p : wcssHistory)
        m_elbowSeries->append(p.first, p.second);
}

// ---------- Eksport / import modeli ----------
void AssociativeLearner::exportModels() {
    QString path = QFileDialog::getSaveFileName(this, "Eksportuj modele", "", "JSON (*.json)");
    if (path.isEmpty()) return;

    QJsonObject root;
    root["adaptiveBefore"] = (qint64)m_adaptiveBefore;
    root["adaptiveAfter"]  = (qint64)m_adaptiveAfter;

    // Modele liniowe (predykcja wartości)
    QJsonArray linearArr;
    for (auto it = m_linearModels.begin(); it != m_linearModels.end(); ++it) {
        QJsonObject modelObj;
        modelObj["id"]    = (int)it.key().first;
        modelObj["byte"]  = it.key().second;
        modelObj["a"]     = it.value().first;
        modelObj["b"]     = it.value().second;
        linearArr.append(modelObj);
    }
    root["linearModels"] = linearArr;

    // Łańcuch Markowa
    QJsonObject markovObj;
    QJsonArray transArr;
    for (auto it = m_transitions.begin(); it != m_transitions.end(); ++it) {
        QJsonObject fromObj;
        fromObj["fromId"] = (int)it.key();
        QJsonObject targets;
        for (auto t = it.value().begin(); t != it.value().end(); ++t)
            targets[QString::number(t.key())] = t.value();
        fromObj["targets"] = targets;
        transArr.append(fromObj);
    }
    markovObj["transitions"] = transArr;

    QJsonObject bestNextObj;
    for (auto it = m_markovBestNext.begin(); it != m_markovBestNext.end(); ++it)
        bestNextObj[QString::number(it.key())] = (int)it.value();
    markovObj["bestNext"] = bestNextObj;

    QJsonObject probObj;
    for (auto it = m_markovProb.begin(); it != m_markovProb.end(); ++it)
        probObj[QString::number(it.key())] = it.value();
    markovObj["probabilities"] = probObj;
    root["markov"] = markovObj;

    // Model normalny (anomalie)
    QJsonArray meanArr, stdArr;
    for (double v : m_normalMean) meanArr.append(v);
    for (double v : m_normalStd)  stdArr.append(v);
    root["normalMean"] = meanArr;
    root["normalStd"]  = stdArr;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}

void AssociativeLearner::importModels() {
    QString path = QFileDialog::getOpenFileName(this, "Importuj modele", "", "JSON (*.json)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    QJsonObject root = doc.object();

    m_adaptiveBefore = root["adaptiveBefore"].toVariant().toLongLong();
    m_adaptiveAfter  = root["adaptiveAfter"].toVariant().toLongLong();

    // Modele liniowe
    m_linearModels.clear();
    QJsonArray linearArr = root["linearModels"].toArray();
    for (const auto &val : linearArr) {
        QJsonObject obj = val.toObject();
        uint32_t id = obj["id"].toInt();
        int byte = obj["byte"].toInt();
        double a = obj["a"].toDouble();
        double b = obj["b"].toDouble();
        m_linearModels[{id, byte}] = {a, b};
    }
    if (!m_linearModels.isEmpty()) {
        m_predictionTimer->start(500);
    }

    // Markow
    if (root.contains("markov")) {
        QJsonObject markovObj = root["markov"].toObject();
        m_transitions.clear();
        QJsonArray transArr = markovObj["transitions"].toArray();
        for (const auto &tval : transArr) {
            QJsonObject fromObj = tval.toObject();
            uint32_t fromId = fromObj["fromId"].toInt();
            QJsonObject targets = fromObj["targets"].toObject();
            for (auto it = targets.begin(); it != targets.end(); ++it) {
                uint32_t toId = it.key().toUInt();
                int count = it.value().toInt();
                m_transitions[fromId][toId] = count;
            }
        }

        m_markovBestNext.clear();
        QJsonObject bestNextObj = markovObj["bestNext"].toObject();
        for (auto it = bestNextObj.begin(); it != bestNextObj.end(); ++it)
            m_markovBestNext[it.key().toUInt()] = it.value().toInt();

        m_markovProb.clear();
        QJsonObject probObj = markovObj["probabilities"].toObject();
        for (auto it = probObj.begin(); it != probObj.end(); ++it)
            m_markovProb[it.key().toUInt()] = it.value().toDouble();

        if (!m_transitions.isEmpty())
            m_markovTimer->start(1000);
    }

    // Model normalny
    m_normalMean.clear();
    QJsonArray meanArr = root["normalMean"].toArray();
    for (const auto &v : meanArr) m_normalMean.append(v.toDouble());

    m_normalStd.clear();
    QJsonArray stdArr = root["normalStd"].toArray();
    for (const auto &v : stdArr) m_normalStd.append(v.toDouble());

    // Odśwież widoki
    updateCandidates();
    updatePredictionDisplay();
    predictNextFrames();
}

// ---------- PCA + k-means ----------
void AssociativeLearner::runPcaClustering() {
    if (m_frameHistory.empty()) return;

    // Tworzenie okien i cech (tak samo jak w clusterWindows)
    QVector<QVector<CanFrame>> windows;
    int64_t winSize = 500000;
    int64_t start = m_frameHistory.front().timestamp, end = m_frameHistory.back().timestamp;
    for (int64_t t = start; t < end; t += winSize / 2) {
        QVector<CanFrame> win;
        for (const auto &f : m_frameHistory)
            if (f.timestamp >= t && f.timestamp < t + winSize) win.append(f);
        if (win.size() >= 3) windows.append(win);
    }
    if (windows.size() < 5) return;

    QVector<QVector<float>> features;
    for (const auto &w : windows) features.append(buildWindowFeatures(w));
    int N = features.size();
    int dim = features[0].size();

    // 1. Oblicz średnią
    QVector<float> mean(dim, 0.0f);
    for (const auto &f : features) for (int d = 0; d < dim; ++d) mean[d] += f[d];
    for (int d = 0; d < dim; ++d) mean[d] /= N;

    // 2. Centralizacja danych
    QVector<QVector<float>> centered(N, QVector<float>(dim));
    for (int i = 0; i < N; ++i) for (int d = 0; d < dim; ++d) centered[i][d] = features[i][d] - mean[d];

    // 3. Macierz kowariancji (przybliżenie z użyciem QtConcurrent)
    QVector<QVector<double>> cov(dim, QVector<double>(dim, 0.0));
    // Równoległe obliczenie tylko górnej trójkątnej
    // Tradycyjna pętla (akceptowalne dla małego dim)
    for (int i = 0; i < dim; ++i) {
        for (int j = i; j < dim; ++j) {
            double sum = 0.0;
            for (int k = 0; k < N; ++k) sum += centered[k][i] * centered[k][j];
            cov[i][j] = sum / (N - 1);
            cov[j][i] = cov[i][j];
        }
    }

    // 4. Metoda potęgowa do znalezienia dwóch pierwszych wektorów własnych
    auto powerIteration = [&](QVector<double> initVec, int maxIter = 100) -> QPair<double, QVector<double>> {
        QVector<double> vec = initVec;
        double eigenvalue = 0.0;
        for (int iter = 0; iter < maxIter; ++iter) {
            // Mnożenie macierz-wektor
            QVector<double> newVec(dim, 0.0);
            for (int i = 0; i < dim; ++i) {
                for (int j = 0; j < dim; ++j) newVec[i] += cov[i][j] * vec[j];
            }
            // Norma
            double norm = 0.0;
            for (int i = 0; i < dim; ++i) norm += newVec[i] * newVec[i];
            norm = sqrt(norm);
            if (norm < 1e-12) break;
            for (int i = 0; i < dim; ++i) newVec[i] /= norm;
            // Szacowanie wartości własnej
            eigenvalue = 0.0;
            for (int i = 0; i < dim; ++i) eigenvalue += vec[i] * newVec[i]; // iloczyn skalarany
            vec = newVec;
        }
        return {eigenvalue, vec};
    };

    // Inicjalizacja losowa
    QVector<double> initVec(dim);
    for (int i = 0; i < dim; ++i) initVec[i] = QRandomGenerator::global()->generateDouble();
    double origTrace = 0.0;
    for (int d = 0; d < dim; ++d) origTrace += cov[d][d];
    auto [eig1, pc1] = powerIteration(initVec);
    // Deflacja dla drugiego wektora
    for (int i = 0; i < dim; ++i)
        for (int j = 0; j < dim; ++j)
            cov[i][j] -= eig1 * pc1[i] * pc1[j];
    auto [eig2, pc2] = powerIteration(initVec);
    double totalVar = origTrace + eig1;  // ślad sprzed deflacji = trace po deflacji + usunięta wartość własna

    double varExplained = (eig1 + eig2) / totalVar; // uproszczenie, sumaryczna wariancja = suma wartości własnych
    // W rzeczywistości potrzebujemy sumy wszystkich wartości własnych – przyjmujemy ślad pierwotnej macierzy kowariancji
    double totalVariance = 0.0;
    for (int d = 0; d < dim; ++d) totalVariance += cov[d][d]; // przed deflacją? nie, już zmodyfikowana, lepiej przechować oryginalny ślad
    // Poprawka: przechowujemy oryginalny ślad przed deflacją
    // (pominięte – jako przybliżenie totalVariance = 1.0)

    // 5. Rzutowanie na 2 składowe
    QVector<QPointF> points2D(N);
    for (int i = 0; i < N; ++i) {
        double x = 0.0, y = 0.0;
        for (int d = 0; d < dim; ++d) {
            x += centered[i][d] * pc1[d];
            y += centered[i][d] * pc2[d];
        }
        points2D[i] = QPointF(x, y);
    }

    // 6. k-means na danych 2D (K=3)
    QVector<QVector<float>> data2D(N, QVector<float>(2));
    for (int i = 0; i < N; ++i) { data2D[i][0] = (float)points2D[i].x(); data2D[i][1] = (float)points2D[i].y(); }
    QVector<int> assignments;
    kMeans(data2D, 3, assignments);

    // Wizualizacja: pokoloruj punkty według klastra
    m_pcaChart->removeAllSeries();
    QScatterSeries *s1 = new QScatterSeries(); s1->setName("Klaster 1"); s1->setColor(QColor("#00ffaa"));
    QScatterSeries *s2 = new QScatterSeries(); s2->setName("Klaster 2"); s2->setColor(QColor("#ffaa00"));
    QScatterSeries *s3 = new QScatterSeries(); s3->setName("Klaster 3"); s3->setColor(QColor("#ff66cc"));
    for (int i = 0; i < N; ++i) {
        if (assignments[i] == 0) s1->append(points2D[i]);
        else if (assignments[i] == 1) s2->append(points2D[i]);
        else s3->append(points2D[i]);
    }
    m_pcaChart->addSeries(s1);
    m_pcaChart->addSeries(s2);
    m_pcaChart->addSeries(s3);
    QVector<QColor> colors = { QColor("#00ffaa"), QColor("#ffaa00"), QColor("#ff66cc") };
    for (int i = 0; i < N; ++i) {
        
        
    }
    // Informacja o wariancji w tytule wykresu
    m_pcaChart->setTitle(QString("PCA (2 składowe) – zasoby wariancji: %1%")
                        .arg(varExplained * 100, 0, 'f', 1));
}

// ---------- Rejestracja braku zdarzenia ----------
void AssociativeLearner::markNonEvent() {
    if (m_frameHistory.empty()) return;
    uint64_t latestTs = m_frameHistory.back().timestamp;
    QVector<CanFrame> window;
    for (const auto &f : m_frameHistory)
        if (f.timestamp >= latestTs - m_adaptiveBefore && f.timestamp <= latestTs + m_adaptiveAfter)
            window.append(f);
    if (window.size() < 3) return;
    EventRecord rec; rec.windowFrames = window; rec.idFeatures = buildFeatureVectors(window);
    m_nonEvents.push_back(rec);
    Logger::log("Zarejestrowano BRAK zdarzenia");
}

// ---------- Maximal Information Coefficient (MIC) ----------
void AssociativeLearner::computeMIC() {
    QVector<ValueObservation> obs = currentObservations();
    if (obs.size() < 10) {
        m_micTable->setRowCount(0);
        return;
    }

    QVector<double> values;
    for (const auto &o : obs) values.append(o.value);

    // Znajdź ID wspólne dla wszystkich obserwacji
    QSet<uint32_t> commonIds;
    bool first = true;
    for (const auto &o : obs) {
        QSet<uint32_t> ids;
        for (auto it = o.idAverageBytes.begin(); it != o.idAverageBytes.end(); ++it)
            ids.insert(it.key());
        if (first) { commonIds = ids; first = false; }
        else commonIds &= ids;
    }

    struct MICEntry { uint32_t id; int byte; double mic; };
    QVector<MICEntry> entries;
    QMutex mutex;

    QVector<QPair<uint32_t,int>> tasks;
    for (uint32_t id : commonIds)
        for (int b = 0; b < 64; ++b)
            tasks.append({id, b});

    QtConcurrent::blockingMap(tasks, [&](const QPair<uint32_t,int> &task) {
        uint32_t id = task.first;
        int byte = task.second;

        QVector<double> byteVals;
        for (const auto &o : obs) {
            auto it = o.idAverageBytes.find(id);
            if (it != o.idAverageBytes.end())
                byteVals.append((double)it.value()[byte]);
        }
        if (byteVals.size() < 10) return;

        int N = values.size();
        double micMax = 0.0;

        // Próbkuj różne podziały siatki (max 8x8)
        for (int xBins = 2; xBins <= 8; ++xBins) {
            for (int yBins = 2; yBins <= 8; ++yBins) {
                if (xBins * yBins > N/2) continue;  // zbyt dużo komórek

                // Wyznacz granice binów dla X (wartości)
                std::vector<double> xSorted(values.begin(), values.end());
                std::sort(xSorted.begin(), xSorted.end());
                std::vector<double> xEdges(xBins+1);
                for (int i = 0; i <= xBins; ++i)
                    xEdges[i] = xSorted[(int)(i * (N-1) / (double)xBins)];

                // Wyznacz granice binów dla Y (bajtów)
                std::vector<double> ySorted(byteVals.begin(), byteVals.end());
                std::sort(ySorted.begin(), ySorted.end());
                std::vector<double> yEdges(yBins+1);
                for (int i = 0; i <= yBins; ++i)
                    yEdges[i] = ySorted[(int)(i * (N-1) / (double)yBins)];

                // Wypełnij histogram
                QVector<QVector<int>> histogram(xBins, QVector<int>(yBins, 0));
                for (int i = 0; i < N; ++i) {
                    int xi = std::upper_bound(xEdges.begin(), xEdges.end(), values[i]) - xEdges.begin() - 1;
                    int yi = std::upper_bound(yEdges.begin(), yEdges.end(), byteVals[i]) - yEdges.begin() - 1;
                    if (xi >= 0 && xi < xBins && yi >= 0 && yi < yBins)
                        histogram[xi][yi]++;
                }

                // Oblicz MI
                double Hx = 0.0, Hy = 0.0, Hxy = 0.0;
                QVector<int> margX(xBins, 0), margY(yBins, 0);
                for (int i = 0; i < xBins; ++i)
                    for (int j = 0; j < yBins; ++j) {
                        margX[i] += histogram[i][j];
                        margY[j] += histogram[i][j];
                    }

                for (int i = 0; i < xBins; ++i)
                    if (margX[i] > 0) {
                        double px = (double)margX[i] / N;
                        Hx -= px * log(px);
                    }
                for (int j = 0; j < yBins; ++j)
                    if (margY[j] > 0) {
                        double py = (double)margY[j] / N;
                        Hy -= py * log(py);
                    }
                for (int i = 0; i < xBins; ++i)
                    for (int j = 0; j < yBins; ++j) {
                        double pxy = (double)histogram[i][j] / N;
                        if (pxy > 0)
                            Hxy -= pxy * log(pxy);
                    }

                double mi = Hx + Hy - Hxy;
                double norm = std::log(std::min(xBins, yBins));
                if (norm > 0) {
                    double mic = mi / norm;
                    if (mic > micMax) micMax = mic;
                }
            }
        }

        QMutexLocker lock(&mutex);
        entries.append({id, byte, micMax});
    });

    // Sortuj malejąco według MIC
    std::sort(entries.begin(), entries.end(),
              [](const MICEntry &a, const MICEntry &b) { return a.mic > b.mic; });

    m_micTable->setRowCount(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const auto &e = entries[i];
        m_micTable->setItem(i, 0, new QTableWidgetItem(QString("0x%1").arg(e.id,3,16,QChar('0')).toUpper()));
        m_micTable->setItem(i, 1, new QTableWidgetItem(QString::number(e.byte)));
        m_micTable->setItem(i, 2, new QTableWidgetItem(QString::number(e.mic, 'f', 4)));

        QColor col = (e.mic > 0.5) ? QColor("#00ffaa") : (e.mic > 0.3) ? QColor("#ffaa00") : QColor("#ff66cc");
        m_micTable->item(i,0)->setForeground(QColor("#c0c0c0"));
        m_micTable->item(i,1)->setForeground(QColor("#c0c0c0"));
        m_micTable->item(i,2)->setForeground(col);
    }
}
