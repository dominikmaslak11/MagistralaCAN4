#!/usr/bin/env bash
# =============================================================================
# MagistralaCAN4 – skrypt instalacyjny
# Kopiuje binarkę, ikonę i plik .desktop do systemu.
# Uruchom: sudo bash install.sh
# =============================================================================
set -euo pipefail

if [[ $EUID -ne 0 ]]; then
    echo "Uruchom jako root: sudo bash install.sh"
    exit 1
fi

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="$PROJECT_DIR/build/MagistralaCAN4"
ICON="$PROJECT_DIR/ico.png"
DESKTOP="$PROJECT_DIR/magistrala-can4.desktop"

# Sprawdź czy binarka istnieje
if [[ ! -f "$BIN" ]]; then
    echo "Binarka nie znaleziona: $BIN"
    echo "Skompiluj najpierw: cd build && cmake .. && make -j\$(nproc)"
    exit 1
fi

echo "=== Instalacja MagistralaCAN4 ==="

# 1. Kopiuj binarkę
echo "[1/3] Instalacja binarki..."
cp "$BIN" /usr/local/bin/MagistralaCAN4
chmod 755 /usr/local/bin/MagistralaCAN4
echo "  → /usr/local/bin/MagistralaCAN4"

# 2. Kopiuj ikonę
echo "[2/3] Instalacja ikony..."
mkdir -p /usr/local/share/icons
cp "$ICON" /usr/local/share/icons/magistrala-can4.png
echo "  → /usr/local/share/icons/magistrala-can4.png"

# 3. Kopiuj plik .desktop
echo "[3/3] Instalacja pliku .desktop..."
cp "$DESKTOP" /usr/share/applications/magistrala-can4.desktop
update-desktop-database /usr/share/applications 2>/dev/null || true
echo "  → /usr/share/applications/magistrala-can4.desktop"

echo ""
echo "=== Instalacja zakończona ==="
echo "Aplikacja dostępna w menu systemowym (kategoria Development/Engineering)."
echo "Możesz też uruchomić z terminala: MagistralaCAN4"
