#!/usr/bin/env bash
# fix_non_event_simple.sh – dodaje #include "Logger.h" do AssociativeLearner.cpp
set -e
echo "=== Dodawanie include Logger.h ==="
# Wstaw #include "Logger.h" na samym początku pliku (przed pierwszym istniejącym include)
sed -i '1i #include "Logger.h"' src/core/AssociativeLearner.cpp
echo "Gotowe. Kompiluj: cd build && make -j\$(nproc)"
