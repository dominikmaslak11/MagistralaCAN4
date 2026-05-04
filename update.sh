#!/usr/bin/env bash
# fix_pca.sh – poprawia błędy PCA: qrand, kolory, wariancja, macierz kowariancji
set -e

echo "=== Poprawki PCA ==="

# 1. Zmień qrand() na QRandomGenerator + dodaj #include
sed -i 's/#include <random>/#include <random>\n#include <QRandomGenerator>/' src/core/AssociativeLearner.cpp
sed -i 's/(qrand() % 1000) \/ 1000.0/QRandomGenerator::global()->generateDouble()/' src/core/AssociativeLearner.cpp

# 2. Usuń pusty blockingMap
sed -i '/QtConcurrent::blockingMap(cov, \[&\](QVector<double> &row) {/,/});/d' src/core/AssociativeLearner.cpp

# 3. Zapamiętaj ślad macierzy kowariancji przed deflacją i poprawnie policz wyjaśnioną wariancję
sed -i '/auto \[eig1, pc1\] = powerIteration(initVec);/i\
    double origTrace = 0.0;\
    for (int d = 0; d < dim; ++d) origTrace += cov[d][d];' src/core/AssociativeLearner.cpp

sed -i '/auto \[eig2, pc2\] = powerIteration(initVec);/a\
    double totalVar = origTrace + eig1;  // ślad sprzed deflacji = trace po deflacji + usunięta wartość własna' src/core/AssociativeLearner.cpp

sed -i 's/double varExplained = (eig1 + eig2) \/ (eig1 + eig2);/double varExplained = (eig1 + eig2) \/ totalVar;/' src/core/AssociativeLearner.cpp

# 4. Zastąp kolorowanie punktów osobnymi seriami (QScatterSeries nie obsługuje kolorowania per punkt)
sed -i '/m_pcaSeries->clear();/,/m_pcaChart->setTitle/{
    s/m_pcaSeries->clear();/m_pcaChart->removeAllSeries();\
    QScatterSeries *s1 = new QScatterSeries(); s1->setName("Klaster 1"); s1->setColor(QColor("#00ffaa"));\
    QScatterSeries *s2 = new QScatterSeries(); s2->setName("Klaster 2"); s2->setColor(QColor("#ffaa00"));\
    QScatterSeries *s3 = new QScatterSeries(); s3->setName("Klaster 3"); s3->setColor(QColor("#ff66cc"));\
    for (int i = 0; i < N; ++i) {\
        if (assignments[i] == 0) s1->append(points2D[i]);\
        else if (assignments[i] == 1) s2->append(points2D[i]);\
        else s3->append(points2D[i]);\
    }\
    m_pcaChart->addSeries(s1);\
    m_pcaChart->addSeries(s2);\
    m_pcaChart->addSeries(s3);/
    s/m_pcaSeries->setColor(colors\[assignments\[i\]\]);//
    s/m_pcaSeries->append(points2D\[i\]);//
}' src/core/AssociativeLearner.cpp

echo "Poprawione. Kompiluj: cd build && make -j\$(nproc)"
