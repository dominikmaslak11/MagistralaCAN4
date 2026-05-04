#!/usr/bin/env bash
# fix_qevent_include.sh – dodaje #include <QEvent>
set -e
echo "=== Dodawanie include <QEvent> ==="
sed -i 's/#include <QPushButton>/#include <QPushButton>\n#include <QEvent>/' src/core/FrameDetailWidget.h
echo "Poprawione. Kompiluj: cd build && make -j\$(nproc)"
