#!/usr/bin/env bash
# add_serialization.sh – zapis i odczyt sesji asocjacyjnej (JSON)
set -e

echo "=== Krok 2: Serializacja sesji asocjacyjnej ==="

# 1. AssociativeLearner.h – dodajemy deklaracje slotów i przycisków
cat > src/core/AssociativeLearner.h << 'EOF'
#pragma once
#include <QWidget>
#include <QPushButton>
#include <QLabel>
#include <QTableView>
#include <QTableWidget>
#include <QLineEdit>
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
    void saveSession();       // zapis
    void loadSession();       // odczyt

signals:
    void eventMarked(int iteration);

private:
    void updateCandidates();
    void updateCorrelationTable();
    QHash<uint32_t, QVector<float>> buildFeatureVectors(const QVector<CanFrame> &window);

    static constexpr int64_t WINDOW_BEFORE = 500000;
    static constexpr int64_t WINDOW_AFTER  = 200000;

    QPushButton *m_markEventBtn;
    QPushButton *m_resetBtn;
    QLabel      *m_iterationLabel;
    QTableView  *m_candidatesView;

    QLineEdit    *m_valueInput;
    QPushButton  *m_addObsBtn;
    QTableWidget *m_correlationTable;

    QPushButton  *m_saveBtn;
    QPushButton  *m_loadBtn;

    CandidateModel *m_candidateModel;
    GpuCorrelator   m_correlator;

    std::deque<CanFrame> m_frameHistory;
    static constexpr int HISTORY_MAX = 20000;

    QVector<EventRecord> m_events;
    QVector<ValueObservation> m_observations;
    int m_iteration = 0;
};
EOF

# 2. AssociativeLearner.cpp – dodajemy implementację save/load (uproszczony JSON)
cat > src/core/AssociativeLearner.cpp << 'EOF'
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

    auto *separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet("background-color: #e94560;");
    mainLayout->addWidget(separator);

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

    // Przyciski serializacji
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

    setStyleSheet(R"(
        QPushButton {
            background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560;
            border-radius: 4px; padding: 6px 15px; font-weight: bold;
        }
        QPushButton:hover { background: #e94560; color: #0a0e17; }
        QLineEdit {
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
        if (t >= latestTs - WINDOW_BEFORE && t <= latestTs + WINDOW_AFTER)
            window.append(f);
    }
    if (window.size() < 3) return;
    EventRecord record;
    record.windowFrames = window;
    record.idFeatures = buildFeatureVectors(window);
    m_events.push_back(record);
    m_iteration++;
    m_iterationLabel->setText(QString("Liczba iteracji: %1").arg(m_iteration));
    emit eventMarked(m_iteration);
    updateCandidates();
}

void AssociativeLearner::resetLearning() {
    m_events.clear();
    m_observations.clear();
    m_iteration = 0;
    m_iterationLabel->setText("Liczba iteracji: 0");
    m_candidateModel->clear();
    m_correlationTable->setRowCount(0);
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
        if (t >= latestTs - WINDOW_BEFORE && t <= latestTs + WINDOW_AFTER)
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
        for (const auto &f : frames) {
            for (int i = 0; i < f.dlc; ++i)
                avgBytes[i] += f.data[i] / frames.size();
        }
        obs.idAverageBytes[id] = avgBytes;
    }
    m_observations.append(obs);
    m_valueInput->clear();
    updateCorrelationTable();
}

QHash<uint32_t, QVector<float>> AssociativeLearner::buildFeatureVectors(const QVector<CanFrame> &window) {
    QHash<uint32_t, QVector<CanFrame>> grouped;
    for (const auto &f : window)
        grouped[f.id].append(f);
    QHash<uint32_t, QVector<float>> result;
    for (auto it = grouped.begin(); it != grouped.end(); ++it) {
        uint32_t id = it.key();
        const auto &frames = it.value();
        QVector<float> feats(67);
        feats[0] = static_cast<float>(frames.size());
        QVector<int64_t> deltas;
        for (int i = 1; i < frames.size(); ++i)
            deltas.push_back(frames[i].timestamp - frames[i-1].timestamp);
        if (deltas.isEmpty()) {
            feats[1] = 0; feats[2] = 0;
        } else {
            double sum = std::accumulate(deltas.begin(), deltas.end(), 0);
            feats[1] = static_cast<float>(sum / deltas.size()) / 1000.0f;
            double sq_sum = 0;
            for (int64_t d : deltas)
                sq_sum += (d - feats[1]) * (d - feats[1]);
            feats[2] = static_cast<float>(std::sqrt(sq_sum / deltas.size()) / 1000.0f);
        }
        for (int b = 0; b < 64; ++b) {
            float avg = 0;
            for (const auto &f : frames)
                if (b < f.dlc) avg += f.data[b];
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
        for (auto it = ev.idFeatures.begin(); it != ev.idFeatures.end(); ++it)
            ids.insert(it.key());
        if (first) { commonIds = ids; first = false; }
        else commonIds &= ids;
    }
    QVector<Candidate> candidates;
    for (uint32_t id : commonIds) {
        QVector<QVector<float>> vectors;
        for (const auto &ev : m_events)
            vectors.append(ev.idFeatures[id]);
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
        candidates.append({id, QString("ID 0x%1").arg(id,3,16,QChar('0')).toUpper(),
                           confidence, (int)vectors.size()});
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
    m_candidateModel->setCandidates(candidates);
}

void AssociativeLearner::updateCorrelationTable() {
    if (m_observations.size() < 3) {
        m_correlationTable->setRowCount(0);
        return;
    }
    QSet<uint32_t> commonIds;
    bool first = true;
    for (const auto &obs : m_observations) {
        QSet<uint32_t> ids;
        for (auto it = obs.idAverageBytes.begin(); it != obs.idAverageBytes.end(); ++it)
            ids.insert(it.key());
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
            double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0, sumY2 = 0;
            for (int i = 0; i < N; ++i) {
                double x = vals[i], y = bytes[i];
                sumX += x; sumY += y;
                sumXY += x * y;
                sumX2 += x * x;
                sumY2 += y * y;
            }
            double denom = sqrt((N * sumX2 - sumX * sumX) * (N * sumY2 - sumY * sumY));
            double corr = (denom != 0) ? (N * sumXY - sumX * sumY) / denom : 0.0;
            entries.append({id, byte, corr});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const CorrEntry &a, const CorrEntry &b) {
        return fabs(a.correlation) > fabs(b.correlation);
    });
    m_correlationTable->setRowCount(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const auto &e = entries[i];
        m_correlationTable->setItem(i, 0, new QTableWidgetItem(QString("0x%1").arg(e.id,3,16,QChar('0')).toUpper()));
        m_correlationTable->setItem(i, 1, new QTableWidgetItem(QString::number(e.byte)));
        m_correlationTable->setItem(i, 2, new QTableWidgetItem(QString::number(e.correlation, 'f', 3)));
        QColor color = (fabs(e.correlation) > 0.7) ? QColor("#00ffaa") :
                        (fabs(e.correlation) > 0.4) ? QColor("#ffaa00") : QColor("#ff66cc");
        m_correlationTable->item(i, 0)->setForeground(QColor("#c0c0c0"));
        m_correlationTable->item(i, 1)->setForeground(QColor("#c0c0c0"));
        m_correlationTable->item(i, 2)->setForeground(color);
    }
}

// --- Serializacja ---
void AssociativeLearner::saveSession() {
    QString path = QFileDialog::getSaveFileName(this, "Zapisz sesję", "", "JSON (*.json)");
    if (path.isEmpty()) return;

    QJsonObject root;
    root["iteration"] = m_iteration;

    QJsonArray eventsArr;
    for (const auto &ev : m_events) {
        QJsonObject evObj;
        QJsonArray framesArr;
        for (const auto &f : ev.windowFrames) {
            QJsonObject fObj;
            fObj["id"] = (int)f.id;
            fObj["dlc"] = f.dlc;
            QJsonArray dataArr;
            for (int i = 0; i < f.dlc; ++i) dataArr.append(f.data[i]);
            fObj["data"] = dataArr;
            fObj["timestamp"] = (qint64)f.timestamp;
            framesArr.append(fObj);
        }
        evObj["windowFrames"] = framesArr;
        // cechy pomijamy – można odbudować
        eventsArr.append(evObj);
    }
    root["events"] = eventsArr;

    QJsonArray obsArr;
    for (const auto &obs : m_observations) {
        QJsonObject obsObj;
        obsObj["value"] = obs.value;
        QJsonObject bytesObj;
        for (auto it = obs.idAverageBytes.begin(); it != obs.idAverageBytes.end(); ++it) {
            QJsonArray byteArr;
            for (uint8_t b : it.value()) byteArr.append((int)b);
            bytesObj[QString::number(it.key())] = byteArr;
        }
        obsObj["idAverageBytes"] = bytesObj;
        obsArr.append(obsObj);
    }
    root["observations"] = obsArr;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
    } else {
        QMessageBox::warning(this, "Błąd", "Nie można zapisać pliku.");
    }
}

void AssociativeLearner::loadSession() {
    QString path = QFileDialog::getOpenFileName(this, "Wczytaj sesję", "", "JSON (*.json)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Błąd", "Nie można otworzyć pliku.");
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    QJsonObject root = doc.object();

    m_events.clear();
    m_observations.clear();
    m_iteration = root["iteration"].toInt();

    QJsonArray eventsArr = root["events"].toArray();
    for (const auto &evVal : eventsArr) {
        EventRecord rec;
        QJsonObject evObj = evVal.toObject();
        QJsonArray framesArr = evObj["windowFrames"].toArray();
        for (const auto &fVal : framesArr) {
            QJsonObject fObj = fVal.toObject();
            CanFrame f;
            f.id = fObj["id"].toInt();
            f.dlc = fObj["dlc"].toInt();
            QJsonArray dataArr = fObj["data"].toArray();
            for (int i = 0; i < dataArr.size(); ++i)
                f.data[i] = (uint8_t)dataArr[i].toInt();
            f.timestamp = fObj["timestamp"].toVariant().toLongLong();
            rec.windowFrames.append(f);
        }
        rec.idFeatures = buildFeatureVectors(rec.windowFrames);
        m_events.append(rec);
    }

    QJsonArray obsArr = root["observations"].toArray();
    for (const auto &obsVal : obsArr) {
        ValueObservation obs;
        QJsonObject obsObj = obsVal.toObject();
        obs.value = obsObj["value"].toDouble();
        QJsonObject bytesObj = obsObj["idAverageBytes"].toObject();
        for (auto it = bytesObj.begin(); it != bytesObj.end(); ++it) {
            uint32_t id = it.key().toUInt();
            std::vector<uint8_t> vec(64, 0);
            QJsonArray byteArr = it.value().toArray();
            for (int i = 0; i < byteArr.size(); ++i)
                vec[i] = (uint8_t)byteArr[i].toInt();
            obs.idAverageBytes[id] = vec;
        }
        m_observations.append(obs);
    }

    m_iterationLabel->setText(QString("Liczba iteracji: %1").arg(m_iteration));
    updateCandidates();
    updateCorrelationTable();
}
EOF

echo "=== Serializacja dodana. Kompiluj: cd build && make -j$(nproc) ==="
