# MagistralaCAN4 — Sesja Modernizacyjna v4

## Data sesji: 2026-05-09

─────────────────────────────────────────────────────────────
## ✅ CO ZOSTAŁO ZROBIONE (SESJA v4)
─────────────────────────────────────────────────────────────

### 1. Pull + push — synchronizacja z GitHub
- Pobrano najnowszy stan z `origin/main`
- Wypchnięto lokalny commit FFT (`53456f3`)

### 2. Event-driven read w CanSniffer
- **Plik**: `src/core/CanSniffer.cpp`
- **Zmiana**: `QThread::msleep(1)` przy pustej ramce zamiast `continue`
- **Include**: dodano `#include <QThread>`
- **Efekt**: eliminacja busy-wait na Windows (PcanDriver, SlCanDriver nieblokujące)

### 3. Dekoder sygnałów DBC (wielobitowy)
- **Plik**: `src/core/DbcParser.h`, `src/core/DbcParser.cpp`
- **Nowa metoda**: `decodeSignals(id, data, dlc) → QHash<QString, double>`
- **Obsługa**: Little-endian (Intel), Big-endian (Motorola), signed/unsigned, scale+offset
- **Refactor**: `signalDescriptions()` używa teraz `decodeSignals()`

### 4. Weryfikacja istniejących funkcji
- QOpenGLWidget dla wszystkich 6 wykresów (PCA, elbow, scatter, time, FFT, FPS) — było
- Per-ID stats + burst detection w MainWindow — było
- DBSCAN clustering — było
- FFT frequency analysis — było
- GPU correlation matrix (OpenCL) — było
- Auto-save / persystencja (timer 5min + destruktor) — było
- RingBuffer SPSC lock-free — było
- Multi-driver (SocketCAN, PCAN, SLCAN) — było

### 5. Kompilacja i push
- Build: 13/13 kroków, 0 błędów, 2 warningi SFINAE (istniejące)
- Commit: `23ca2fe` — "feat: event-driven read (1ms idle) + DBC signal decoder (decodeSignals)"
- Push: `main → origin/main`

─────────────────────────────────────────────────────────────
## 📋 PLANY MODERNIZACJI (FAZA 1-3)
─────────────────────────────────────────────────────────────

### FAZA 1: Higiena (niski koszt, wysoki zysk)

| # | Zadanie | Priorytet |
|---|---------|-----------|
| 1.1 | CanFrame — union/payload pointer zamiast `std::array<uint8_t, 2048>` (40B zamiast 2KB) | WYSOKI |
| 1.2 | DFT → FFT (Cooley-Tukey, O(N log N) zamiast O(N²)) | WYSOKI |
| 1.3 | Style do plików `.qss` (style_dark.qss, style_light.qss) | ŚREDNI |
| 1.4 | `.clang-format` + formatowanie całości | ŚREDNI |

### FAZA 2: Dekompozycja (średni koszt, strukturalny zysk)

| # | Zadanie | Priorytet |
|---|---------|-----------|
| 2.1 | Rozbicie AssociativeLearner (2513 linii) na: LearnerUI, CorrelationEngine, ClusterEngine, AnomalyDetector, SessionManager | WYSOKI |
| 2.2 | MainWindow → wydzielić CanStatsPanel (widget) | ŚREDNI |
| 2.3 | Wydzielić logikę eksportu do CanExporter | NISKI |
| 2.4 | QtConcurrent::run dla kosztownych korelacji (GUI nie zamiera) | WYSOKI |

### FAZA 3: Infrastruktura (długoterminowy zysk)

| # | Zadanie |
|---|---------|
| 3.1 | Testy jednostkowe (Google Test + QTest) |
| 3.2 | CI/CD (GitHub Actions: build Win+Linux, testy, clang-tidy, auto-release) |
| 3.3 | Precompiled headers (build 2-3× szybszy) |
| 3.4 | Konfiguracja przez plik (config.toml) zamiast hardcoded wartości |
| 3.5 | Plugin system — hot-reload, stabilne API |

### Sugerowana kolejność startowa:

1. **CanFrame payload (1.1)** — natychmiastowy zysk pamięciowy
2. **DFT → FFT (1.2)** — jeden plik, duży efekt
3. **Style do .qss (1.3)** — czysta separacja
4. **QtConcurrent dla korelacji (2.4)** — GUI przestaje się zacinać
5. **Dekompozycja AssociativeLearner (2.1)** — największy dług techniczny
6. **Testy (3.1)** — bezpieczeństwo przed dalszymi zmianami

─────────────────────────────────────────────────────────────
## 🏗 ARCHITEKTURA
─────────────────────────────────────────────────────────────

```
                    ┌──── MainWindow (UI) ────┐
                    │  CanFrameModel           │
                    │  Dashboard, J1939, UDS…  │
                    └─────────┬───────────────┘
                              │ CanFrame
                    ┌─────────┴───────────────┐
                    │      ICanDriver         │
                    │  open/close/readFrame/  │
                    │  writeFrame/devices     │
                    └────┬────────────┬─────┘
              ┌──────────┴──┐      ┌───┴───────────┐
              │ SocketCan    │      │ PcanDriver     │
              │ Driver       │      │ (PCANBasic.dll)│
              │ (Linux)      │      │ (Windows)      │
              └─────────────┘      └───────────────┘
              ┌──────────┘
              │ SlCanDriver  │
              │ (serial port)│
              └──────────────┘
```

### Kluczowe pliki (rozmiar):

| Plik | Linie | Rola |
|------|-------|------|
| `src/core/AssociativeLearner.cpp` | 2513 | ML, korelacje, klastrowanie, DFT, UI |
| `src/gui/MainWindow.cpp` | 643 | Główne okno, statystyki, burst detection |
| `src/core/CanFrameModel.cpp` | 336 | Model tabeli Qt, overwrite, sortowanie |
| `src/core/CanSniffer.cpp` | 83 | Wątek odczytu, ring buffer |
| `src/core/DbcParser.cpp` | 201 | Parser DBC + decodeSignals |
| `src/core/RingBuffer.h` | 74 | Lock-free SPSC ring buffer |
| `src/core/CanFrame.h` | 27 | Struktura ramki (2048B!) |

### Problem: CanFrame waży 2KB

```cpp
struct CanFrame {
    uint32_t id;
    // ... flags ...
    std::array<uint8_t, 2048> data{};  // ← 2KB na każdą ramkę!
};
```

Każda kopia przez sygnał Qt (wartość) to 2KB. Przy 10000 ramek = 20MB.

─────────────────────────────────────────────────────────────
## 🛠 ŚCIEŻKI NARZĘDZI (Windows)
─────────────────────────────────────────────────────────────

| Narzędzie | Ścieżka |
|-----------|---------|
| cmake (pip) | `C:\msys64\ucrt64\lib\python3.14\site-packages\cmake\data\bin\cmake.exe` |
| ninja | `C:\msys64\ucrt64\bin\ninja.exe` |
| g++ (UCRT64) | `C:\msys64\ucrt64\bin\c++.exe` |
| git | `git` (w PATH) |
| Qt6 static | `C:\msys64\ucrt64\qt6-static\` |

### Kompilacja (z cmd.exe):
```cmd
C:\msys64\ucrt64\bin\ninja.exe -C E:\MagistralaCAN4\build_native -j8
```

### GitHub:
```
Username: dominikmaslak11
Token: (zapytaj użytkownika — GitHub PAT)
Repo: https://github.com/dominikmaslak11/MagistralaCAN4.git
```

─────────────────────────────────────────────────────────────
## 📊 STAN GIT
─────────────────────────────────────────────────────────────

```
23ca2fe feat: event-driven read (1ms idle) + DBC signal decoder (decodeSignals)
53456f3 feat: FFT frequency analysis for periodic CAN signal detection
c55b6d8 feat: DBSCAN clustering + CanSniffer QWaitCondition signaling
edab4fd v2.1.1: SLCAN driver (serial port) with auto-detection
56a170f v2.1.0: Sniffer ring buffer, per-ID stats, burst detection
```

Branch: `main` — zsynchronizowany z `origin/main`

─────────────────────────────────────────────────────────────
## 🎯 PROMPT WZNOWIENIA
─────────────────────────────────────────────────────────────

Wznawiamy sesję modernizacji MagistralaCAN4 (SESSION_PROMPT.md).
Ostatni stan:
- Commit 23ca2fe na GitHub (event-driven read + DBC signal decoder)
- Zatwierdzony plan: Faza 1 → Faza 2 → Faza 3
- Następny krok: CanFrame payload (union/payload pointer zamiast 2048B array)
- Potem: DFT → FFT, style do .qss, QtConcurrent dla korelacji

Token GitHub: (zapytaj użytkownika)

Kontynuuj od miejsca, gdzie skończyliśmy.

─────────────────────────────────────────────────────────────
## END OF SESSION PROMPT — sesja v4, 2026-05-09
─────────────────────────────────────────────────────────────
