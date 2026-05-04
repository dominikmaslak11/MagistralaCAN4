#!/usr/bin/env bash
# fix_elbow_include.sh – dodanie brakującego QLineSeries
set -e
echo "=== Dodawanie #include <QtCharts/QLineSeries> ==="
sed -i 's|#include <QtCharts/QScatterSeries>|#include <QtCharts/QScatterSeries>\n#include <QtCharts/QLineSeries>|' src/core/AssociativeLearner.h
echo "Poprawione. Kompiluj: cd build && make -j\$(nproc)"
