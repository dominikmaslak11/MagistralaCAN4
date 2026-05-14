#!/usr/bin/env bash
# =============================================================================
# MagistralaCAN4 – push.sh
# Kompiluje aplikację, dodaje binarek do commita i wypycha na GitHub.
# Uruchomienie:  bash push.sh "opis commita"
# =============================================================================
set -euo pipefail

COMMIT_MSG="${1:-update binary}"

echo "=== 1. Kompilacja ==="
cd "$(dirname "$0")"
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release > /dev/null
make -j"$(nproc)" 2>&1 | tail -3
cd ..

echo "=== 2. Dodawanie binarka do git ==="
git add -f build/MagistralaCAN4

echo "=== 3. Commit ==="
git commit -m "$COMMIT_MSG" || echo "(brak zmian do commita)"

echo "=== 4. Push ==="
git push origin main

echo "=== Gotowe ==="
