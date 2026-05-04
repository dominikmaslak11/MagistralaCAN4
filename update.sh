#!/usr/bin/env bash
# add_sequence_prediction.sh – predykcja następnej ramki (łańcuch Markowa)
set -e

echo "=== Dodawanie predykcji sekwencji ==="

# 1. Nagłówek – nowe pola
sed -i '/QHash<QPair<uint32_t,int>, QPair<double,double>> m_linearModels;/a\
    // Predykcja sekwencji (Markov)\
    QPushButton  *m_trainMarkovBtn;\
    QTableWidget *m_markovTable;\
    QTimer       *m_markovTimer;\
    QHash<uint32_t, QHash<uint32_t, int>> m_transitions; // fromId -> toId -> count\
    QHash<uint32_t, uint32_t> m_markovBestNext;        // fromId -> best next id\
    QHash<uint32_t, double>   m_markovProb;            // fromId -> probability' src/core/AssociativeLearner.h

sed -i '/void checkAutoEvent();/a\    void trainMarkovModel();\n    void predictNextFrames();' src/core/AssociativeLearner.h

# 2. Konstruktor – UI i timer
sed -i '/m_chart = new QChart/a\
    // --- Predykcja sekwencji (Markov) ---\
    auto *markovLayout = new QHBoxLayout;\
    markovLayout->addWidget(new QLabel("Predykcja następnej ramki:"));\
    m_trainMarkovBtn = new QPushButton("Trenuj model Markowa");\
    markovLayout->addWidget(m_trainMarkovBtn);\
    markovLayout->addStretch();\
    mainLayout->addLayout(markovLayout);\
    m_markovTable = new QTableWidget(0,3);\
    m_markovTable->setHorizontalHeaderLabels({"Ostatnie ID","Przewidywane ID","Prawdopodobieństwo"});\
    m_markovTable->verticalHeader()->hide(); m_markovTable->horizontalHeader()->setStretchLastSection(true);\
    m_markovTable->setShowGrid(false); m_markovTable->setAlternatingRowColors(false);\
    m_markovTable->setEditTriggers(QAbstractItemView::NoEditTriggers);\
    m_markovTable->setMinimumHeight(200);\
    mainLayout->addWidget(m_markovTable);\
    m_markovTimer = new QTimer(this);\
    connect(m_markovTimer, \&QTimer::timeout, this, \&AssociativeLearner::predictNextFrames);\
    connect(m_trainMarkovBtn, \&QPushButton::clicked, this, [this]() {\
        trainMarkovModel();\
        if (!m_transitions.isEmpty()) m_markovTimer->start(1000);\
    });' src/core/AssociativeLearner.cpp

# 3. Implementacja trainMarkovModel i predictNextFrames (na końcu pliku .cpp)
cat >> src/core/AssociativeLearner.cpp << 'EOF'

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
EOF

echo "=== Predykcja sekwencji dodana. Kompiluj: cd build && make -j\$(nproc) ==="
