# MagistralaCAN4 — Sesja Modernizacyjna v5

## Data sesji: 2026-05-10

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
## 📋 FAZA 2: Dekompozycja (częściowo)
─────────────────────────────────────────────────────────────

| # | Zadanie | Priorytet | Status |
|---|---------|-----------|--------|
| 2.1 | Rozbicie AssociativeLearner (2500+ linii) na: LearnerUI, CorrelationEngine, ClusterEngine, AnomalyDetector, SessionManager | WYSOKI | ⬜ pending (odłożone) |
| 2.2 | MainWindow → wydzielić CanStatsPanel (widget) | ŚREDNI | ✅ done |
| 2.3 | Wydzielić logikę eksportu do CanExporter | NISKI | ⬜ pending |
| 2.4 | QtConcurrent::run dla kosztownych korelacji | WYSOKI | ✅ done (7 metod + 3 już było = 10) |

### FAZA 3: Infrastruktura

| # | Zadanie | Status |
|---|---------|--------|
| 3.1 | Testy jednostkowe (Google Test + QTest) | ⬜ pending |
| 3.2 | CI/CD (GitHub Actions: build Win+Linux) | ✅ done |
| 3.3 | Precompiled headers (build 2-3× szybszy) | ✅ done |
| 3.4 | Konfiguracja przez plik (config.toml) zamiast hardcoded wartości | ⬜ pending |
| 3.5 | Plugin system — hot-reload, stabilne API | ⬜ pending |

### Ukończone modernizacje (sesja v5 2026-05-10):

- **3.3 Precompiled headers** — `12133cc`: `target_precompile_headers` w CMake, `pch.h` rozszerzony do 82 nagłówków STL+Qt, 43/43 TU
- **3.2 CI/CD** — `5da3b3f`: `.github/workflows/build.yml` — Windows (MSYS2 UCRT64) + Linux (Ubuntu 24.04), Ninja, Qt6, Lua, OpenCL

### Sugerowana kolejność (następne kroki):

1. **Testy (3.1)** — bezpieczeństwo przed dalszymi zmianami
2. **Dekompozycja AssociativeLearner (2.1)** — 2500+ linii w jednym pliku, największy dług
3. **Konfiguracja config.toml (3.4)** — zastąpienie hardcoded wartości

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
5da3b3f ci: GitHub Actions build workflow (Windows MSYS2 + Ubuntu 24.04)
12133cc build: PCH via target_precompile_headers
bb67573 fix: move CanStatsPanel init before setupCentralWidget (fixes SIGSEGV)
d51c2e6 perf: async all heavy correlation/clustering/ML methods via runAsync (Faza 2.4)
e264125 refactor: extract CanStatsPanel widget from MainWindow (Faza 2.2)
3b5c9a6 fix: PCAN auto-detection via CAN_Initialize + push build/ artifacts
206be55 feat: Git LFS for *.exe/*.dll + precompiled header ready (pch.h)
e838509 feat: CAN baudrate selector UI + consolidate output to build_native
0b384d5 fix: PCAN inverted OCCUPIED flag + hw detection; SLCAN multi-baud detection
2c66879 perf: DFT → Cooley-Tukey FFT (O(N log N)) + DFT→FFT UI labels
3ea6916 feat: .clang-format config + async button feedback (QtConcurrent::run ready)
a068cb1 refactor: extract inline styles to style_dark.qss / style_light.qss
3369f75 perf: CanFrame downsizing — 2048B→64B payload (40× memory reduction)
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
githubie (`git fetch && git status`). Kontynuuj od **Fazy 3.1 — testy jednostkowe**.

**Faza 3.1 — testy jednostkowe (Google Test + QTest):**

1. Dodaj Google Test do CMake (FetchContent lub find_package)
2. Utwórz testy dla struktur danych:
   - `test_canframe.cpp` — `CanFrame::byteAt()` bounds-check, domyślne wartości, rozmiar (64B)
   - `test_ringbuffer.cpp` — `RingBuffer<T>` SPSC lock-free: push/pop, empty, overflow
   - `test_dbcparser.cpp` — parsowanie pliku DBC, decodeSignals()
3. Utwórz testy dla logiki (bez sprzętu):
   - `test_canframemodel.cpp` — model tabeli Qt: rowCount, columnCount, overwrite, sort
4. Podepnij testy do CMake (`add_executable`, `enable_testing()`, `add_test()`)
5. Dodaj target `ninja test` lub `ctest`
6. Zintegruj testy z CI/CD (dodaj krok w `.github/workflows/build.yml`)

Token GitHub: użytkownik przekaże w nowej sesji.

─────────────────────────────────────────────────────────────
## END OF SESSION PROMPT — sesja v5, 2026-05-10
─────────────────────────────────────────────────────────────
