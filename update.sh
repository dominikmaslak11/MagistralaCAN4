#!/usr/bin/env bash
# fix_auto_declarations.sh – wstawia brakujące deklaracje pól auto-detekcji
set -e

echo "=== Wstawianie deklaracji pól auto-detekcji ==="

# W pliku AssociativeLearner.h znajdź linię 'QTimer       *m_autoEventTimer;' i zaraz za nią wstaw trzy nowe deklaracje
sed -i '/QTimer       \*m_autoEventTimer;/a\    QCheckBox   *m_autoEventCheck;\n    QLineEdit   *m_autoEventThreshold;\n    QLabel      *m_autoEventLabel;' src/core/AssociativeLearner.h

echo "Deklaracje dodane. Kompiluj: cd build && make -j\$(nproc)"
