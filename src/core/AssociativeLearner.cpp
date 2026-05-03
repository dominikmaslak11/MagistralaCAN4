#include "AssociativeLearner.h"
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

AssociativeLearner::AssociativeLearner(QWidget *parent) : QWidget(parent) {
    auto *mainLayout = new QVBoxLayout(this);

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

    auto *separator1 = new QFrame;
    separator1->setFrameShape(QFrame::HLine);
    separator1->setStyleSheet("background-color: #e94560;");
    mainLayout->addWidget(separator1);

    auto *valueLayout = new QHBoxLayout;
    valueLayout->addWidget(new QLabel("Wartość (np. temperatura):"));
    m_valueInput = new QLineEdit;
    m_valueInput->setPlaceholderText("0.0");
    m_addObsBtn = new QPushButton("Dodaj obserwację");
    valueLayout->addWidget(m_valueInput);
    valueLayout->addWidget(m_addObsBtn);
    mainLayout->addLayout(valueLayout);

    m_correlationTable = new QTableWidget(0, 4);
    m_correlationTable->setHorizontalHeaderLabels({"CAN ID", "Bajt", "Korelacja", ""});
    m_correlationTable->verticalHeader()->hide();
    m_correlationTable->horizontalHeader()->setStretchLastSection(true);
    m_correlationTable->setShowGrid(false);
    m_correlationTable->setAlternatingRowColors(false);
    m_correlationTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_correlationTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mainLayout->addWidget(m_correlationTable);

    auto *separator2 = new QFrame;
    separator2->setFrameShape(QFrame::HLine);
    separator2->setStyleSheet("background-color: #e94560;");
    mainLayout->addWidget(separator2);

    // Sekwencje
    auto *seqLayout = new QHBoxLayout;
    seqLayout->addWidget(new QLabel("Długość sekwencji:"));
    m_ngramCombo = new QComboBox;
    m_ngramCombo->addItems({"Bigram (2)", "Trigram (3)"});
    seqLayout->addWidget(m_ngramCombo);
    seqLayout->addStretch();
    mainLayout->addLayout(seqLayout);

    m_sequenceTable = new QTableWidget(0, 3);
    m_sequenceTable->setHorizontalHeaderLabels({"Sekwencja ID", "Wystąpienia w zdarzeniach", "Pewność"});
    m_sequenceTable->verticalHeader()->hide();
    m_sequenceTable->horizontalHeader()->setStretchLastSection(true);
    m_sequenceTable->setShowGrid(false);
    m_sequenceTable->setAlternatingRowColors(false);
    m_sequenceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_sequenceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mainLayout->addWidget(m_sequenceTable);

    auto *separator3 = new QFrame;
    separator3->setFrameShape(QFrame::HLine);
    separator3->setStyleSheet("background-color: #e94560;");
    mainLayout->addWidget(separator3);

    // Korelacja międzybajtowa
    mainLayout->addWidget(new QLabel("Korelacja międzybajtowa (Cross‑Byte)"));
    m_crossByteTable = new QTableWidget(0, 5);
    m_crossByteTable->setHorizontalHeaderLabels({"ID1", "Bajt1", "ID2", "Bajt2", "Korelacja"});
    m_crossByteTable->verticalHeader()->hide();
    m_crossByteTable->horizontalHeader()->setStretchLastSection(true);
    m_crossByteTable->setShowGrid(false);
    m_crossByteTable->setAlternatingRowColors(false);
    m_crossByteTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_crossByteTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    mainLayout->addWidget(m_crossByteTable);

    auto *serLayout = new QHBoxLayout;
    m_saveBtn = new QPushButton("💾 Zapisz sesję");
    m_loadBtn = new QPushButton("📂 Wczytaj sesję");
    serLayout->addWidget(m_saveBtn);
    serLayout->addWidget(m_loadBtn);
    mainLayout->addLayout(serLayout);

    connect(m_markEventBtn, &QPushButton::clicked, this, &AssociativeLearner::markEvent);
    connect(m_resetBtn, &QPushButton::clicked, this, &AssociativeLearner::resetLearning);
    connect(m_addObsBtn, &QPushButton::clicked, this, &AssociativeLearner::addObservation);
    connect(m_saveBtn, &QPushButton::clicked, this, &AssociativeLearner::saveSession);
    connect(m_loadBtn, &QPushButton::clicked, this, &AssociativeLearner::loadSession);
    connect(m_ngramCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AssociativeLearner::updateSequenceTable);

    setStyleSheet(R"(
        QPushButton {
            background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560;
            border-radius: 4px; padding: 6px 15px; font-weight: bold;
        }
        QPushButton:hover { background: #e94560; color: #0a0e17; }
        QLineEdit, QComboBox {
            background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560;
            border-radius: 4px; padding: 4px 8px;
        }
    )");
}

AssociativeLearner::~AssociativeLearner() = default;

void AssociativeLearner::processFrame(const CanFrame &frame) {
    m_frameHistory.push_back(frame);
    if (m_frameHistory.size() > HISTORY_MAX)
        m_frameHistory.pop_front();
}

void AssociativeLearner::markEvent() {
    if (m_frameHistory.empty()) return;
    uint64_t latestTs = m_frameHistory.back().timestamp;
    QVector<CanFrame> window;
    for (const auto &f : m_frameHistory) {
        int64_t t = f.timestamp;
        if (t >= latestTs - m_adaptiveBefore && t <= latestTs + m_adaptiveAfter)
            window.append(f);
    }
    if (window.size() < 3) return;
    EventRecord record;
    record.windowFrames = window;
    record.idFeatures = buildFeatureVectors(window);
    m_events.push_back(record);
    m_iteration++;
    m_iterationLabel->setText(QString("Liczba iteracji: %1").arg(m_iteration));
    if (m_iteration == 1) recalcAdaptiveWindow();
    emit eventMarked(m_iteration);
    updateCandidates();
    updateSequenceTable();
}

void AssociativeLearner::resetLearning() {
    m_events.clear();
    m_observations.clear();
    m_iteration = 0;
    m_iterationLabel->setText("Liczba iteracji: 0");
    m_candidateModel->clear();
    m_correlationTable->setRowCount(0);
    m_sequenceTable->setRowCount(0);
    m_crossByteTable->setRowCount(0);
}

void AssociativeLearner::addObservation() {
    bool ok;
    double value = m_valueInput->text().toDouble(&ok);
    if (!ok) return;
    if (m_frameHistory.empty()) return;
    uint64_t latestTs = m_frameHistory.back().timestamp;
    QVector<CanFrame> window;
    for (const auto &f : m_frameHistory) {
        int64_t t = f.timestamp;
        if (t >= latestTs - m_adaptiveBefore && t <= latestTs + m_adaptiveAfter)
            window.append(f);
    }
    if (window.empty()) return;
    QHash<uint32_t, QVector<CanFrame>> grouped;
    for (const auto &f : window)
        grouped[f.id].append(f);
    ValueObservation obs;
    obs.value = value;
    for (auto it = grouped.begin(); it != grouped.end(); ++it) {
        uint32_t id = it.key();
        const auto &frames = it.value();
        std::vector<uint8_t> avgBytes(64, 0);
        for (const auto &f : frames)
            for (int i = 0; i < f.dlc; ++i)
                avgBytes[i] += f.data[i] / frames.size();
        obs.idAverageBytes[id] = avgBytes;
    }
    m_observations.append(obs);
    m_valueInput->clear();
    updateCorrelationTable();
    updateCrossByteTable();   // automatycznie po nowej obserwacji
}

void AssociativeLearner::recalcAdaptiveWindow() {
    if (m_events.isEmpty()) return;
    const auto &window = m_events.first().windowFrames;
    if (window.size() < 2) return;
    QVector<int64_t> deltas;
    for (int i = 1; i < window.size(); ++i)
        deltas.push_back(window[i].timestamp - window[i-1].timestamp);
    double mean = std::accumulate(deltas.begin(), deltas.end(), 0) / (double)deltas.size();
    int64_t newHalfWindow = std::max<int64_t>(100000, std::min<int64_t>(2000000, static_cast<int64_t>(3.0 * mean)));
    m_adaptiveBefore = newHalfWindow;
    m_adaptiveAfter  = newHalfWindow / 3;
}

QHash<uint32_t, QVector<float>> AssociativeLearner::buildFeatureVectors(const QVector<CanFrame> &window) {
    QHash<uint32_t, QVector<CanFrame>> grouped;
    for (const auto &f : window) grouped[f.id].append(f);
    QHash<uint32_t, QVector<float>> result;
    for (auto it = grouped.begin(); it != grouped.end(); ++it) {
        uint32_t id = it.key();
        const auto &frames = it.value();
        QVector<float> feats(67);
        feats[0] = static_cast<float>(frames.size());
        QVector<int64_t> deltas;
        for (int i = 1; i < frames.size(); ++i)
            deltas.push_back(frames[i].timestamp - frames[i-1].timestamp);
        if (deltas.isEmpty()) { feats[1] = 0; feats[2] = 0; }
        else {
            double sum = std::accumulate(deltas.begin(), deltas.end(), 0);
            feats[1] = static_cast<float>(sum / deltas.size()) / 1000.0f;
            double sq_sum = 0;
            for (int64_t d : deltas) sq_sum += (d - feats[1]) * (d - feats[1]);
            feats[2] = static_cast<float>(std::sqrt(sq_sum / deltas.size()) / 1000.0f);
        }
        for (int b = 0; b < 64; ++b) {
            float avg = 0;
            for (const auto &f : frames) if (b < f.dlc) avg += f.data[b];
            avg /= frames.size();
            feats[3 + b] = avg / 255.0f;
        }
        result[id] = feats;
    }
    return result;
}

void AssociativeLearner::updateCandidates() {
    if (m_events.isEmpty()) return;
    if (m_events.size() == 1) {
        const auto &feats = m_events.first().idFeatures;
        QVector<Candidate> cands;
        for (auto it = feats.begin(); it != feats.end(); ++it)
            cands.append({it.key(), "Pierwsze zdarzenie", 0.0f, 1});
        m_candidateModel->setCandidates(cands);
        return;
    }
    QSet<uint32_t> commonIds;
    bool first = true;
    for (const auto &ev : m_events) {
        QSet<uint32_t> ids;
        for (auto it = ev.idFeatures.begin(); it != ev.idFeatures.end(); ++it) ids.insert(it.key());
        if (first) { commonIds = ids; first = false; }
        else commonIds &= ids;
    }
    QVector<Candidate> candidates;
    for (uint32_t id : commonIds) {
        QVector<QVector<float>> vectors;
        for (const auto &ev : m_events) vectors.append(ev.idFeatures[id]);
        int N = vectors.size();
        float totalSim = 0.0f;
        int pairs = 0;
        for (int i = 0; i < N; ++i) {
            for (int j = i+1; j < N; ++j) {
                float dot = 0, normA = 0, normB = 0;
                for (int k = 0; k < vectors[i].size(); ++k) {
                    float a = vectors[i][k], b = vectors[j][k];
                    dot += a*b; normA += a*a; normB += b*b;
                }
                totalSim += dot / (std::sqrt(normA)*std::sqrt(normB) + 1e-6f);
                pairs++;
            }
        }
        float confidence = (pairs > 0) ? (totalSim / pairs) : 0.0f;
        candidates.append({id, QString("ID 0x%1").arg(id,3,16,QChar('0')).toUpper(), confidence, (int)vectors.size()});
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
    m_candidateModel->setCandidates(candidates);
}

void AssociativeLearner::updateCorrelationTable() {
    if (m_observations.size() < 3) { m_correlationTable->setRowCount(0); return; }
    QSet<uint32_t> commonIds;
    bool first = true;
    for (const auto &obs : m_observations) {
        QSet<uint32_t> ids;
        for (auto it = obs.idAverageBytes.begin(); it != obs.idAverageBytes.end(); ++it) ids.insert(it.key());
        if (first) { commonIds = ids; first = false; }
        else commonIds &= ids;
    }
    struct CorrEntry { uint32_t id; int byte; double correlation; };
    QVector<CorrEntry> entries;
    for (uint32_t id : commonIds) {
        for (int byte = 0; byte < 64; ++byte) {
            QVector<double> vals, bytes;
            for (const auto &obs : m_observations) {
                auto it = obs.idAverageBytes.find(id);
                if (it != obs.idAverageBytes.end()) {
                    vals.append(obs.value);
                    bytes.append(static_cast<double>(it.value()[byte]));
                }
            }
            int N = vals.size();
            if (N < 3) continue;
            double sumX=0,sumY=0,sumXY=0,sumX2=0,sumY2=0;
            for (int i=0;i<N;++i) {
                double x=vals[i], y=bytes[i];
                sumX+=x; sumY+=y; sumXY+=x*y; sumX2+=x*x; sumY2+=y*y;
            }
            double denom = sqrt((N*sumX2 - sumX*sumX)*(N*sumY2 - sumY*sumY));
            double corr = (denom != 0) ? (N*sumXY - sumX*sumY)/denom : 0.0;
            entries.append({id, byte, corr});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const CorrEntry &a, const CorrEntry &b) { return fabs(a.correlation) > fabs(b.correlation); });
    m_correlationTable->setRowCount(entries.size());
    for (int i=0; i<entries.size(); ++i) {
        auto &e = entries[i];
        m_correlationTable->setItem(i,0,new QTableWidgetItem(QString("0x%1").arg(e.id,3,16,QChar('0')).toUpper()));
        m_correlationTable->setItem(i,1,new QTableWidgetItem(QString::number(e.byte)));
        m_correlationTable->setItem(i,2,new QTableWidgetItem(QString::number(e.correlation,'f',3)));
        QColor color = (fabs(e.correlation)>0.7)?QColor("#00ffaa"):(fabs(e.correlation)>0.4)?QColor("#ffaa00"):QColor("#ff66cc");
        m_correlationTable->item(i,0)->setForeground(QColor("#c0c0c0"));
        m_correlationTable->item(i,1)->setForeground(QColor("#c0c0c0"));
        m_correlationTable->item(i,2)->setForeground(color);
    }
}

void AssociativeLearner::updateSequenceTable() {
    if (m_events.size() < 2) { m_sequenceTable->setRowCount(0); return; }
    int n = m_ngramCombo->currentIndex() + 2;
    QHash<QString, int> eventCounts;
    for (const auto &ev : m_events) {
        const auto &frames = ev.windowFrames;
        if (frames.size() < n) continue;
        QSet<QString> localSet;
        for (int i=0; i<=frames.size()-n; ++i) {
            QStringList ids;
            for (int j=0; j<n; ++j) ids.append(QString::number(frames[i+j].id));
            localSet.insert(ids.join("→"));
        }
        for (const auto &k : localSet) eventCounts[k]++;
    }
    int total = m_events.size();
    QVector<QPair<QString,int>> sorted;
    for (auto it = eventCounts.begin(); it != eventCounts.end(); ++it) sorted.append({it.key(), it.value()});
    std::sort(sorted.begin(), sorted.end(), [](const QPair<QString,int> &a, const QPair<QString,int> &b) { return a.second > b.second; });
    if (sorted.size()>20) sorted.resize(20);
    m_sequenceTable->setRowCount(sorted.size());
    for (int i=0; i<sorted.size(); ++i) {
        const auto &p = sorted[i];
        double prob = (double)p.second / total;
        m_sequenceTable->setItem(i,0,new QTableWidgetItem(p.first));
        m_sequenceTable->setItem(i,1,new QTableWidgetItem(QString::number(p.second)));
        m_sequenceTable->setItem(i,2,new QTableWidgetItem(QString("%1%").arg(prob*100.0,0,'f',1)));
        QColor color = (prob>0.7)?QColor("#00ffaa"):(prob>0.4)?QColor("#ffaa00"):QColor("#ff66cc");
        m_sequenceTable->item(i,0)->setForeground(QColor("#c0c0c0"));
        m_sequenceTable->item(i,1)->setForeground(QColor("#c0c0c0"));
        m_sequenceTable->item(i,2)->setForeground(color);
    }
}

void AssociativeLearner::updateCrossByteTable() {
    if (m_observations.size() < 3) {
        m_crossByteTable->setRowCount(0);
        return;
    }

    // Zbierz listę wszystkich par (id, byte) występujących we WSZYSTKICH obserwacjach
    QVector<QPair<uint32_t,int>> allPairs;
    QSet<QPair<uint32_t,int>> firstSet;
    bool firstObs = true;
    for (const auto &obs : m_observations) {
        QSet<QPair<uint32_t,int>> thisSet;
        for (auto it = obs.idAverageBytes.begin(); it != obs.idAverageBytes.end(); ++it) {
            uint32_t id = it.key();
            for (int b=0; b<64; ++b)
                thisSet.insert({id, b});
        }
        if (firstObs) { firstSet = thisSet; firstObs = false; }
        else firstSet &= thisSet;
    }
    allPairs = firstSet.values();

    struct CrossEntry {
        uint32_t id1; int byte1;
        uint32_t id2; int byte2;
        double correlation;
    };
    QVector<CrossEntry> crossEntries;

    // Porównaj każdą parę (i,j) gdzie i<j
    for (int i=0; i<allPairs.size(); ++i) {
        for (int j=i+1; j<allPairs.size(); ++j) {
            auto p1 = allPairs[i], p2 = allPairs[j];
            QVector<double> xVals, yVals;
            for (const auto &obs : m_observations) {
                auto it1 = obs.idAverageBytes.find(p1.first);
                auto it2 = obs.idAverageBytes.find(p2.first);
                if (it1 != obs.idAverageBytes.end() && it2 != obs.idAverageBytes.end()) {
                    xVals.append(static_cast<double>(it1.value()[p1.second]));
                    yVals.append(static_cast<double>(it2.value()[p2.second]));
                }
            }
            int N = xVals.size();
            if (N < 3) continue;
            double sumX=0,sumY=0,sumXY=0,sumX2=0,sumY2=0;
            for (int k=0;k<N;++k) {
                double x=xVals[k], y=yVals[k];
                sumX+=x; sumY+=y; sumXY+=x*y; sumX2+=x*x; sumY2+=y*y;
            }
            double denom = sqrt((N*sumX2 - sumX*sumX)*(N*sumY2 - sumY*sumY));
            double corr = (denom != 0) ? (N*sumXY - sumX*sumY)/denom : 0.0;
            crossEntries.append({p1.first, p1.second, p2.first, p2.second, corr});
        }
    }

    std::sort(crossEntries.begin(), crossEntries.end(),
              [](const CrossEntry &a, const CrossEntry &b) { return fabs(a.correlation) > fabs(b.correlation); });

    m_crossByteTable->setRowCount(crossEntries.size());
    for (int i=0; i<crossEntries.size(); ++i) {
        const auto &e = crossEntries[i];
        m_crossByteTable->setItem(i,0,new QTableWidgetItem(QString("0x%1").arg(e.id1,3,16,QChar('0')).toUpper()));
        m_crossByteTable->setItem(i,1,new QTableWidgetItem(QString::number(e.byte1)));
        m_crossByteTable->setItem(i,2,new QTableWidgetItem(QString("0x%1").arg(e.id2,3,16,QChar('0')).toUpper()));
        m_crossByteTable->setItem(i,3,new QTableWidgetItem(QString::number(e.byte2)));
        m_crossByteTable->setItem(i,4,new QTableWidgetItem(QString::number(e.correlation,'f',3)));
        QColor color = (fabs(e.correlation)>0.7)?QColor("#00ffaa"):(fabs(e.correlation)>0.4)?QColor("#ffaa00"):QColor("#ff66cc");
        m_crossByteTable->item(i,0)->setForeground(QColor("#c0c0c0"));
        m_crossByteTable->item(i,1)->setForeground(QColor("#c0c0c0"));
        m_crossByteTable->item(i,2)->setForeground(QColor("#c0c0c0"));
        m_crossByteTable->item(i,3)->setForeground(QColor("#c0c0c0"));
        m_crossByteTable->item(i,4)->setForeground(color);
    }
}

void AssociativeLearner::saveSession() {
    QString path = QFileDialog::getSaveFileName(this, "Zapisz sesję", "", "JSON (*.json)");
    if (path.isEmpty()) return;
    QJsonObject root;
    root["iteration"] = m_iteration;
    root["adaptiveBefore"] = (qint64)m_adaptiveBefore;
    root["adaptiveAfter"] = (qint64)m_adaptiveAfter;
    QJsonArray eventsArr;
    for (const auto &ev : m_events) {
        QJsonObject evObj;
        QJsonArray framesArr;
        for (const auto &f : ev.windowFrames) {
            QJsonObject fObj;
            fObj["id"] = (int)f.id; fObj["dlc"] = f.dlc;
            QJsonArray dataArr;
            for (int i=0; i<f.dlc; ++i) dataArr.append(f.data[i]);
            fObj["data"] = dataArr; fObj["timestamp"] = (qint64)f.timestamp;
            framesArr.append(fObj);
        }
        evObj["windowFrames"] = framesArr; eventsArr.append(evObj);
    }
    root["events"] = eventsArr;
    QJsonArray obsArr;
    for (const auto &obs : m_observations) {
        QJsonObject obsObj; obsObj["value"] = obs.value;
        QJsonObject bytesObj;
        for (auto it = obs.idAverageBytes.begin(); it != obs.idAverageBytes.end(); ++it) {
            QJsonArray byteArr; for (uint8_t b : it.value()) byteArr.append((int)b);
            bytesObj[QString::number(it.key())] = byteArr;
        }
        obsObj["idAverageBytes"] = bytesObj; obsArr.append(obsObj);
    }
    root["observations"] = obsArr;
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) file.write(QJsonDocument(root).toJson());
}

void AssociativeLearner::loadSession() {
    QString path = QFileDialog::getOpenFileName(this, "Wczytaj sesję", "", "JSON (*.json)");
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { QMessageBox::warning(this,"Błąd","Nie można otworzyć pliku."); return; }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll()); file.close();
    QJsonObject root = doc.object();
    m_events.clear(); m_observations.clear();
    m_iteration = root["iteration"].toInt();
    m_adaptiveBefore = root["adaptiveBefore"].toVariant().toLongLong();
    m_adaptiveAfter = root["adaptiveAfter"].toVariant().toLongLong();
    QJsonArray eventsArr = root["events"].toArray();
    for (const auto &evVal : eventsArr) {
        EventRecord rec; QJsonObject evObj = evVal.toObject();
        QJsonArray framesArr = evObj["windowFrames"].toArray();
        for (const auto &fVal : framesArr) {
            QJsonObject fObj = fVal.toObject();
            CanFrame f; f.id=fObj["id"].toInt(); f.dlc=fObj["dlc"].toInt();
            QJsonArray dataArr = fObj["data"].toArray();
            for (int i=0;i<dataArr.size();++i) f.data[i]=(uint8_t)dataArr[i].toInt();
            f.timestamp = fObj["timestamp"].toVariant().toLongLong();
            rec.windowFrames.append(f);
        }
        rec.idFeatures = buildFeatureVectors(rec.windowFrames);
        m_events.append(rec);
    }
    QJsonArray obsArr = root["observations"].toArray();
    for (const auto &obsVal : obsArr) {
        ValueObservation obs; QJsonObject obsObj = obsVal.toObject();
        obs.value = obsObj["value"].toDouble();
        QJsonObject bytesObj = obsObj["idAverageBytes"].toObject();
        for (auto it = bytesObj.begin(); it != bytesObj.end(); ++it) {
            uint32_t id = it.key().toUInt();
            std::vector<uint8_t> vec(64,0);
            QJsonArray byteArr = it.value().toArray();
            for (int i=0;i<byteArr.size();++i) vec[i]=(uint8_t)byteArr[i].toInt();
            obs.idAverageBytes[id] = vec;
        }
        m_observations.append(obs);
    }
    m_iterationLabel->setText(QString("Liczba iteracji: %1").arg(m_iteration));
    updateCandidates(); updateCorrelationTable(); updateSequenceTable(); updateCrossByteTable();
}
