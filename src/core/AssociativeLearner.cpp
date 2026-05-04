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

    m_chart = new QChart(); m_chart->setTitle("Wartość od bajtu");
    m_scatterSeries = new QScatterSeries(); m_scatterSeries->setMarkerSize(8.0); m_scatterSeries->setColor(QColor("#00ffaa"));
    m_chart->addSeries(m_scatterSeries); m_chart->createDefaultAxes();
    m_chartView = new QChartView(m_chart); m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setMinimumHeight(300);
    mainLayout->addWidget(m_chartView);

    auto *serLayout = new QHBoxLayout;
    m_saveBtn = new QPushButton("💾 Zapisz sesję"); m_loadBtn = new QPushButton("📂 Wczytaj sesję");
    serLayout->addWidget(m_saveBtn); serLayout->addWidget(m_loadBtn);
    mainLayout->addLayout(serLayout);

    connect(m_markEventBtn, &QPushButton::clicked, this, &AssociativeLearner::markEvent);
    connect(m_resetBtn, &QPushButton::clicked, this, &AssociativeLearner::resetLearning);
    connect(m_addObsBtn, &QPushButton::clicked, this, &AssociativeLearner::addObservation);
    connect(m_saveBtn, &QPushButton::clicked, this, &AssociativeLearner::saveSession);
    connect(m_loadBtn, &QPushButton::clicked, this, &AssociativeLearner::loadSession);
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
    }
}
