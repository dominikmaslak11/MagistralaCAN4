#include "AssociativeLearner.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <numeric>
#include <cmath>
#include <set>

AssociativeLearner::AssociativeLearner(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);

    m_markEventBtn = new QPushButton("🔴 Zarejestruj zdarzenie");
    m_resetBtn = new QPushButton("Resetuj uczenie");
    m_iterationLabel = new QLabel("Liczba iteracji: 0");
    m_iterationLabel->setStyleSheet("color: #00ffaa; font-weight: bold;");

    layout->addWidget(m_markEventBtn);
    layout->addWidget(m_resetBtn);
    layout->addWidget(m_iterationLabel);

    m_candidateModel = new CandidateModel(this);
    m_candidatesView = new QTableView;
    m_candidatesView->setModel(m_candidateModel);
    m_candidatesView->verticalHeader()->hide();
    m_candidatesView->horizontalHeader()->setStretchLastSection(true);
    m_candidatesView->setShowGrid(false);
    m_candidatesView->setAlternatingRowColors(false);
    m_candidatesView->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_candidatesView);

    connect(m_markEventBtn, &QPushButton::clicked, this, &AssociativeLearner::markEvent);
    connect(m_resetBtn, &QPushButton::clicked, this, &AssociativeLearner::resetLearning);

    setStyleSheet(R"(
        QPushButton {
            background: #1a1a2e; color: #00ffaa; border: 1px solid #e94560;
            border-radius: 4px; padding: 6px 15px; font-weight: bold;
        }
        QPushButton:hover { background: #e94560; color: #0a0e17; }
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
    m_iteration = 0;
    m_iterationLabel->setText("Liczba iteracji: 0");
    m_candidateModel->clear();
}

QHash<uint32_t, QVector<float>> AssociativeLearner::buildFeatureVectors(const QVector<CanFrame> &window) {
    QHash<uint32_t, QVector<CanFrame>> grouped;
    for (const auto &f : window)
        grouped[f.id].append(f);

    QHash<uint32_t, QVector<float>> result;
    for (auto it = grouped.begin(); it != grouped.end(); ++it) {
        uint32_t id = it.key();
        const auto &frames = it.value();
        QVector<float> feats(16);
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

        for (int b = 0; b < 8; ++b) {
            float avg = 0;
            for (const auto &f : frames) avg += f.data[b];
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
        for (auto it = feats.begin(); it != feats.end(); ++it) {
            cands.append({it.key(), "Pierwsze zdarzenie", 0.0f, 1});
        }
        m_candidateModel->setCandidates(cands);
        return;
    }

    // Zbierz ID, które występują we wszystkich zdarzeniach
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
        // Oblicz średnie podobieństwo kosinusowe między wszystkimi parami zdarzeń dla tego ID
        QVector<QVector<float>> vectors;
        for (const auto &ev : m_events)
            vectors.append(ev.idFeatures[id]);

        // Jeśli mamy GPU, użyj go do korelacji (mała liczba wektorów – CPU wystarczy)
        // Użyjemy prostego CPU, bo to tylko kilka wektorów.
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

    // Sortuj malejąco według pewności
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
    m_candidateModel->setCandidates(candidates);
}
