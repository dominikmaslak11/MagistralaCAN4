#!/usr/bin/env bash
# MagistralaCAN4 – instalacja środowiska OpenCL (obsługa Kali)
set -e

echo "=== Instalacja OpenCL (wymagane do GPU) ==="

if [ -f /etc/os-release ]; then
    . /etc/os-release
    DISTRO=$ID
else
    echo "Brak /etc/os-release – spróbuję ręcznie."
    DISTRO="ubuntu"  # domyślna ścieżka
fi

case $DISTRO in
    ubuntu|debian|kali)
        echo "Wykryto dystrybucję bazującą na Debianie: $DISTRO"
        sudo apt update
        sudo apt install -y opencl-headers ocl-icd-opencl-dev clinfo
        # Opcjonalny sterownik Mesa (jeśli nie ma dedykowanego GPU)
        sudo apt install -y mesa-opencl-icd 2>/dev/null || true
        ;;
    fedora|rhel|centos)
        sudo dnf install -y opencl-headers ocl-icd-devel clinfo
        ;;
    arch|manjaro)
        sudo pacman -S --noconfirm opencl-headers ocl-icd clinfo
        ;;
    *)
        echo "Dystrybucja $DISTRO nie jest bezpośrednio obsługiwana, ale można spróbować ręcznie:"
        echo "  - Zainstaluj pakiety: opencl-headers, ocl-icd-opencl-dev (lub odpowiednik), clinfo"
        echo "  - Skontaktuj się, jeśli potrzebujesz pomocy."
        exit 1
        ;;
esac

echo ""
echo "=== Sprawdzanie dostępnych urządzeń OpenCL ==="
if command -v clinfo &>/dev/null; then
    clinfo | grep -E "Device Name|Device Type|Device Version" || echo "UWAGA: Nie wykryto urządzeń OpenCL – możesz potrzebować dodatkowych sterowników GPU."
else
    echo "clinfo nie jest dostępne – sprawdź instalację."
fi

echo ""
echo "=== Instalacja zakończona. Teraz uruchom fix_cmake_opencl.sh ==="
