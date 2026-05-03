#!/usr/bin/env bash
# add_anomaly_detection_v2.sh – Detekcja anomalii (kompletna aktualizacja .h i .cpp)
set -e

echo "=== Krok 8: Detekcja anomalii (pełna integracja) ==="

# --- 1. Nagłówek AssociativeLearner.h z nowymi elementami ---
cat > src/core/AssociativeLearner.h << 'EOF'
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
EOF

# --- 2. Pełny plik .cpp z dodanymi metodami anomalii ---
cat > src/core/AssociativeLearner.cpp << 'EOF'
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
    scrollArea->setStyleSheet("QScrollArea { border: none; }");
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

    auto *sep1 = new QFrame; sep1->setFrameShape(QFrame::HLine); sep1->setStyleSheet("background-color: #e94560;");
    mainLayout->addWidget(sep1);

    auto *valLayout = new QHBoxLayout;
    valLayout->addWidget(new QLabel("Wartość (np. temperatura):"));
    m_valueInput = new QLineEdit; m_valueInput->setPlaceholderText("0.0");
    m_addObsBtn = new QPushButton("Dodaj obserwację");
    valLayout->addWidget(m_valueInput); valLayout->addWidget(m_addObsBtn);
    mainLayout->addLayout(valLayout);

    m_correlationTable = new QTableWidget(0,4);
    m_correlationTable->setHorizontalHeaderLabels({"CAN ID","Bajt","Korelacja",""});
    m_correlationTable->verticalHeader()->hide(); m_correlationTable->horizontalHeader()->setStretchLastSection(true);
    m_correlationTable->setShowGrid(false); m_correlationTable->setAlternatingRowColors(false);
    m_correlationTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mainLayout->addWidget(m_correlationTable);

    auto *sep2 = new QFrame; sep2->setFrameShape(QFrame::HLine); sep2->setStyleSheet("background-color: #e94560;");
    mainLayout->addWidget(sep2);

    auto *seqLayout = new QHBoxLayout;
    seqLayout->addWidget(new QLabel("Długość sekwencji:"));
    m_ngramCombo = new QComboBox; m_ngramCombo->addItems({"Bigram (2)","Trigram (3)"});
    seqLayout->addWidget(m_ngramCombo); seqLayout->addStretch();
    mainLayout->addLayout(seqLayout);

    m_sequenceTable = new QTableWidget(0,3);
    m_sequenceTable->setHorizontalHeaderLabels({"Sekwencja ID","Wystąpienia","Pewność"});
    m_sequenceTable->verticalHeader()->hide(); m_sequenceTable->horizontalHeader()->setStretchLastSection(true);
    m_sequenceTable->setShowGrid(false); m_sequenceTable->setAlternatingRowColors(false);
    m_sequenceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mainLayout->addWidget(m_sequenceTable);

    auto *sep3 = new QFrame; sep3->setFrameShape(QFrame::HLine); sep3->setStyleSheet("background-color: #e94560;");
    mainLayout->addWidget(sep3);

    mainLayout->addWidget(new QLabel("Korelacja międzybajtowa"));
    m_crossByteTable = new QTableWidget(0,5);
    m_crossByteTable->setHorizontalHeaderLabels({"ID1","Bajt1","ID2","Bajt2","Korelacja"});
    m_crossByteTable->verticalHeader()->hide(); m_crossByteTable->horizontalHeader()->setStretchLastSection(true);
    m_crossByteTable->setShowGrid(false); m_crossByteTable->setAlternatingRowColors(false);
    m_crossByteTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mainLayout->addWidget(m_crossByteTable);

    auto *sep4 = new QFrame; sep4->setFrameShape(QFrame::HLine); sep4->setStyleSheet("background-color: #e94560;");
    mainLayout->addWidget(sep4);

    auto *clusterLayout = new QHBoxLayout;
    clusterLayout->addWidget(new QLabel("Klastrowanie okien"));
    m_clusterBtn = new QPushButton("Uruchom k-means");
    clusterLayout->addWidget(m_clusterBtn); clusterLayout->addStretch();
    mainLayout->addLayout(clusterLayout);

    m_clusterTable = new QTableWidget(0,4);
    m_clusterTable->setHorizontalHeaderLabels({"Klaster","Śr. liczba ramek","Dominujące ID","Liczba okien"});
    m_clusterTable->verticalHeader()->hide(); m_clusterTable->horizontalHeader()->setStretchLastSection(true);
    m_clusterTable->setShowGrid(false); m_clusterTable->setAlternatingRowColors(false);
    m_clusterTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mainLayout->addWidget(m_clusterTable);

    auto *sep5 = new QFrame; sep5->setFrameShape(QFrame::HLine); sep5->setStyleSheet("background-color: #e94560;");
    mainLayout->addWidget(sep5);

    auto *predLayout = new QHBoxLayout;
    predLayout->addWidget(new QLabel("Predykcja wartości"));
    m_trainPredictionBtn = new QPushButton("Trenuj predykcję");
    predLayout->addWidget(m_trainPredictionBtn); predLayout->addStretch();
    mainLayout->addLayout(predLayout);

    m_predictionTable = new QTableWidget(0,5);
    m_predictionTable->setHorizontalHeaderLabels({"CAN ID","Bajt","Wsp. kier. (a)","Wyraz wolny (b)","Bieżąca prognoza"});
    m_predictionTable->verticalHeader()->hide(); m_predictionTable->horizontalHeader()->setStretchLastSection(true);
    m_predictionTable->setShowGrid(false); m_predictionTable->setAlternatingRowColors(false);
    m_predictionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mainLayout->addWidget(m_predictionTable);

    m_predictionTimer = new QTimer(this);
    connect(m_predictionTimer, &QTimer::timeout, this, &AssociativeLearner::updatePredictionDisplay);

    auto *sep6 = new QFrame; sep6->setFrameShape(QFrame::HLine); sep6->setStyleSheet("background-color: #e94560;");
    mainLayout->addWidget(sep6);

    // --- Anomalie ---
    auto *anomalyLayout = new QHBoxLayout;
    m_anomalyToggleBtn = new QPushButton("▶ Rozpocznij monitorowanie anomalii");
    m_anomalyThreshold = new QLineEdit; m_anomalyThreshold->setText("10.0");
    m_anomalyThreshold->setMaximumWidth(60);
    anomalyLayout->addWidget(m_anomalyToggleBtn);
    anomalyLayout->addWidget(new QLabel("Próg:"));
    anomalyLayout->addWidget(m_anomalyThreshold);
    mainLayout->addLayout(anomalyLayout);

    m_anomalyTable = new QTableWidget(0,3);
    m_anomalyTable->setHorizontalHeaderLabels({"Czas (s)", "Wynik", "Opis"});
    m_anomalyTable->verticalHeader()->hide(); m_anomalyTable->horizontalHeader()->setStretchLastSection(true);
    m_anomalyTable->setShowGrid(false); m_anomalyTable->setAlternatingRowColors(false);
    m_anomalyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mainLayout->addWidget(m_anomalyTable);

    m_anomalyTimer = new QTimer(this);
    connect(m_anomalyTimer, &QTimer::timeout, this, &AssociativeLearner::checkAnomaly);
    connect(m_anomalyToggleBtn, &QPushButton::clicked, this, [this]() {
        if (m_monitoring) stopAnomalyMonitoring(); else startAnomalyMonitoring();
    });

    auto *serLayout = new QHBoxLayout;
    m_saveBtn = new QPushButton("💾 Zapisz sesję"); m_loadBtn = new QPushButton("📂 Wczytaj sesję");
    serLayout->addWidget(m_saveBtn); serLayout->addWidget(m_loadBtn);
    mainLayout->addLayout(serLayout);

    connect(m_markEventBtn, &QPushButton::clicked, this, &AssociativeLearner::markEvent);
    connect(m_resetBtn, &QPushButton::clicked, this, &AssociativeLearner::resetLearning);
    connect(m_addObsBtn, &QPushButton::clicked, this, &AssociativeLearner::addObservation);
    connect(m_saveBtn, &QPushButton::clicked, this, &AssociativeLearner::saveSession);
    connect(m_loadBtn, &QPushButton::clicked, this, &AssociativeLearner::loadSession);
    connect(m_ngramCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AssociativeLearner::updateSequenceTable);
    connect(m_clusterBtn, &QPushButton::clicked, this, &AssociativeLearner::clusterWindows);
    connect(m_trainPredictionBtn, &QPushButton::clicked, this, &AssociativeLearner::trainPrediction);

    // Minimalne wysokości tabel
    m_candidatesView->setMinimumHeight(400);
    m_correlationTable->setMinimumHeight(400);
    m_sequenceTable->setMinimumHeight(400);
    m_crossByteTable->setMinimumHeight(400);
    m_clusterTable->setMinimumHeight(400);
    m_predictionTable->setMinimumHeight(400);
    m_anomalyTable->setMinimumHeight(400);

    setStyleSheet(R"(
        QPushButton { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560; border-radius: 4px; padding: 6px 15px; font-weight: bold; }
        QPushButton:hover { background: #e94560; color: #0a0e17; }
        QLineEdit, QComboBox { background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560; border-radius: 4px; padding: 4px 8px; }
    )");
}

AssociativeLearner::~AssociativeLearner() {
    m_predictionTimer->stop();
    m_anomalyTimer->stop();
}

void AssociativeLearner::processFrame(const CanFrame &frame) {
    m_frameHistory.push_back(frame);
    if (m_frameHistory.size() > HISTORY_MAX) m_frameHistory.pop_front();
}

void AssociativeLearner::markEvent() {
    if (m_frameHistory.empty()) return;
    uint64_t latestTs = m_frameHistory.back().timestamp;
    QVector<CanFrame> window;
    for (const auto &f : m_frameHistory)
        if (f.timestamp >= latestTs - m_adaptiveBefore && f.timestamp <= latestTs + m_adaptiveAfter)
            window.append(f);
    if (window.size() < 3) return;
    EventRecord rec; rec.windowFrames = window; rec.idFeatures = buildFeatureVectors(window);
    m_events.push_back(rec); m_iteration++;
    m_iterationLabel->setText(QString("Liczba iteracji: %1").arg(m_iteration));
    if (m_iteration == 1) recalcAdaptiveWindow();
    emit eventMarked(m_iteration);
    updateCandidates(); updateSequenceTable();
}

void AssociativeLearner::resetLearning() {
    m_events.clear(); m_observations.clear(); m_iteration = 0;
    m_iterationLabel->setText("Liczba iteracji: 0");
    m_candidateModel->clear(); m_correlationTable->setRowCount(0);
    m_sequenceTable->setRowCount(0); m_crossByteTable->setRowCount(0);
    m_clusterTable->setRowCount(0);
    m_linearModels.clear();
    m_predictionTable->setRowCount(0);
    m_predictionTimer->stop();
    stopAnomalyMonitoring();
    m_anomalyTable->setRowCount(0);
    m_normalMean.clear();
    m_normalStd.clear();
}

void AssociativeLearner::addObservation() {
    bool ok; double v = m_valueInput->text().toDouble(&ok); if(!ok) return;
    if(m_frameHistory.empty()) return;
    uint64_t latestTs = m_frameHistory.back().timestamp;
    QVector<CanFrame> window;
    for(const auto &f : m_frameHistory)
        if(f.timestamp >= latestTs - m_adaptiveBefore && f.timestamp <= latestTs + m_adaptiveAfter)
            window.append(f);
    if(window.empty()) return;
    QHash<uint32_t, QVector<CanFrame>> grouped;
    for(const auto &f : window) grouped[f.id].append(f);
    ValueObservation obs; obs.value = v;
    for(auto it=grouped.begin(); it!=grouped.end(); ++it) {
        std::vector<uint8_t> avg(64,0); const auto &frames = it.value();
        for(const auto &f : frames) for(int i=0;i<f.dlc;++i) avg[i] += f.data[i]/frames.size();
        obs.idAverageBytes[it.key()] = avg;
    }
    m_observations.append(obs); m_valueInput->clear();
    updateCorrelationTable(); updateCrossByteTable();
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
    if(m_observations.size()<3){ m_correlationTable->setRowCount(0); return; }
    QSet<uint32_t> common; bool first=true;
    for(const auto &obs : m_observations){
        QSet<uint32_t> ids; for(auto it=obs.idAverageBytes.begin(); it!=obs.idAverageBytes.end(); ++it) ids.insert(it.key());
        if(first){common=ids; first=false;} else common&=ids;
    }
    struct E{uint32_t id; int b; double corr;}; QVector<E> entries;
    for(uint32_t id : common) for(int b=0;b<64;++b){
        QVector<double> vx,vy;
        for(const auto &obs : m_observations){ auto it=obs.idAverageBytes.find(id); if(it!=obs.idAverageBytes.end()){ vx.append(obs.value); vy.append((double)it.value()[b]); } }
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
        m_sequenceTable->setItem(i,0,new QTableWidgetItem(p.first));
        m_sequenceTable->setItem(i,1,new QTableWidgetItem(QString::number(p.second)));
        m_sequenceTable->setItem(i,2,new QTableWidgetItem(QString("%1%").arg(prob*100.0,0,'f',1)));
        QColor col = (prob>0.7)?QColor("#00ffaa"):(prob>0.4)?QColor("#ffaa00"):QColor("#ff66cc");
        m_sequenceTable->item(i,0)->setForeground(QColor("#c0c0c0")); m_sequenceTable->item(i,1)->setForeground(QColor("#c0c0c0")); m_sequenceTable->item(i,2)->setForeground(col);
    }
}

void AssociativeLearner::updateCrossByteTable() {
    if(m_observations.size()<3){ m_crossByteTable->setRowCount(0); return; }
    QSet<QPair<uint32_t,int>> allPairs; bool first=true;
    for(const auto &obs : m_observations){
        QSet<QPair<uint32_t,int>> s; for(auto it=obs.idAverageBytes.begin(); it!=obs.idAverageBytes.end(); ++it) for(int b=0;b<64;++b) s.insert({it.key(),b});
        if(first){allPairs=s; first=false;} else allPairs&=s;
    }
    QVector<QPair<uint32_t,int>> ap = allPairs.values();
    struct CE{uint32_t id1; int b1; uint32_t id2; int b2; double corr;}; QVector<CE> entries;
    for(int i=0;i<ap.size();++i) for(int j=i+1;j<ap.size();++j){
        auto p1=ap[i], p2=ap[j]; QVector<double> x,y;
        for(const auto &obs : m_observations){
            auto it1=obs.idAverageBytes.find(p1.first), it2=obs.idAverageBytes.find(p2.first);
            if(it1!=obs.idAverageBytes.end() && it2!=obs.idAverageBytes.end()){ x.append((double)it1.value()[p1.second]); y.append((double)it2.value()[p2.second]); }
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
    QVector<float> feat;
    if(window.isEmpty()){ feat.fill(0,5); return feat; }
    feat.append((float)window.size());
    QSet<uint32_t> ids; for(const auto &f : window) ids.insert(f.id);
    feat.append((float)ids.size());
    double entropy = 0.0;
    QHash<uint32_t,int> freq; for(const auto &f : window) freq[f.id]++;
    for(auto it=freq.begin(); it!=freq.end(); ++it){ double p = (double)it.value()/window.size(); entropy -= p*log2(p+1e-9); }
    feat.append((float)entropy);
    int64_t dur = window.back().timestamp - window.front().timestamp;
    feat.append((float)dur/1000.0f);
    feat.append(0.0f);
    return feat;
}

int AssociativeLearner::kMeans(const QVector<QVector<float>> &data, int K, QVector<int> &assignments) {
    int N = data.size(); if(N==0) return 0; int dim = data[0].size();
    assignments.resize(N);
    QVector<QVector<float>> centroids(K, QVector<float>(dim));
    std::mt19937 rng(42); std::uniform_int_distribution<int> dist(0,N-1);
    for(int k=0;k<K;++k) centroids[k] = data[dist(rng)];
    int iter=0, maxIter=50;
    while(iter++ < maxIter){
        QVector<int> counts(K,0); QVector<QVector<float>> newCentroids(K, QVector<float>(dim,0.0f));
        QMutex mutex;
        QtConcurrent::blockingMap(data, [&](const QVector<float> &point){
            int idx = 0; double best = std::numeric_limits<double>::max();
            for(int k=0;k<K;++k){
                double d = 0.0; for(int i=0;i<dim;++i) d += (point[i]-centroids[k][i])*(point[i]-centroids[k][i]);
                if(d < best){ best = d; idx = k; }
            }
            QMutexLocker lock(&mutex);
            assignments[&point - &data[0]] = idx; counts[idx]++;
            for(int i=0;i<dim;++i) newCentroids[idx][i] += point[i];
        });
        bool changed = false;
        for(int k=0;k<K;++k){
            if(counts[k]>0) for(int i=0;i<dim;++i) newCentroids[k][i]/=counts[k];
            double diff = 0.0; for(int i=0;i<dim;++i) diff += (centroids[k][i]-newCentroids[k][i])*(centroids[k][i]-newCentroids[k][i]);
            if(diff > 0.001) changed = true;
            centroids[k] = newCentroids[k];
        }
        if(!changed) break;
    }
    double wcss = 0.0; for(int i=0;i<N;++i) for(int d=0;d<dim;++d) wcss += (data[i][d]-centroids[assignments[i]][d])*(data[i][d]-centroids[assignments[i]][d]);
    return wcss;
}

void AssociativeLearner::clusterWindows() {
    if(m_frameHistory.empty()) return;
    QVector<QVector<CanFrame>> windows;
    int64_t windowSize = 500000;
    if(m_frameHistory.size() < 10) return;
    int64_t start = m_frameHistory.front().timestamp, end = m_frameHistory.back().timestamp;
    for(int64_t t = start; t < end; t += windowSize/2){
        QVector<CanFrame> win; for(const auto &f : m_frameHistory) if(f.timestamp >= t && f.timestamp < t+windowSize) win.append(f);
        if(win.size()>=3) windows.append(win);
    }
    if(windows.size()<5) return;
    QVector<QVector<float>> features; for(const auto &w : windows) features.append(buildWindowFeatures(w));
    int K = 3; QVector<int> assignments; kMeans(features, K, assignments);
    struct ClusterStats { int count=0; double avgFrames=0; QHash<uint32_t,int> idFreq; };
    QVector<ClusterStats> stats(K);
    for(int i=0;i<assignments.size();++i){
        int c = assignments[i]; stats[c].count++; stats[c].avgFrames += windows[i].size();
        for(const auto &f : windows[i]) stats[c].idFreq[f.id]++;
    }
    for(int c=0;c<K;++c) if(stats[c].count>0) stats[c].avgFrames /= stats[c].count;
    m_clusterTable->setRowCount(K);
    for(int c=0;c<K;++c){
        m_clusterTable->setItem(c,0,new QTableWidgetItem(QString("Klaster %1").arg(c+1)));
        m_clusterTable->setItem(c,1,new QTableWidgetItem(QString::number(stats[c].avgFrames,'f',1)));
        QList<QPair<uint32_t,int>> sorted; for(auto it=stats[c].idFreq.begin(); it!=stats[c].idFreq.end(); ++it) sorted.append({it.key(),it.value()});
        std::sort(sorted.begin(), sorted.end(), [](const QPair<uint32_t,int> &a, const QPair<uint32_t,int> &b){ return a.second>b.second; });
        QStringList topIds; for(int i=0; i<3 && i<sorted.size(); ++i) topIds.append(QString("0x%1").arg(sorted[i].first,3,16,QChar('0')).toUpper());
        m_clusterTable->setItem(c,2,new QTableWidgetItem(topIds.join(", ")));
        m_clusterTable->setItem(c,3,new QTableWidgetItem(QString::number(stats[c].count)));
    }
}

// ---------- Predykcja ----------
void AssociativeLearner::trainPrediction() {
    if (m_observations.size() < 3) {
        QMessageBox::information(this, "Predykcja", "Zbyt mało obserwacji.");
        return;
    }

    m_linearModels.clear();
    QSet<QPair<uint32_t,int>> allPairs;
    for (const auto &obs : m_observations)
        for (auto it = obs.idAverageBytes.begin(); it != obs.idAverageBytes.end(); ++it)
            for (int b = 0; b < 64; ++b)
                allPairs.insert({it.key(), b});

    for (const auto &pair : allPairs) {
        uint32_t id = pair.first;
        int byte = pair.second;
        QVector<double> X, Y;
        for (const auto &obs : m_observations) {
            auto it = obs.idAverageBytes.find(id);
            if (it != obs.idAverageBytes.end()) {
                X.append((double)it.value()[byte]);
                Y.append(obs.value);
            }
        }
        if (X.size() < 3) continue;

        double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
        int N = X.size();
        for (int i = 0; i < N; ++i) {
            double x = X[i], y = Y[i];
            sumX += x; sumY += y;
            sumXY += x * y;
            sumX2 += x * x;
        }
        double denom = N * sumX2 - sumX * sumX;
        if (fabs(denom) < 1e-9) continue;
        double a = (N * sumXY - sumX * sumY) / denom;
        double b = (sumY - a * sumX) / N;

        double sumY2 = 0;
        for (double y : Y) sumY2 += y * y;
        double corr = (N * sumXY - sumX * sumY) / sqrt((N*sumX2 - sumX*sumX)*(N*sumY2 - sumY*sumY) + 1e-9);
        if (fabs(corr) < 0.8) continue;

        m_linearModels[pair] = {a, b};
    }

    m_predictionTable->setRowCount(m_linearModels.size());
    int row = 0;
    for (auto it = m_linearModels.begin(); it != m_linearModels.end(); ++it, ++row) {
        uint32_t id = it.key().first;
        int byte = it.key().second;
        double a = it.value().first;
        double b_ = it.value().second;

        m_predictionTable->setItem(row, 0, new QTableWidgetItem(QString("0x%1").arg(id,3,16,QChar('0')).toUpper()));
        m_predictionTable->setItem(row, 1, new QTableWidgetItem(QString::number(byte)));
        m_predictionTable->setItem(row, 2, new QTableWidgetItem(QString::number(a, 'f', 4)));
        m_predictionTable->setItem(row, 3, new QTableWidgetItem(QString::number(b_, 'f', 4)));
        m_predictionTable->setItem(row, 4, new QTableWidgetItem("—"));
    }

    if (!m_linearModels.isEmpty() && !m_predictionTimer->isActive()) {
        m_predictionTimer->start(500);
    }
}

void AssociativeLearner::updatePredictionDisplay() {
    if (m_linearModels.isEmpty() || m_frameHistory.empty()) return;

    int row = 0;
    for (auto it = m_linearModels.begin(); it != m_linearModels.end(); ++it, ++row) {
        uint32_t id = it.key().first;
        int byte = it.key().second;
        double a = it.value().first;
        double b_ = it.value().second;

        CanFrame lastFrame;
        bool found = false;
        for (auto frameIter = m_frameHistory.rbegin(); frameIter != m_frameHistory.rend(); ++frameIter) {
            if (frameIter->id == id) {
                lastFrame = *frameIter;
                found = true;
                break;
            }
        }
        if (found && byte < lastFrame.dlc) {
            double x = lastFrame.data[byte];
            double prediction = a * x + b_;
            if (row < m_predictionTable->rowCount()) {
                m_predictionTable->item(row, 4)->setText(QString::number(prediction, 'f', 2));
            }
        }
    }
}

// ---------- Anomalie ----------
void AssociativeLearner::startAnomalyMonitoring() {
    if (m_frameHistory.size() < 100) {
        QMessageBox::warning(this, "Monitorowanie", "Zbyt mało danych w historii (minimum 100 ramek).");
        return;
    }
    buildNormalModel();
    m_monitoring = true;
    m_anomalyToggleBtn->setText("■ Zatrzymaj monitorowanie anomalii");
    m_anomalyTimer->start(1000);
}

void AssociativeLearner::stopAnomalyMonitoring() {
    m_monitoring = false;
    m_anomalyToggleBtn->setText("▶ Rozpocznij monitorowanie anomalii");
    m_anomalyTimer->stop();
}

void AssociativeLearner::buildNormalModel() {
    QVector<QVector<float>> windowFeatures;
    int64_t windowSize = 1000000; // 1 s
    if (m_frameHistory.size() < 50) return;
    int64_t start = m_frameHistory.front().timestamp;
    int64_t end = m_frameHistory.back().timestamp;
    for (int64_t t = start; t < end - windowSize; t += 500000) {
        QVector<CanFrame> win;
        for (const auto &f : m_frameHistory) {
            if (f.timestamp >= t && f.timestamp < t + windowSize)
                win.append(f);
        }
        if (win.size() >= 3)
            windowFeatures.append(buildWindowFeatures(win));
    }
    if (windowFeatures.size() < 10) return;

    int dim = windowFeatures[0].size();
    m_normalMean.resize(dim, 0.0f);
    m_normalStd.resize(dim, 0.0f);
    for (int d = 0; d < dim; ++d) {
        double sum = 0, sq_sum = 0;
        for (const auto &feat : windowFeatures) {
            sum += feat[d];
            sq_sum += feat[d] * feat[d];
        }
        m_normalMean[d] = sum / windowFeatures.size();
        m_normalStd[d] = std::sqrt(sq_sum / windowFeatures.size() - m_normalMean[d] * m_normalMean[d]);
        if (m_normalStd[d] < 1e-6) m_normalStd[d] = 1.0f;
    }
}

void AssociativeLearner::checkAnomaly() {
    if (!m_monitoring || m_frameHistory.empty()) return;

    uint64_t now = m_frameHistory.back().timestamp;
    int64_t windowSize = 1000000;
    QVector<CanFrame> currentWindow;
    for (auto it = m_frameHistory.rbegin(); it != m_frameHistory.rend(); ++it) {
        if (it->timestamp >= now - windowSize) currentWindow.prepend(*it);
        else break;
    }
    if (currentWindow.size() < 3) return;

    QVector<float> feat = buildWindowFeatures(currentWindow);
    if (m_normalMean.empty()) {
        buildNormalModel();
        if (m_normalMean.empty()) return;
    }

    double score = 0.0;
    for (int d = 0; d < feat.size(); ++d) {
        double z = (feat[d] - m_normalMean[d]) / m_normalStd[d];
        score += z * z;
    }

    double threshold = m_anomalyThreshold->text().toDouble();
    if (score > threshold) {
        int row = m_anomalyTable->rowCount();
        m_anomalyTable->insertRow(row);
        m_anomalyTable->setItem(row, 0, new QTableWidgetItem(QString::number(now / 1000000.0, 'f', 2)));
        m_anomalyTable->setItem(row, 1, new QTableWidgetItem(QString::number(score, 'f', 2)));
        m_anomalyTable->setItem(row, 2, new QTableWidgetItem("Anomalia wykryta"));
        m_anomalyTable->scrollToBottom();
    }
}

// --- Serializacja ---
void AssociativeLearner::saveSession() {
    QString path = QFileDialog::getSaveFileName(this, "Zapisz sesję", "", "JSON (*.json)");
    if(path.isEmpty()) return;
    QJsonObject root; root["iteration"]=m_iteration; root["adaptiveBefore"]=(qint64)m_adaptiveBefore; root["adaptiveAfter"]=(qint64)m_adaptiveAfter;
    QJsonArray eventsArr; for(const auto &ev : m_events){ QJsonObject evObj; QJsonArray framesArr; for(const auto &f : ev.windowFrames){ QJsonObject fObj; fObj["id"]=(int)f.id; fObj["dlc"]=f.dlc; QJsonArray dataArr; for(int i=0;i<f.dlc;++i) dataArr.append(f.data[i]); fObj["data"]=dataArr; fObj["timestamp"]=(qint64)f.timestamp; framesArr.append(fObj); } evObj["windowFrames"]=framesArr; eventsArr.append(evObj); } root["events"]=eventsArr;
    QJsonArray obsArr; for(const auto &obs : m_observations){ QJsonObject obsObj; obsObj["value"]=obs.value; QJsonObject bytesObj; for(auto it=obs.idAverageBytes.begin(); it!=obs.idAverageBytes.end(); ++it){ QJsonArray byteArr; for(uint8_t b : it.value()) byteArr.append((int)b); bytesObj[QString::number(it.key())]=byteArr; } obsObj["idAverageBytes"]=bytesObj; obsArr.append(obsObj); } root["observations"]=obsArr;
    QFile file(path); if(file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(root).toJson());
}

void AssociativeLearner::loadSession() {
    QString path = QFileDialog::getOpenFileName(this, "Wczytaj sesję", "", "JSON (*.json)");
    if(path.isEmpty()) return;
    QFile file(path); if(!file.open(QIODevice::ReadOnly)){ QMessageBox::warning(this,"Błąd","Nie można otworzyć pliku."); return; }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll()); file.close(); QJsonObject root = doc.object();
    m_events.clear(); m_observations.clear(); m_iteration = root["iteration"].toInt(); m_adaptiveBefore = root["adaptiveBefore"].toVariant().toLongLong(); m_adaptiveAfter = root["adaptiveAfter"].toVariant().toLongLong();
    QJsonArray eventsArr = root["events"].toArray(); for(const auto &evVal : eventsArr){ EventRecord rec; QJsonObject evObj = evVal.toObject(); QJsonArray framesArr = evObj["windowFrames"].toArray(); for(const auto &fVal : framesArr){ QJsonObject fObj = fVal.toObject(); CanFrame f; f.id=fObj["id"].toInt(); f.dlc=fObj["dlc"].toInt(); QJsonArray dataArr = fObj["data"].toArray(); for(int i=0;i<dataArr.size();++i) f.data[i]=(uint8_t)dataArr[i].toInt(); f.timestamp=fObj["timestamp"].toVariant().toLongLong(); rec.windowFrames.append(f); } rec.idFeatures = buildFeatureVectors(rec.windowFrames); m_events.append(rec); }
    QJsonArray obsArr = root["observations"].toArray(); for(const auto &obsVal : obsArr){ ValueObservation obs; QJsonObject obsObj = obsVal.toObject(); obs.value = obsObj["value"].toDouble(); QJsonObject bytesObj = obsObj["idAverageBytes"].toObject(); for(auto it = bytesObj.begin(); it != bytesObj.end(); ++it){ uint32_t id = it.key().toUInt(); std::vector<uint8_t> vec(64,0); QJsonArray byteArr = it.value().toArray(); for(int i=0;i<byteArr.size();++i) vec[i]=(uint8_t)byteArr[i].toInt(); obs.idAverageBytes[id] = vec; } m_observations.append(obs); }
    m_iterationLabel->setText(QString("Liczba iteracji: %1").arg(m_iteration));
    updateCandidates(); updateCorrelationTable(); updateSequenceTable(); updateCrossByteTable();
}
EOF

echo "=== Detekcja anomalii wdrożona pomyślnie. Kompiluj: cd build && make -j\$(nproc) ==="
