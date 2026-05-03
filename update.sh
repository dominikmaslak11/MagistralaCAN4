#!/usr/bin/env bash
# fix_resetLearning.sh – dodaje implementację resetLearning
set -e
echo "=== Dodawanie brakującej definicji resetLearning ==="

# Wstaw przed funkcją addObservation
sed -i '/^void AssociativeLearner::addObservation()/i\
void AssociativeLearner::resetLearning() {\
    m_events.clear();\
    m_observations.clear();\
    m_iteration = 0;\
    m_iterationLabel->setText("Liczba iteracji: 0");\
    m_candidateModel->clear();\
    m_correlationTable->setRowCount(0);\
}\
' src/core/AssociativeLearner.cpp

echo "Gotowe. Kompiluj: cd build && make -j\$(nproc)"
