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
| 2.2 | MainWindow → wydzielić CanStatsPanel (widget) | ŚREDNI | ⬜ pending |
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
## 🎯 PROMPT WZNOWIENIA
─────────────────────────────────────────────────────────────

Wznawiamy sesję modernizacji MagistralaCAN4 v5.
Ostatni stan:
- **Faza 1: ZAKOŃCZONA** — CanFrame 64B, FFT Cooley-Tukey, style .qss, .clang-format
- Commit `2c66879` na GitHub
- **Następny krok: Faza 2.1 — dekompozycja AssociativeLearner** (2500+ linii → 5 modułów)
- Po nim: 2.4 (uzupełnienie QtConcurrent::run), 2.2 (CanStatsPanel)

Token GitHub: (zapytaj użytkownika)

Kontynuuj od Fazy 2.1.

─────────────────────────────────────────────────────────────
## END OF SESSION PROMPT — sesja v5, 2026-05-09
─────────────────────────────────────────────────────────────
