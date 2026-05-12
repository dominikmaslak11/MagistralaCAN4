# Sesja optymalizacji — MagistralaCAN4 — 2026-05-12

## Faza 1: Pipeline "Ruch CAN" ✅
- ✅ #1–#10: QSortFilterProxyModel, cache DisplayRole, wirtualne scrollowanie, batch'owanie, throttle, heatmap, presety

## Faza 2: Stabilność i infrastruktura ✅
- ✅ #11–#23: QSettings, async I/O, testy, MDF4Writer, backoff, async export, MRU, zstd, undo/redo, plugin protection, CanPlayer

---

## Faza 3: Modernizacja uczenia asocjacyjnego ✅

### Faza A: Dekompozycja silnika ✅
- ✅ #24 `LearningEngine` — czysty C++, STL, shared_mutex, 37 algorytmów ML
- ✅ #25 `AssociativeLearner` → cienka warstwa UI (2836→1372 LOC, -52%)

### Faza B: Online / incremental learning ✅
- ✅ #26 Ring buffer dla obserwacji (max 10k)
- ✅ #27 Exponential forgetting (e^(-λ·Δt), ważona korelacja Pearsona)
- ✅ #28 Korelacje online (Welford's algorithm)
- ✅ #29 Anomalie EWMA + adaptive threshold

### Faza C: Unowocześnienie modeli predykcyjnych ✅
- ✅ #30 MLP v2 — Adam optimizer + L2 regularization + early stopping
- ✅ #31 Gradient Boosted Trees (XGBoost-lite)
- ✅ #32 Przedziały ufności (bootstrap residuals, 95% CI)
- ✅ #33 Multi-target (trainAllPredictions, predictRealtimeAll)

### Faza D: Wydajność i skalowalność ✅
- ✅ #34 Sparse cross-byte correlation (pomijanie bajtów o zerowej wariancji)
- ✅ #35 DBSCAN z k-d tree (5D, O(N log N))
- ✅ #36 k-means++ initialization
- ✅ #37 PCA: Jacobi eigen-decomposition dla macierzy 5×5

### Faza E: Zaawansowana analiza ✅
- ✅ #38 Granger causality (F-test, OLS, Gaussian elimination)
- ✅ #39 Change-point detection (Binary Segmentation)
- ✅ #40 Cross-correlation z przesunięciem czasowym (lag [-10,+10])
- ⏭ #41 t-SNE — pominięte (opcjonalne, GPU)

### Faza F: Trwałość i odtwarzalność ✅
- ✅ #42–45 JSON serializacja, checkpoint save/load, ONNX stub, session resume

---

## Dodatkowe funkcje

### GPU Acceleration ✅
- `GpuCompute` — STL-based, OpenCL, 3 kernele (correlation, Pearson, k-means)
- PCA i kMeans używają GPU z CPU fallbackiem

### Binary Search w OfflineAnalyzer ✅
- Klasyczne wyszukiwanie binarne w plikach candump
- Odtwarza pierwszą połowę zakresu, pyta "Czy zjawisko wystąpiło?"
- TAK → zawęża do pierwszej połowy, NIE → przechodzi do drugiej
- Rekurencyjnie aż do pojedynczej ramki (~log₂ N pytań)
- Log ostatnich 20 ramek, pauza podczas odtwarzania

### Ciągłe nagrywanie candump ✅
- Checkbox "Nagrywaj candump" (domyślnie ✓)
- Zapis do C:\candump\candump_YYYYMMDD_HHMMSS.log
- Automatyczny start/stop przy sniffingu

### Diagnostyka CAN ✅
- Status bar pokazuje backend drivera i baud rate
- No-data timeout (5s) z żółtym ostrzeżeniem
- Licznik ramek, logowanie błędów PCAN

---

---
- PcanDriver::readFrame — usunięty martwy warunek CAN_GetStatus & 0x100
- Deadlock w markEvent → recalcAdaptiveWindow (reentrant mutex)
- Integer division w addObservation (uint8_t / size_t → 0)
- Korelacje: majority threshold zamiast strict intersection
- readFrame: classic CAN przed FD (FD = ILLOPERATION na PCAN-USB)


---

## Naprawione bugi (2026-05-11)

| Bug | Przyczyna | Rozwiązanie |
|-----|-----------|-------------|
| Crash przy "Zarejestruj zdarzenie" | Deadlock: `markEvent`(lock) → `recalcAdaptiveWindow`(lock) na tym samym `std::shared_mutex` | Dodane `recalcAdaptiveWindowLocked()` wewnętrzne, nie-lockujące |
| Korelacje puste mimo 10+ obserwacji | Dzielenie całkowite (`data[i] / frames.size()`) → średnie = 0 | Poprawione na `float sum = ...; avg[i] = sum / n` |
| Korelacje puste — filtr ID | Ścisłe przecięcie zbiorów ID eliminowało wszystkie ID | Zmiana na próg większościowy (≥50% obserwacji) |
| Candump pusty plik | `QTextStream` nie flushowany przed zamknięciem | Dodany `flush()` co 100 ramek |
| Odtwarzanie offline nie wysyła na CAN | `OfflineAnalyzer` nie auto-startował sniffera | Dodane `m_sniffer->start(m_sniffInterface)` przed `writeFrame` |
| "Wyślij zmodyfikowaną ramkę" nie działa | `FrameDetailWidget` wymagał otwartego sniffera | Auto-open `PCAN_USBBUS1` jeśli sniffer nieaktywny |
| PCAN readFrame: 0 ramek mimo aktywnej magistrali | `PCAN_CHANNEL_OCCUPIED` sprawdzany przed odczytem, ale z błędnym bitem `0x100` | Usunięto status check — czytaj bezpośrednio `CAN_Read`/`CAN_ReadFD` |
| Pusty plik candump | Dzielenie całkowite, brak flush | Poprawiony format, flush co 100 ramek |

## Nowe funkcje UI ✅

| Funkcja | Status |
|---------|--------|
| Przyczynowość Granger | ✅ UI dodane |
| Wykrywanie punktów zmiany | ✅ UI dodane |
| Korelacja z przesunięciem czasowym | ✅ UI dodane |
| Gradient Boosted Trees | ✅ UI dodane |
| Online learning (Welford) | ✅ checkbox |
| EWMA anomaly detection | ✅ checkbox |
| Nagrywanie candump na dysk | ✅ checkbox, auto-start, C:\candump |
| Wysyłanie ramek na CAN w offline analyzer | ✅ checkbox + auto-start sniffera |
| Auto-inkrement filtr bajtów | ✅ dodany do computeCorrelations |

---

## Sesja 2026-05-12: Synchronizacja i dokończenie UI ✅

### Synchronizacja repozytorium
- ✅ Lokalne zmiany schowane do `stash@{0}` (niedokończone UI)
- ✅ `git reset --hard origin/main` + `git clean -fd`
- ✅ Token GitHub zweryfikowany

### Dokończenie nowego UI ✅
- ✅ `AssociativeLearner.h`: 5 deklaracji metod + 13 widgetów
- ✅ `AssociativeLearner.cpp`: ~230 linii UI + implementacje metod
- ✅ Metody podpięte przez istniejący `runAsync` (QtConcurrent)

### Bugfixy ✅
- ✅ `LearningEngine.cpp`: usunięty duplikat `recalcAdaptiveWindow()` (przed-istniejący bug)
- ✅ `test_learningengine.cpp`: poprawiony include, `<random>`, `iterationCount`, `#define private public`

### Rozszerzenie testów ✅
- ✅ `CMakeLists.txt`: dodane `test_learningengine.cpp`, `LearningEngine.cpp`, `GpuCompute.cpp`
- ✅ Oba targety skompilowane (exe + tests), 63 testy uruchomione

### Roadmapa (rekomendacje)
1. **Serializacja UI** — 7 stubów (save/load, export/import, Lua, HTML)
2. **Test hardening** — poprawa testów + rozszerzenie pokrycia
3. **t-SNE (#41)** — Barnes-Hut, GPU, wizualizacja 2D/3D
4. **Dekompozycja LearningEngine.cpp** — 2800 LOC → 6 modułów
5. **CI/CD** — GitHub Actions (Win+Linux), clang-tidy, ASan

---

## Sesja 2026-05-12 (część 4): Test hardening — 65/65 ✅

### Filtr szumu cyklicznego (commit 947e4e9)
- ✅ `detectCyclicNoiseBytes()`, `setNoiseFilterEnabled()` + integracja w 3 metodach
- ✅ UI: czerwony checkbox w AssociativeLearner (domyślnie ✓)
- ✅ +2 testy: CyclicNoiseBytesDetected, NoiseFilterToggle

### Naprawa 4 padających testów DbcParser
- ✅ Bug: `line.trimmed()` + `startsWith(" SG_ ")` → sygnały nigdy nie parsowane
  Fix: `startsWith("SG_ ")` (bez wiodącej spacji)
- ✅ Bug: regex `(\d+)` nie obsługiwał hex ID `0x123`
  Fix: `(0[xX][0-9a-fA-F]+|\d+)` + `toUInt(nullptr, 0)`

### Integracja nowych plików testów (deepseek)
- ✅ `test_canframe.cpp` — 14 testów CanFrame (byteAt, defaults, toString, XL)
- ✅ `test_ringbuffer.cpp` — 12 testów RingBuffer (reserve, push/drain, wrap, SPSC)
  - Fix: błędne oczekiwanie `reserve(3)→7` → `3`
  - Fix: SPSC test `QThread` bez event loop → `std::thread` + `std::chrono`

### Wynik: 65/65 testów ✅ (poprzednio ~59 + crash)

### Zaktualizowana roadmapa
1. ~~Serializacja UI~~ ✅
2. ~~Test hardening~~ ✅ — 65/65
3. **t-SNE (#41)** — Barnes-Hut, GPU, wizualizacja 2D/3D
4. **Dekompozycja LearningEngine.cpp** — 2800 LOC → 6 modułów
5. **CI/CD** — GitHub Actions (Win+Linux), clang-tidy, ASan

---

## Sesja 2026-05-12 (część 5): t-SNE 2D ✅

### Implementacja (commit TBD)
- ✅ `LearningEngine::TsneResult` struct + `runTsne(perplexity, maxIter, theta)` — 140 LOC czystego C++
- ✅ Wejście: `splitWindows` → `buildWindowFeatures` (4D: size, unique IDs, entropy, duration) — spójne z PCA
- ✅ Normalizacja: zero mean, unit std per dimesion
- ✅ Macierz podobieństw P: binary search na σᵢ dla zadanej perplexity (max 50 iter, tol 1e-5)
- ✅ Symetryzacja P + early exaggeration (12× pierwsze 100 iteracji)
- ✅ Gradient descent: momentum 0.5→0.8, adaptive gains (van der Maaten), η=200
- ✅ Re-centering po każdej iteracji
- ✅ N cap = 300 punktów, iteracje cap = 1000
- ✅ k-means++ na wynik 2D → 3 kolory klastrów
- ✅ Końcowa dywergencja KL w status label
- ✅ UI: button + pola perplexity/iteracji + 3-kolorowy scatter chart (OpenGL)
- ✅ Obliczenia przez `runAsync` (nie blokuje UI)
- ✅ 2 nowe testy: `TsneInsufficientData`, `TsneProducesPoints` (137ms)
- ✅ 67/67 testów

### Zaktualizowana roadmapa
1. ~~Serializacja UI~~ ✅
2. ~~Test hardening~~ ✅ — 67/67
3. ~~t-SNE (#41)~~ ✅ — 2D, 67/67 testów
4. ~~Dekompozycja LearningEngine.cpp~~ ✅ — 6 modułów, 67/67 testów
5. **CI/CD** — GitHub Actions (Win+Linux), clang-tidy, ASan

---

## Sesja 2026-05-12 (część 6): Dekompozycja LearningEngine.cpp ✅

### Wynik: 3048 LOC → 6 modułów

| Plik | LOC | Zawartość |
|------|-----|-----------|
| `LearningEngine.cpp` | 313 | Core: frame ingestion, events, observations, features, candidates |
| `LearningEngine_filters.cpp` | 86 | detectAutoIncrementBytes, detectCyclicNoiseBytes |
| `LearningEngine_correlations.cpp` | 734 | Pearson, cross-byte, sequence, cross-var, MutInfo, MIC, FFT, Markov |
| `LearningEngine_models.cpp` | 926 | Neural net, GBT, CI bootstrap, multi-target, Granger, change-point, adaptive window |
| `LearningEngine_clustering.cpp` | 751 | DBSCAN, k-means++, PCA, t-SNE, k-d tree, Welford online |
| `LearningEngine_persistence.cpp` | 272 | JSON serialize/deserialize, checkpoint, ONNX stub, EWMA |

- ✅ Wszystkie moduły zarejestrowane w CMakeLists.txt (SOURCES + TEST_SOURCES)
- ✅ 67/67 testów po dekompozycji
- ✅ Commit: `c296030` — pushed to main

---

## Sesja 2026-05-12 (część 2): Serializacja UI ✅

### Implementacja 7 metod (były stuby)
- ✅ `autoSave()`: zapis `m_engine.serializeSession()` → JSON na dysk (`autosave_learner.json`)
- ✅ `saveSession()`: QFileDialog → zapis JSON, aktualizacja ścieżki auto-zapisu
- ✅ `loadSession()`: QFileDialog → odczyt JSON → `m_engine.deserializeSession()` → odświeżenie wszystkich tabel, combo, wykresów
- ✅ `exportModels()`: eksport modeli liniowych (a, b per ID/bajt/zmienna) do strukturalnego JSON
- ✅ `importModels()`: wczytanie JSON z modelami + info (modele read-only — retraining wymagany)
- ✅ `generateLuaScript()`: generacja kompletnego skryptu Lua z istotnymi ID (p<0.05) i modelami predykcyjnymi
- ✅ `exportHtmlReport()`: raport HTML z ciemnym motywem, tabelami korelacji, modeli, statystykami

### Dodane includy
- ✅ `#include <QFile>`, `#include <QTextStream>`, `#include <QDateTime>`

### Kompilacja
- ✅ `MagistralaCAN4.exe` — OK
- ✅ 63 testy — te same 4 nieprzechodzące (przed-istniejące)

### Zaktualizowana roadmapa
1. ~~Serializacja UI~~ ✅
2. **Test hardening** — poprawa 4 padających testów + rozszerzenie pokrycia
3. **t-SNE (#41)** — Barnes-Hut, GPU, wizualizacja 2D/3D
4. **Dekompozycja LearningEngine.cpp** — 2800 LOC → 6 modułów
5. **CI/CD** — GitHub Actions (Win+Linux), clang-tidy, ASan

---

## Sesja 2026-05-12 (część 3): Filtr zaszumienia cyklicznego ✅

### Implementacja
- ✅ `LearningEngine::detectCyclicNoiseBytes()` — wykrywanie bajtów gdzie dowolny bit toggleuje >40% par ramek
- ✅ `setNoiseFilterEnabled(bool)` / `noiseFilterEnabled()` — włączanie/wyłączanie filtru
- ✅ `bool m_noiseFilterEnabled = true` — domyślnie włączony
- ✅ Integracja w `computeCorrelations()` — noisy bytes pomijane przy Pearsonie
- ✅ Integracja w `computeCorrelationsOnline()` — noisy bytes pomijane w Welford
- ✅ Integracja w `computeCrossByte()` — noisy bytes pomijane w cross-byte
- ✅ `AssociativeLearner`: checkbox "Filtr zaszumienia cyklicznego (bity 0↔1)", domyślnie ✓
- ✅ 2 nowe testy: `CyclicNoiseBytesDetected`, `NoiseFilterToggle` → 17/17 OK

### Algorytm
Dla każdego bajtu każdego CAN ID: zlicz ile razy każdy bit (0-7) zmienia wartość
w kolejnych parach ramek. Jeśli jakikolwiek bit zmienia się w >40% par → bajt = szum cykliczny.

---

## Sesja 2026-05-12 (część 7): Filtry dla całego uczenia + checkbox auto-incr ✅

### Zakres zmian
- ✅ `m_autoIncrFilterEnabled = true` + getter/setter w `LearningEngine.h`
- ✅ Oba filtry (auto-incr + noise) działają we **wszystkich 8 metodach** uczenia:
  `computeCorrelations`, `computeCrossByte`, `computeCorrelationsOnline`,
  `computeMutualInformation`, `computeMIC`, `computeCrossCorrelationLag`,
  `computeGrangerCausality`, `trainPrediction`
- ✅ UI: pomarańczowy checkbox "Filtr autoinkrementacji (liczniki, timestampy)", domyślnie ✓
- ✅ Testy Granger/CrossCorrelationLag: `setAutoIncrFilterEnabled(false)` + `setNoiseFilterEnabled(false)` — testy sprawdzają algorytm, nie filtry
- ✅ 67/67 testów, commit `15aa24e`

### Infrastruktura
- ✅ `build_native/MagistralaCAN4.exe` śledzony przez Git LFS (`*.exe filter=lfs`)
- ✅ Push 124 MB przez LFS na GitHub

---

## Sesja 2026-05-12 (część 8): Roadmapa dalszej modernizacji

### Plan (priorytet malejący)
1. ~~ASan + code coverage w CI~~ ✅ — 5 jobów CI, lcov HTML artifact, Codecov upload
2. ~~ISO 15765 (CAN TP)~~ ✅ — `CanTpLayer`: SF/FF/CF/FC, reassembly do 4095B, 12 testów, 79/79
3. ~~Filtr szumu domyślnie wyłączony~~ ✅ — checkbox opt-in, `m_noiseFilterEnabled = false`
4. ~~Isolation Forest~~ ✅ — `trainIsolationForest` + `scoreIsolationForest`, 4 testy, 83/83
5.  ~~SOME/IP parser~~ ✅ — `SomeIpParser` + `SomeIpWidget`, 16 testów, 99/99
6.  ~~Signal Plotter~~ ✅ — `SignalPlotterWidget`, 4 kanały, DBC decode, rolling window, PNG export
7.  ~~DoIP parser~~ ✅ — `DoIpParser` + `DoIpWidget`, ISO 13400-2, 17 testów, 116/116
8.  ~~DBC Auto-Generator~~ ✅ — `DbcAutoGenerator` reverse-inżynieruje sygnały z ruchu CAN
9.  **Bus Load Analyzer** — % wykorzystania pasma z wykresem per-ID
10. **SQLite dla obserwacji** — persistencja milionów obserwacji, zapytania historyczne

---

## Sesja 2026-05-12 (część 11): Signal Plotter Widget ✅

### Implementacja (commit a89bcdf)
- ✅ `SignalPlotterWidget.h/cpp` — Qt Charts widget z 4 kanałami, rolling time window
- ✅ `QSplitter`: drzewo sygnałów DBC (lewo) + wykres liniowy (prawo)
- ✅ `processFrame()` → `DbcParser::decodeSignals()` → append do `QLineSeries`
- ✅ 4 kanały (czerwony/niebieski/zielony/pomarańczowy), przycisk ✕ per kanał
- ✅ Rolling window: 5/10/30/60/120 s (konfigurowalne)
- ✅ Tryb normalizacji [0–1] per kanał (checkbox)
- ✅ Auto-skalowanie osi Y, eksport PNG
- ✅ Podpięty do wszystkich 3 ścieżek ładowania DBC (dialog, MRU, edytor)
- ✅ Throttlowany (co N-tą ramkę) — jak inne widgety diagnostyczne
- ✅ Nowa zakładka "Przebiegi sygnałów" w MainWindow
- ✅ 99/99 testów bez regresji

---

## Sesja 2026-05-12 (część 10): SOME/IP parser ✅

### Implementacja (commit 1818562)
- ✅ `SomeIpFrame.h` — structs: `SomeIpHeader`, `SomeIpSdEntry`, `SomeIpMessage`, stałe (SD=0xFFFF/0x8100, Magic=0xDEAD)
- ✅ `SomeIpParser.h/cpp` — Qt-free, testable headless:
  - `parse(vector/pointer+len)` — 16-byte header decode + payload extraction
  - `parseSdPayload()` — SOME/IP-SD entries array (type, serviceId, instanceId, majorVersion, TTL, minorVersion)
  - `messageTypeName()` / `returnCodeName()` / `sdEntryTypeName()` — human-readable enum strings
  - `isServiceDiscovery()` / `isMagicCookie()` — classification helpers
- ✅ `SomeIpWidget.h/cpp` — Qt UI:
  - Hex input field z parserem na kliknięcie lub Enter
  - 8-kolumnowa tabela (ServiceID, MethodID, Typ, ClientID, SessionID, ReturnCode, Bajty, Opis)
  - Detail pane (ostry preformat z pełnym nagłówkiem + payload hex dump lub SD entries)
  - `processPayload(QByteArray)` — publiczny slot dla integracji z CanTpLayer
- ✅ `tests/test_someipparser.cpp` — 16 testów: basic parse, payload, truncated, invalid length, SD empty, SD 1 entry, SD 2 entries, magic cookie, name helpers, raw pointer API
- ✅ Nowa zakładka "SOME/IP" w MainWindow
- ✅ 99/99 testów po integracji

### Architektura
- `SomeIpParser` jest czystym C++ (bez Qt) — identycznie jak `CanTpLayer`
- `SomeIpWidget` używa `processPayload(QByteArray)` — można podpiąć do wyjścia `CanTpLayer::m_onMessage`
- SOME/IP-SD entries: 16 bajtów każdy, parsowane z entries array length field

---

## Sesja 2026-05-12 (część 12): Bus Load Analyzer + Frame Sender ✅

### Bus Load Analyzer (commit 09bfcce)
- ✅ `BusLoadAnalyzer.h/cpp` — czysty C++, sliding-window bandwidth utilization
  - `frameBits()`: Classic 11-bit = 44+8*dlc, Extended 29-bit = 64+8*dlc, CAN FD ≈ 60/80+8*dlc
  - `processFrame()` → `pruneOld()` — sliding window via `std::deque<Record>`
  - `currentLoad()` = windowBits / (windowSec * baudRate)  [0.0, 1.0]
  - `peakLoad()`, `framesPerSec()`, `uniqueIdCount()`
  - `topLoaders(n)` — per-ID load sorted by loadFraction descending
- ✅ `CanBusLoadWidget.h/cpp` — Qt6 UI:
  - Load bar z kolorem (zielony<50% / pomarańczowy<80% / czerwony≥80%)
  - Rolling 60s chart (QLineSeries + QValueAxis, Qt6 Charts bez namespace QtCharts)
  - Top-10 ID table z per-ID load%, FPS, bitami
  - Konfiguracja: baud rate (125k/250k/500k/1M), okno [0.1–10s], próg alertu [%]
  - Timer 250ms tick → refresh wszystkich wskaźników
- ✅ `tests/test_busloadanalyzer.cpp` — 21 testów (frameBits, sliding window, pruning, topLoaders, reset, guards)
- ✅ Podpięty do `frameProcessedThrottled` w MainWindow
- ✅ Nowa zakładka "Obciążenie magistrali"
- ✅ 159/159 testów po integracji

### Manual Frame Sender (commit TBD)
- ✅ `CanFrameSenderWidget.h/cpp` — nadajnik ramek CAN:
  - Hex ID input + Extended/FD checkbox + DLC spinner + 8 byte hex edits
  - Load from DBC combo (wybór wiadomości z załadowanego pliku DBC)
  - Send Once + Periodic (konfigurowalne ms) + Stop
  - Historia 50 ostatnich wysłanych ramek z powrotem do edytora (klik w wiersz)
- ✅ Podpięty do `CanSniffer::writeFrame()` bezpośrednio
- ✅ setDbcParser() wołane we wszystkich 3 ścieżkach ładowania DBC
- ✅ Nowa zakładka "Nadajnik ramek"

---

## Sesja 2026-05-12 (część 13): Protokoły diagnostyczne + nowe narzędzia ✅

### Test count: 192 → 294 (+102 testów, 20 test suites)

### CAN Gateway (commit 5a0ca16)
- ✅ `CanGateway.h/cpp` — mostkowanie między dwoma ICanDriver: PassAll / Whitelist / Blacklist
- ✅ `CanGatewayWidget.h/cpp` — UI z edytorem reguł remappingu ID
- ✅ 8 testów: PassAll, Whitelist, Blacklist, Remap, ResetStats, NotRunning, ClearRules, NullSink
- ✅ Bug naprawiony: Blacklist tryb (switch zamiast boolean OR)

### UDS Sequence Runner (commit 483f09f)
- ✅ `UdsSequenceRunner.h/cpp` — automat stanów: kroki UDS, retry, timeout, positive/negative response
- ✅ `UdsSequenceWidget.h/cpp` — 3 wbudowane szablony (ECU ID, DTC read/clear, Security Access)
- ✅ 6 testów: SingleStepPass, NegativeResponseAborts, WrongRxIdIgnored, AbortMarksRemaining, EmptySteps, MultiStepAllPass

### Wireshark-style Filter Expression (commit f2d6f21)
- ✅ `CanFilterExpr.h/cpp` — recursive descent parser, AST, pola: can.id/dlc/data[N]/ext/fd/err/ts
- ✅ Integracja w `CanFilterProxy` — wyrażenie zastępuje filtr tekstowy
- ✅ 19 testów: wszystkie operatory, AND/OR/NOT, nawiasy, pola boolowskie, błędy

### LIN Bus Parser (commit d28d5e2)
- ✅ `LinFrame.h` + `LinParser.h/cpp` — ISO 17987, PID (P0/P1 parity), Classic/Enhanced checksum
- ✅ `LinWidget.h/cpp` — tabela ramek z walidacją PID i checksum
- ✅ 16 testów: computePid, pidValid, oba typy checksum, parse z/bez sync, ClassicFallback, feed, TooShort

### Per-ID Statistics Profiler (commit 278216c)
- ✅ `CanIdStats.h/cpp` — per-ID: frame count, intervals (min/max/avg), byte stats (min/max/avg per pos), CSV export
- ✅ `CanIdStatsWidget.h/cpp` — sortowalna tabela z filtrem i eksportem CSV
- ✅ 17 testów: akumulacja, byte min/max/avg, maxDlc, interwały, freq Hz, timestampy, reset, sort, CSV

### KWP2000 / ISO 14230 Parser (commit 8ea8c1b)
- ✅ `Kwp2000Parser.h/cpp` — request / positive response / negative response, LRC (XOR), NRC decoder
- ✅ `Kwp2000Widget.h/cpp` — kolorowe wiersze (zielony/czerwony), NRC opisy
- ✅ 17 testów: request/response/negative parsing, LRC, serviceNames, NRC descriptions, isRequestSid

### XCP/CCP Calibration Protocol Parser (commit 64502fa)
- ✅ `XcpParser.h/cpp` — ASAM MCD-1 XCP, komendy (CONNECT/DOWNLOAD/SET_MTA/DAQ/PGM), error codes
- ✅ `XcpWidget.h/cpp` — decode z wyborem kanału (CMD/RESP/Auto)
- ✅ 15 testów: komendy, positive/negative response, event/service packets, isCommand, error descriptions
- ✅ Uwaga architektoniczna: PID 0xFF/0xFE/0xFD/0xFC są jednocześnie komendami i markerami response

### CAN Replay Filter / Transformer (commit 2eee779)
- ✅ `CanReplayFilter.h/cpp` — reguły per-ID: drop, remap ID, byte transform (scale/offset, clamp 0-255)
- ✅ `CanReplayFilterWidget.h/cpp` — edytor reguł + tabela podglądu
- ✅ 13 testów: passthrough, drop, remap, scale, offset, clamp over/underflow, inactive bytes, batch, first-match, clearRules

### Live DBC Signal Monitor (commit 05678b9)
- ✅ `CanSignalMonitor.h/cpp` — śledzi bieżące wartości fizyczne wszystkich sygnałów DBC, alarmy, stale detection
- ✅ `CanSignalMonitorWidget.h/cpp` — odświeżana tabela 250ms, filtr nazwy, konfigurowalny próg stale
- ✅ Podpięty do wszystkich 3 ścieżek ładowania DBC + frameProcessedThrottled
- ✅ 13 testów: NoDbc, UnknownId, single/multi update, updateCount, latestValue, valueFor, alarm thresholds, staleDetection, reset

### Periodic CAN Frame Scheduler (commit 06b0405)
- ✅ `CanPeriodicSender.h/cpp` — wiele ramek na niezależnych timerach, tick 10ms, per-entry enable/disable
- ✅ `CanPeriodicSenderWidget.h/cpp` — tabela harmonogramu, add/remove, Start/Stop, live TX count
- ✅ 11 testów: empty state, add/remove/update/enable entries, clearAll, running state, out-of-range safety

### Nowe zakładki w MainWindow
| Zakładka | Klasa | Opis |
|----------|-------|------|
| CAN Gateway | CanGatewayWidget | Mostkowanie interfejsów z filtrowaniem |
| Sekwencje UDS | UdsSequenceWidget | Automatyczne sekwencje diagnostyczne |
| LIN Bus | LinWidget | Dekoder protokołu LIN |
| ID Statistics | CanIdStatsWidget | Statystyki per-ID z CSV export |
| KWP2000 | Kwp2000Widget | Dekoder ISO 14230 diagnostics |
| XCP | XcpWidget | Dekoder protokołu kalibracyjnego XCP |
| Replay Filter | CanReplayFilterWidget | Transformacje ramek podczas odtwarzania |
| Live Signals | CanSignalMonitorWidget | Live dashboard sygnałów DBC |
| Periodic Sender | CanPeriodicSenderWidget | Harmonogram cyklicznych ramek |

### Łączny stan projektu (2026-05-12)
- **Testy**: 294/294 w 20 suites
- **Protokoły**: CAN, CAN FD, LIN, J1939, UDS, KWP2000, XCP, SOME/IP, DoIP, CANopen, OBD-II
- **Narzędzia**: Bus Load, Frame Sender, Periodic Sender, CAN Gateway, UDS Sequences, Replay Filter, Signal Monitor
- **ML**: LearningEngine z 37 algorytmami, t-SNE, Isolation Forest, GBT, EWMA
- **Infrastruktura**: REST API, MQTT, WebSocket, MDF4, zstd, Lua scripting, plugin system

### Roadmapa (następna sesja — priorytety)
1. **SQLite dla obserwacji** — persistencja milionów obserwacji, zapytania historyczne
2. **CI/CD GitHub Actions** — Win+Linux build, testy, lcov coverage
3. **Protocol sequence diagram** — wizualna oś czasu wymiany komunikatów (MSC diagram)
4. **CAN FD extended stats** — Bit Timing, TDC, ESI tracking
5. **AUTOSAR ARXML import** — sygnały z ARXML bez DBC (Vehicle Network Designer format)
