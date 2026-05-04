#!/usr/bin/env bash
# add_cross_variable_matrix.sh – macierz korelacji między zmiennymi (heatmapa)
set -e

echo "=== Dodawanie macierzy korelacji zmiennych ==="

# 1. Nagłówek – nowe pole dla macierzy i przycisku
sed -i '/QHash<QPair<uint32_t,int>, QPair<double,double>> m_linearModels;/a\
    // Macierz korelacji zmiennych\
    QPushButton  *m_crossVarBtn;\
    QTableWidget *m_crossVarTable;' src/core/AssociativeLearner.h

# Deklaracja metody
sed -i '/void predictNextFrames();/a\    void updateCrossVariableMatrix();' src/core/AssociativeLearner.h

# 2. Konstruktor – nowe elementy UI (wstaw przed końcem)
sed -i '/m_markovTimer = new QTimer(this);/a\
    // --- Macierz korelacji zmiennych ---\
    auto *crossVarLayout = new QHBoxLayout;\
    crossVarLayout->addWidget(new QLabel("Macierz korelacji zmiennych:"));\
    m_crossVarBtn = new QPushButton("Pokaż macierz korelacji zmiennych");\
    crossVarLayout->addWidget(m_crossVarBtn);\
    crossVarLayout->addStretch();\
    mainLayout->addLayout(crossVarLayout);\
    m_crossVarTable = new QTableWidget(0,0);\
    m_crossVarTable->verticalHeader()->hide();\
    m_crossVarTable->horizontalHeader()->setStretchLastSection(true);\
    m_crossVarTable->setShowGrid(false);\
    m_crossVarTable->setAlternatingRowColors(false);\
    m_crossVarTable->setEditTriggers(QAbstractItemView::NoEditTriggers);\
    m_crossVarTable->setMinimumHeight(300);\
    mainLayout->addWidget(m_crossVarTable);\
    connect(m_crossVarBtn, \&QPushButton::clicked, this, \&AssociativeLearner::updateCrossVariableMatrix);' src/core/AssociativeLearner.cpp

# 3. Implementacja updateCrossVariableMatrix (na końcu pliku .cpp)
cat >> src/core/AssociativeLearner.cpp << 'EOF'

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
EOF

echo "=== Macierz korelacji zmiennych dodana. Kompiluj: cd build && make -j\$(nproc) ==="
