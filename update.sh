#!/usr/bin/env bash
# fix_brush_isvalid.sh – naprawa isValid() w QBrush + porządki
set -e
echo "=== Naprawa QBrush::isValid i duplikatu include ==="
sed -i 's/if (it && it->background().isValid())/if (it \&\& it->background().color() != QColor(Qt::transparent) \&\& it->background().style() != Qt::NoBrush)/' src/core/AssociativeLearner.cpp
sed -i '2{/^#include "Logger.h"$/d}' src/core/AssociativeLearner.cpp
echo "Poprawione. Kompiluj: cd build && make -j\$(nproc)"
