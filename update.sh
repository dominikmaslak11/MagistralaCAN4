#!/usr/bin/env bash
# fix_mic_qvector.sh – poprawki kompilacji MIC
set -e
echo "=== Naprawa konwersji QVector -> std::vector w MIC ==="

# 1. Popraw dwie linie przypisania std::vector = QVector
sed -i 's/std::vector<double> xSorted = values;/std::vector<double> xSorted(values.begin(), values.end());/' src/core/AssociativeLearner.cpp
sed -i 's/std::vector<double> ySorted = byteVals;/std::vector<double> ySorted(byteVals.begin(), byteVals.end());/' src/core/AssociativeLearner.cpp

# 2. Usuń zdublowane #include "Logger.h" na początku pliku (opcjonalnie, dla porządku)
sed -i '2{/^#include "Logger.h"$/d}' src/core/AssociativeLearner.cpp

echo "Gotowe. Kompiluj: cd build && make -j\$(nproc)"
