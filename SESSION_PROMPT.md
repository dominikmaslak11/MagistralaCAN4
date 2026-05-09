# MagistralaCAN4 — Sesja Modernizacyjna v5

## Data sesji: 2026-05-09 (aktualizacja)

─────────────────────────────────────────────────────────────
## ✅ FAZA 1 — ZAKOŃCZONA
─────────────────────────────────────────────────────────────

### 1.1 CanFrame downsizing (2048B → 64B)
- **Commit**: `3369f75`
- `std::array<uint8_t, 2048>` → `std::array<uint8_t, 64>` (CAN 2.0 + FD inline; XL truncated)
- Dodano `byteAt(idx)` z bounds-check dla CAN XL
- Efekt: 40× redukcja pamięci (20MB → 0.6MB przy 10000 ramkach)

### 1.2 DFT → Cooley-Tukey FFT (O(N log N))
- **Commit**: `2c66879`
- **Plik**: `src/core/AssociativeLearner.cpp`
- Zastąpiono naiwny DFT (O(N²)) implementacją Cooley-Tukey radix-2 FFT
- Zero-padding do potęgi 2, bit-reversal, FFT butterflies
- Wszystkie etykiety UI: DFT → FFT
- Build: 3/3, 0 błędów

### 1.3 Style do plików .qss
- **Commit**: `a068cb1`
- **Pliki**: `resources/style_dark.qss` (303 linie), `resources/style_light.qss` (266 linii)
- Usunięto 200+ linii zduplikowanych `setStyleSheet` w 8 plikach widgetów

### 1.4 .clang-format + formatowanie
- **Commit**: `3ea6916`
- Dodano `.clang-format`, `runAsync<Func>()` template helper (QtConcurrent::run)
- async button feedback dla dbscanClustering, runPcaClustering, computeMIC

### 1.6 CAN baudrate selector + katalogi scalone
- **Commit**: `e838509`
- **UI**: `QComboBox` w toolbarze — wybór prędkości CAN (1M, 800K, 500K, 250K, 125K, 100K, 50K, 20K, 10K)
- **ICanDriver**: dodano wirtualne `setBaudRate(const QString&)` z domyślną pustą implementacją
- **PcanDriver**: `setBaudRate()` mapuje etykiety na stałe PCAN (0x0014..0x672F)
- **SlCanDriver**: `setBaudRate()` mapuje etykiety na numeryczne baud rate UART
- **Katalogi**: usunięto `build/` (stary Linux) i `dist/` (duplikat) — jedyny katalog wyjściowy to `build_native/`
- **CMakeLists.txt**: post-build już nie kopiuje do `dist/`

### 1.5 PCAN + SLCAN driver fixes (Windows)
- **Commit**: `0b384d5`
- **PcanDriver**: naprawiono krytyczny bug — odwrócona flaga `PCAN_CHANNEL_OCCUPIED` (driver nigdy nie odczytywał ramek)
- **PcanDriver**: `availableDevices()` — auto-detekcja sprzętu przez `CAN_GetValue(PCAN_CHANNEL_CONDITION)` zamiast hardcoded listy
- **PcanDriver**: obsługa kanałów 1-8 + dynamiczne ładowanie `CAN_GetValue`
- **SlCanDriver**: `detectDevices()` — próbuje 4 prędkości (115200, 500000, 921600, 1M) — candleLight, Lawicel, USBtin, Canable
- **SlCanDriver**: `open()` parsuje prędkość z nazwy urządzenia zwróconej przez auto-detekcję

### Wcześniejsze (sesja v4):
- Event-driven read w CanSniffer (`QThread::msleep(1)`)
- Dekoder sygnałów DBC (`decodeSignals`)
- FFT frequency analysis (UI + wykres)
- DBSCAN clustering, GPU correlation matrix (OpenCL)
- Auto-save / persystencja, RingBuffer SPSC lock-free
- Multi-driver (SocketCAN, PCAN, SLCAN)

─────────────────────────────────────────────────────────────
## 📋 FAZA 2: Dekompozycja (w toku)
─────────────────────────────────────────────────────────────

| # | Zadanie | Priorytet | Status |
|---|---------|-----------|--------|
| 2.1 | Rozbicie AssociativeLearner (2500+ linii) na: LearnerUI, CorrelationEngine, ClusterEngine, AnomalyDetector, SessionManager | WYSOKI | ⬜ pending |
| 2.2 | MainWindow → wydzielić CanStatsPanel (widget) | ŚREDNI | ✅ done (commit pending) |
| 2.3 | Wydzielić logikę eksportu do CanExporter | NISKI | ⬜ pending |
| 2.4 | QtConcurrent::run dla kosztownych korelacji | WYSOKI | 🟡 częściowo (runAsync helper jest, użyty w 3 miejscach) |

### FAZA 3: Infrastruktura

| # | Zadanie |
|---|---------|
| 3.1 | Testy jednostkowe (Google Test + QTest) |
| 3.2 | CI/CD (GitHub Actions: build Win+Linux, testy, clang-tidy, auto-release) |
| 3.3 | Precompiled headers (build 2-3× szybszy) |
| 3.4 | Konfiguracja przez plik (config.toml) zamiast hardcoded wartości |
| 3.5 | Plugin system — hot-reload, stabilne API |

### Sugerowana kolejność (następne kroki):

1. **Dekompozycja AssociativeLearner (2.1)** — 2500+ linii w jednym pliku, największy dług
2. **QtConcurrent dla pozostałych korelacji (2.4)** — uzupełnić async o correlationMatrix, MIC, mutualInformation
3. **CanStatsPanel (2.2)** — wydzielenie statystyk z MainWindow
4. **Testy (3.1)** — bezpieczeństwo przed dalszymi zmianami

─────────────────────────────────────────────────────────────
## 🏗 ARCHITEKTURA
─────────────────────────────────────────────────────────────

```
                    ┌──── MainWindow (UI) ────┐
                    │  CanFrameModel           │
                    │  Dashboard, J1939, UDS…  │
                    └─────────┬───────────────┘
                              │ CanFrame (64B!)
                    ┌─────────┴───────────────┐
                    │      ICanDriver         │
                    └────┬────────────┬─────┘
              ┌──────────┴──┐      ┌───┴───────────┐
              │ SocketCan    │      │ PcanDriver     │
              └─────────────┘      └───────────────┘
              ┌──────────┘
              │ SlCanDriver  │
              └──────────────┘
```

### Kluczowe pliki (rozmiar):

| Plik | Linie | Rola |
|------|-------|------|
| `src/core/AssociativeLearner.cpp` | ~2530 | ML, korelacje, FFT, klastrowanie, UI — **do rozbicia** |
| `src/gui/MainWindow.cpp` | 643 | Główne okno, statystyki, burst detection |
| `src/core/CanFrameModel.cpp` | 336 | Model tabeli Qt, overwrite, sortowanie |
| `src/core/DbcParser.cpp` | 201 | Parser DBC + decodeSignals |
| `src/core/CanSniffer.cpp` | 83 | Wątek odczytu, ring buffer |
| `src/core/RingBuffer.h` | 74 | Lock-free SPSC ring buffer |
| `src/core/CanFrame.h` | 39 | Struktura ramki (64B — zoptymalizowana!) |

### CanFrame — stan po optymalizacji:

```cpp
struct CanFrame {
    uint32_t id = 0;
    bool     extended, rtr, error, fd, xl;
    uint8_t  sdt = 0;
    uint32_t af = 0;
    uint8_t  dlc = 0;                       // 0-8 classic, 0-64 FD, 0-2047 XL
    std::array<uint8_t, 64> data{};          // inline payload (CAN 2.0 + FD)
    uint64_t timestamp = 0;
    [[nodiscard]] uint8_t byteAt(int idx) const;
};
```

Każda kopia przez sygnał Qt: 64B (było 2048B). Przy 10000 ramek = 0.6MB (było 20MB).

─────────────────────────────────────────────────────────────
## 🛠 ŚCIEŻKI NARZĘDZI (Windows)
─────────────────────────────────────────────────────────────

| Narzędzie | Ścieżka |
|-----------|---------|
| ninja | `C:\msys64\ucrt64\bin\ninja.exe` |
| g++ (UCRT64) | `C:\msys64\ucrt64\bin\c++.exe` |
| Qt6 static | `C:\msys64\ucrt64\qt6-static\` |

### Kompilacja:
```cmd
ninja -C build_native -j8
```

### GitHub:
```
Repo: https://github.com/dominikmaslak11/MagistralaCAN4.git
Branch: main
```

─────────────────────────────────────────────────────────────
## 📊 STAN GIT
─────────────────────────────────────────────────────────────

```
e838509 feat: CAN baudrate selector UI + consolidate output to build_native
0b384d5 fix: PCAN inverted OCCUPIED flag + hw detection; SLCAN multi-baud detection
a244bea docs: session prompt v5 — Faza 1 complete, Faza 2 plan
2c66879 perf: DFT → Cooley-Tukey FFT (O(N log N)) + DFT→FFT UI labels
3ea6916 feat: .clang-format config + async button feedback (QtConcurrent::run ready)
a068cb1 refactor: extract inline styles to style_dark.qss / style_light.qss
3369f75 perf: CanFrame downsizing — 2048B→64B payload (40× memory reduction)
7ca82dc docs: session prompt v4 — modernization roadmap + resume instructions
23ca2fe feat: event-driven read (1ms idle) + DBC signal decoder (decodeSignals)
53456f3 feat: FFT frequency analysis for periodic CAN signal detection
c55b6d8 feat: DBSCAN clustering + CanSniffer QWaitCondition signaling
edab4fd v2.1.1: SLCAN driver (serial port) with auto-detection
56a170f v2.1.0: Sniffer ring buffer, per-ID stats, burst detection
```

Branch: `main` — zsynchronizowany z `origin/main`

─────────────────────────────────────────────────────────────
## 🔑 CREDENTIALS
─────────────────────────────────────────────────────────────

```
GitHub Username: dominikmaslak11
Repo URL:        https://github.com/dominikmaslak11/MagistralaCAN4.git
```

Token GitHub — użytkownik przekaże go w nowej sesji.

─────────────────────────────────────────────────────────────
## 🎯 PROMPT WZNOWIENIA (wklej w nowej sesji)
─────────────────────────────────────────────────────────────

Wczytaj plik `SESSION_PROMPT.md`, porównaj lokalne pliki z tym co jest na
githubie (`git fetch && git status`). Chcę kontynuować pracę od Fazy 2.1.

**Faza 2.1 — dekompozycja AssociativeLearner** (plik ~2530 linii):

Rozbij `src/core/AssociativeLearner.cpp` na 5 modułów:

1. **LearnerUI** (`.h` + `.cpp`) — konstrukcja widgetów, layout, connecty
   - Wyciągnij cały konstruktor `AssociativeLearner::AssociativeLearner(QWidget*)`
   - Wszystkie QPushButton, QComboBox, QTableWidget, QChartView — jako osobna klasa

2. **CorrelationEngine** (`.h` + `.cpp`) — korelacje, regresja, MIC, MI
   - `updateCorrelationTable()`, `computeMutualInformation()`, `computeMIC()`,
     `updateCrossByteTable()`, `updateCrossVariableMatrix()`, `applySignificanceFilter()`
   - `trainPrediction()`, `updatePredictionDisplay()`, `LinearModel`

3. **ClusterEngine** (`.h` + `.cpp`) — klastrowanie, PCA, DBSCAN, uczenie
   - `clusterWindows()`, `runPcaClustering()`, `dbscanClustering()`, `autoKMeans()`
   - `markEvent()`, `markNonEvent()`, `updateCandidates()`, `resetLearning()`
   - `buildFeatureVectors()`, `buildWindowFeatures()`, `recalcAdaptiveWindow()`
   - `kMeans()`, `dbscan()`

4. **AnomalyDetector** (`.h` + `.cpp`) — anomalie, auto-detekcja, NN, Markov
   - `startAnomalyMonitoring()`, `stopAnomalyMonitoring()`, `checkAnomaly()`
   - `buildNormalModel()`, `checkAutoEvent()`
   - `trainNeuralNetwork()`, `updateNnPrediction()`, `predictNeural()`
   - `trainMarkovModel()`, `predictNextFrames()`

5. **SessionManager** (`.h` + `.cpp`) — persystencja, eksport
   - `saveSession()`, `loadSession()`, `autoSave()`
   - `exportModels()`, `importModels()`
   - `generateLuaScript()`, `exportHtmlReport()`

Pozostałe w AssociativeLearner (jako fasada):
- `processFrame()`, `addObservation()`, `addVariable()`, `addNewVariable()`
- `onVariableChanged()`, `currentObservations()`
- `updateSequenceTable()`, `updateTimeChart()`, `updateChart()`
- `runFftAnalysis()`, `computeDft()`
- Wszystkie member variables (`m_*`) — rozdzielone do odpowiednich modułów

**Zasady**:
- AssociativeLearner staje się fasadą trzymającą wskaźniki do 5 modułów
- Każdy moduł dostaje referencję do AssociativeLearner (dostęp do `m_frameHistory` itp.)
- Zachowaj sygnatury metod publicznych (nie psuj connectów!)
- Kompiluj po każdej wyodrębnionej klasie: `ninja -C build_native -j8`

Token GitHub jest w sekcji 🔑 CREDENTIALS powyżej. Działaj samodzielnie.

─────────────────────────────────────────────────────────────
## END OF SESSION PROMPT — sesja v5, 2026-05-09
─────────────────────────────────────────────────────────────
