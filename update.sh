#!/usr/bin/env bash
# add_mutual_information.sh – Mutual Information + powiększone tabele + GPU
set -e

echo "=== Wdrażanie Mutual Information, powiększanie tabel, GPU ==="

# --- Instalacja brakujących zależności (na wszelki wypadek) ---
# OpenCL jest już zainstalowane, upewnijmy się, że g++ obsługuje standard C++17 i mamy libc6-dev
sudo apt update
sudo apt install -y build-essential libc6-dev opencl-headers ocl-icd-opencl-dev clinfo 2>/dev/null || echo "Zależności już zainstalowane"

# --- 1. Nagłówek AssociativeLearner.h – nowe pola i metoda ---
# Dodajemy przed deklaracją sekcji prywatnej (na końcu listy zmiennych)
sed -i '/QHash<QPair<uint32_t,int>, QPair<double,double>> m_linearModels;/a\
    // Mutual Information\
    QPushButton  *m_miBtn;\
    QTableWidget *m_miTable;' src/core/AssociativeLearner.h

# Deklaracja metody
sed -i '/void updateCrossVariableMatrix();/a\    void computeMutualInformation();' src/core/AssociativeLearner.h

# --- 2. Konstruktor – nowe UI (między macierzą korelacji a wykresem) ---
# Wstawiamy zaraz za blokiem macierzy korelacji
sed -i '/connect(m_crossVarBtn, \&QPushButton::clicked, this, \&AssociativeLearner::updateCrossVariableMatrix);/a\
    // --- Mutual Information ---\
    auto *miLayout = new QHBoxLayout;\
    miLayout->addWidget(new QLabel("Zależności nieliniowe (MI):"));\
    m_miBtn = new QPushButton("Oblicz Mutual Information");\
    miLayout->addWidget(m_miBtn);\
    miLayout->addStretch();\
    mainLayout->addLayout(miLayout);\
    m_miTable = new QTableWidget(0,4);\
    m_miTable->setHorizontalHeaderLabels({"CAN ID","Bajt","MI (nat)","Porównanie"});\
    m_miTable->verticalHeader()->hide(); m_miTable->horizontalHeader()->setStretchLastSection(true);\
    m_miTable->setShowGrid(false); m_miTable->setAlternatingRowColors(false);\
    m_miTable->setEditTriggers(QAbstractItemView::NoEditTriggers);\
    m_miTable->setMinimumHeight(500);  /* wystarczająco na 20 wierszy */\
    mainLayout->addWidget(m_miTable);\
    connect(m_miBtn, \&QPushButton::clicked, this, \&AssociativeLearner::computeMutualInformation);' src/core/AssociativeLearner.cpp

# --- 3. Zwiększenie minimalnej wysokości WSZYSTKICH tabel do 500 pikseli (około 20 wierszy) ---
sed -i '/m_candidatesView->setMinimumHeight(400);/s/400/500/' src/core/AssociativeLearner.cpp
sed -i '/m_correlationTable->setMinimumHeight(400);/s/400/500/' src/core/AssociativeLearner.cpp
sed -i '/m_sequenceTable->setMinimumHeight(400);/s/400/500/' src/core/AssociativeLearner.cpp
sed -i '/m_crossByteTable->setMinimumHeight(400);/s/400/500/' src/core/AssociativeLearner.cpp
sed -i '/m_clusterTable->setMinimumHeight(400);/s/400/500/' src/core/AssociativeLearner.cpp
sed -i '/m_predictionTable->setMinimumHeight(400);/s/400/500/' src/core/AssociativeLearner.cpp
sed -i '/m_anomalyTable->setMinimumHeight(400);/s/400/500/' src/core/AssociativeLearner.cpp
sed -i '/m_miTable->setMinimumHeight(500);/!b; n;' src/core/AssociativeLearner.cpp # już ustawione
# Dla m_crossVarTable też ustawiamy minimalną wysokość (jeśli nie istnieje, dodamy)
sed -i '/m_crossVarTable->setShowGrid/a\    m_crossVarTable->setMinimumHeight(500);' src/core/AssociativeLearner.cpp

# --- 4. Implementacja computeMutualInformation (na końcu pliku .cpp) ---
cat >> src/core/AssociativeLearner.cpp << 'EOF'

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
EOF

echo "=== Mutual Information + powiększone tabele dodane. Kompiluj: cd build && make -j\$(nproc) ==="
