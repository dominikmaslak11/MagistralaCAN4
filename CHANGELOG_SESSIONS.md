# MagistralaCAN4 — Historia sesji deweloperskich

Pełne logi wszystkich sesji w porządku chronologicznym.
Aktualny stan projektu i roadmapa → `SESSION_PROMPT.md`.

---

## Fazy wstępne (przed 2026-05-11)

### Faza 1: Pipeline "Ruch CAN" ✅
- ✅ #1–#10: QSortFilterProxyModel, cache DisplayRole, wirtualne scrollowanie, batch'owanie, throttle, heatmap, presety

### Faza 2: Stabilność i infrastruktura ✅
- ✅ #11–#23: QSettings, async I/O, testy, MDF4Writer, backoff, async export, MRU, zstd, undo/redo, plugin protection, CanPlayer

### Faza 3: Modernizacja uczenia asocjacyjnego ✅

#### Faza A: Dekompozycja silnika ✅
- ✅ #24 `LearningEngine` — czysty C++, STL, shared_mutex, 37 algorytmów ML
- ✅ #25 `AssociativeLearner` → cienka warstwa UI (2836→1372 LOC, -52%)

#### Faza B: Online / incremental learning ✅
- ✅ #26 Ring buffer dla obserwacji (max 10k)
- ✅ #27 Exponential forgetting (e^(-λ·Δt), ważona korelacja Pearsona)
- ✅ #28 Korelacje online (Welford's algorithm)
- ✅ #29 Anomalie EWMA + adaptive threshold

#### Faza C: Unowocześnienie modeli predykcyjnych ✅
- ✅ #30 MLP v2 — Adam optimizer + L2 regularization + early stopping
- ✅ #31 Gradient Boosted Trees (XGBoost-lite)
- ✅ #32 Przedziały ufności (bootstrap residuals, 95% CI)
- ✅ #33 Multi-target (trainAllPredictions, predictRealtimeAll)

#### Faza D: Wydajność i skalowalność ✅
- ✅ #34 Sparse cross-byte correlation (pomijanie bajtów o zerowej wariancji)
- ✅ #35 DBSCAN z k-d tree (5D, O(N log N))
- ✅ #36 k-means++ initialization
- ✅ #37 PCA: Jacobi eigen-decomposition dla macierzy 5×5

#### Faza E: Zaawansowana analiza ✅
- ✅ #38 Granger causality (F-test, OLS, Gaussian elimination)
- ✅ #39 Change-point detection (Binary Segmentation)
- ✅ #40 Cross-correlation z przesunięciem czasowym (lag [-10,+10])
- ⏭ #41 t-SNE — pominięte (opcjonalne, GPU) → zaimplementowane później

#### Faza F: Trwałość i odtwarzalność ✅
- ✅ #42–45 JSON serializacja, checkpoint save/load, ONNX stub, session resume

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
- Zapis do pliku `candump_YYYYMMDD_HHMMSS.log` (Windows: `C:\candump\`, Linux: konfigurowalny)
- Automatyczny start/stop przy sniffingu

### Diagnostyka CAN ✅
- Status bar pokazuje backend drivera i baud rate
- No-data timeout (5s) z żółtym ostrzeżeniem
- Licznik ramek, logowanie błędów PCAN

---

## Sesja 2026-05-11: Bugfixy PCAN + korelacje

### Naprawione bugi PCAN / SocketCAN
- `PcanDriver::readFrame` — usunięty martwy warunek `CAN_GetStatus & 0x100`
- Deadlock w `markEvent` → `recalcAdaptiveWindow` (reentrant mutex)
- Integer division w `addObservation` (`uint8_t / size_t` → 0)
- Korelacje: majority threshold zamiast strict intersection
- readFrame: classic CAN przed FD (FD = ILLOPERATION na PCAN-USB)

### Naprawione bugi (tabela)
| Bug | Przyczyna | Rozwiązanie |
|-----|-----------|-------------|
| Crash przy "Zarejestruj zdarzenie" | Deadlock: `markEvent`(lock) → `recalcAdaptiveWindow`(lock) na tym samym `std::shared_mutex` | Dodane `recalcAdaptiveWindowLocked()` wewnętrzne, nie-lockujące |
| Korelacje puste mimo 10+ obserwacji | Dzielenie całkowite (`data[i] / frames.size()`) → średnie = 0 | Poprawione na `float sum = ...; avg[i] = sum / n` |
| Korelacje puste — filtr ID | Ścisłe przecięcie zbiorów ID eliminowało wszystkie ID | Zmiana na próg większościowy (≥50% obserwacji) |
| Candump pusty plik | `QTextStream` nie flushowany przed zamknięciem | Dodany `flush()` co 100 ramek |
| Odtwarzanie offline nie wysyła na CAN | `OfflineAnalyzer` nie auto-startował sniffera | Dodane `m_sniffer->start(m_sniffInterface)` przed `writeFrame` |
| "Wyślij zmodyfikowaną ramkę" nie działa | `FrameDetailWidget` wymagał otwartego sniffera | Auto-open `PCAN_USBBUS1` jeśli sniffer nieaktywny |
| PCAN readFrame: 0 ramek mimo aktywnej magistrali | `PCAN_CHANNEL_OCCUPIED` sprawdzany przed odczytem z błędnym bitem `0x100` | Usunięto status check — czytaj bezpośrednio `CAN_Read`/`CAN_ReadFD` |
| Pusty plik candump | Dzielenie całkowite, brak flush | Poprawiony format, flush co 100 ramek |

### Nowe funkcje UI ✅
| Funkcja | Status |
|---------|--------|
| Przyczynowość Granger | ✅ UI dodane |
| Wykrywanie punktów zmiany | ✅ UI dodane |
| Korelacja z przesunięciem czasowym | ✅ UI dodane |
| Gradient Boosted Trees | ✅ UI dodane |
| Online learning (Welford) | ✅ checkbox |
| EWMA anomaly detection | ✅ checkbox |
| Nagrywanie candump na dysk | ✅ checkbox, auto-start |
| Wysyłanie ramek na CAN w offline analyzer | ✅ checkbox + auto-start sniffera |
| Auto-inkrement filtr bajtów | ✅ dodany do computeCorrelations |

---

## Sesja 2026-05-12, część 1: Synchronizacja i dokończenie UI ✅

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

---

## Sesja 2026-05-12, część 2: Serializacja UI ✅

### Implementacja 7 metod (były stuby)
- ✅ `autoSave()`: zapis `m_engine.serializeSession()` → JSON na dysk (`autosave_learner.json`)
- ✅ `saveSession()`: QFileDialog → zapis JSON, aktualizacja ścieżki auto-zapisu
- ✅ `loadSession()`: QFileDialog → odczyt JSON → `m_engine.deserializeSession()` → odświeżenie wszystkich tabel, combo, wykresów
- ✅ `exportModels()`: eksport modeli liniowych (a, b per ID/bajt/zmienna) do strukturalnego JSON
- ✅ `importModels()`: wczytanie JSON z modelami + info (modele read-only — retraining wymagany)
- ✅ `generateLuaScript()`: generacja kompletnego skryptu Lua z istotnymi ID (p<0.05) i modelami predykcyjnymi
- ✅ `exportHtmlReport()`: raport HTML z ciemnym motywem, tabelami korelacji, modeli, statystykami
- ✅ Dodane includy: `<QFile>`, `<QTextStream>`, `<QDateTime>`
- ✅ Kompilacja: `MagistralaCAN4.exe` OK, 63 testy

---

## Sesja 2026-05-12, część 3: Filtr zaszumienia cyklicznego ✅

### Implementacja
- ✅ `LearningEngine::detectCyclicNoiseBytes()` — wykrywanie bajtów gdzie dowolny bit toggleuje >40% par ramek
- ✅ `setNoiseFilterEnabled(bool)` / `noiseFilterEnabled()` — włączanie/wyłączanie filtru
- ✅ `bool m_noiseFilterEnabled = true` — domyślnie włączony
- ✅ Integracja w `computeCorrelations()` — noisy bytes pomijane przy Pearsonie
- ✅ Integracja w `computeCorrelationsOnline()` — noisy bytes pomijane w Welford
- ✅ Integracja w `computeCrossByte()` — noisy bytes pomijane w cross-byte
- ✅ `AssociativeLearner`: checkbox "Filtr zaszumienia cyklicznego (bity 0↔1)", domyślnie ✓
- ✅ +2 testy: `CyclicNoiseBytesDetected`, `NoiseFilterToggle`

### Algorytm
Dla każdego bajtu każdego CAN ID: zlicz ile razy każdy bit (0-7) zmienia wartość
w kolejnych parach ramek. Jeśli jakikolwiek bit zmienia się w >40% par → bajt = szum cykliczny.

---

## Sesja 2026-05-12, część 4: Test hardening — 65/65 ✅

### Naprawa 4 padających testów DbcParser
- ✅ Bug: `line.trimmed()` + `startsWith(" SG_ ")` → sygnały nigdy nie parsowane
  Fix: `startsWith("SG_ ")` (bez wiodącej spacji)
- ✅ Bug: regex `(\d+)` nie obsługiwał hex ID `0x123`
  Fix: `(0[xX][0-9a-fA-F]+|\d+)` + `toUInt(nullptr, 0)`

### Integracja nowych plików testów
- ✅ `test_canframe.cpp` — 14 testów CanFrame (byteAt, defaults, toString, XL)
- ✅ `test_ringbuffer.cpp` — 12 testów RingBuffer (reserve, push/drain, wrap, SPSC)
  - Fix: błędne oczekiwanie `reserve(3)→7` → `3`
  - Fix: SPSC test `QThread` bez event loop → `std::thread` + `std::chrono`

**Wynik: 65/65 testów ✅** (poprzednio ~59 + crash)

---

## Sesja 2026-05-12, część 5: t-SNE 2D ✅

### Implementacja
- ✅ `LearningEngine::TsneResult` struct + `runTsne(perplexity, maxIter, theta)` — 140 LOC czystego C++
- ✅ Wejście: `splitWindows` → `buildWindowFeatures` (4D: size, unique IDs, entropy, duration) — spójne z PCA
- ✅ Normalizacja: zero mean, unit std per dimension
- ✅ Macierz podobieństw P: binary search na σᵢ dla zadanej perplexity (max 50 iter, tol 1e-5)
- ✅ Symetryzacja P + early exaggeration (12× pierwsze 100 iteracji)
- ✅ Gradient descent: momentum 0.5→0.8, adaptive gains (van der Maaten), η=200
- ✅ Re-centering po każdej iteracji
- ✅ N cap = 300 punktów, iteracje cap = 1000
- ✅ k-means++ na wynik 2D → 3 kolory klastrów
- ✅ Końcowa dywergencja KL w status label
- ✅ UI: button + pola perplexity/iteracji + 3-kolorowy scatter chart (OpenGL)
- ✅ Obliczenia przez `runAsync` (nie blokuje UI)
- ✅ +2 testy: `TsneInsufficientData`, `TsneProducesPoints` (137ms)

**Wynik: 67/67 testów ✅**

---

## Sesja 2026-05-12, część 6: Dekompozycja LearningEngine.cpp ✅

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
- ✅ Commit: `c296030`

---

## Sesja 2026-05-12, część 7: Filtry dla całego uczenia + checkbox auto-incr ✅

### Zakres zmian
- ✅ `m_autoIncrFilterEnabled = true` + getter/setter w `LearningEngine.h`
- ✅ Oba filtry (auto-incr + noise) działają we **wszystkich 8 metodach** uczenia:
  `computeCorrelations`, `computeCrossByte`, `computeCorrelationsOnline`,
  `computeMutualInformation`, `computeMIC`, `computeCrossCorrelationLag`,
  `computeGrangerCausality`, `trainPrediction`
- ✅ UI: pomarańczowy checkbox "Filtr autoinkrementacji (liczniki, timestampy)", domyślnie ✓
- ✅ Testy Granger/CrossCorrelationLag: `setAutoIncrFilterEnabled(false)` + `setNoiseFilterEnabled(false)`
- ✅ 67/67 testów — commit `15aa24e`

### Infrastruktura
- ✅ `build_native/MagistralaCAN4.exe` śledzony przez Git LFS (`*.exe filter=lfs`)
- ✅ Push 124 MB przez LFS na GitHub

---

## Sesja 2026-05-12, część 8: Roadmapa i CI/CD infrastruktura ✅

### Zrealizowane z planu
1. ✅ ASan + code coverage w CI — 5 jobów CI, lcov HTML artifact, Codecov upload
2. ✅ ISO 15765 (CAN TP) — `CanTpLayer`: SF/FF/CF/FC, reassembly do 4095B, 12 testów (79/79)
3. ✅ Filtr szumu domyślnie wyłączony — `m_noiseFilterEnabled = false` (opt-in)
4. ✅ Isolation Forest — `trainIsolationForest` + `scoreIsolationForest`, 4 testy (83/83)

---

## Sesja 2026-05-12, część 10: SOME/IP parser ✅

### Implementacja (commit `1818562`)
- ✅ `SomeIpFrame.h` — structs: `SomeIpHeader`, `SomeIpSdEntry`, `SomeIpMessage`, stałe (SD=0xFFFF/0x8100, Magic=0xDEAD)
- ✅ `SomeIpParser.h/cpp` — Qt-free, testable headless:
  - `parse(vector/pointer+len)` — 16-byte header decode + payload extraction
  - `parseSdPayload()` — SOME/IP-SD entries array (type, serviceId, instanceId, majorVersion, TTL, minorVersion)
  - `messageTypeName()` / `returnCodeName()` / `sdEntryTypeName()` — human-readable enum strings
  - `isServiceDiscovery()` / `isMagicCookie()` — classification helpers
- ✅ `SomeIpWidget.h/cpp` — Qt UI: hex input, 8-kolumnowa tabela, detail pane
- ✅ `tests/test_someipparser.cpp` — 16 testów
- ✅ Nowa zakładka "SOME/IP" w MainWindow

**Wynik: 99/99 testów ✅**

---

## Sesja 2026-05-12, część 11: Signal Plotter Widget ✅

### Implementacja (commit `a89bcdf`)
- ✅ `SignalPlotterWidget.h/cpp` — Qt Charts widget z 4 kanałami, rolling time window
- ✅ `QSplitter`: drzewo sygnałów DBC (lewo) + wykres liniowy (prawo)
- ✅ `processFrame()` → `DbcParser::decodeSignals()` → append do `QLineSeries`
- ✅ 4 kanały (czerwony/niebieski/zielony/pomarańczowy), przycisk ✕ per kanał
- ✅ Rolling window: 5/10/30/60/120 s (konfigurowalne)
- ✅ Tryb normalizacji [0–1] per kanał (checkbox)
- ✅ Auto-skalowanie osi Y, eksport PNG
- ✅ Podpięty do wszystkich 3 ścieżek ładowania DBC
- ✅ Throttlowany (co N-tą ramkę)
- ✅ Nowa zakładka "Przebiegi sygnałów" w MainWindow

**Wynik: 99/99 testów ✅**

---

## Sesja 2026-05-12, część 12: Bus Load Analyzer + Frame Sender ✅

### Bus Load Analyzer (commit `09bfcce`)
- ✅ `BusLoadAnalyzer.h/cpp` — czysty C++, sliding-window bandwidth utilization
  - `frameBits()`: Classic 11-bit = 44+8*dlc, Extended 29-bit = 64+8*dlc, CAN FD ≈ 60/80+8*dlc
  - `processFrame()` → `pruneOld()` — sliding window via `std::deque<Record>`
  - `currentLoad()` = windowBits / (windowSec * baudRate)  [0.0, 1.0]
  - `peakLoad()`, `framesPerSec()`, `uniqueIdCount()`
  - `topLoaders(n)` — per-ID load sorted by loadFraction descending
- ✅ `CanBusLoadWidget.h/cpp` — load bar z kolorem, rolling 60s chart, Top-10 ID table
- ✅ Konfiguracja: baud rate (125k/250k/500k/1M), okno [0.1–10s], próg alertu [%]
- ✅ 21 testów, podpięty do `frameProcessedThrottled`
- ✅ Nowa zakładka "Obciążenie magistrali"

### Manual Frame Sender
- ✅ `CanFrameSenderWidget.h/cpp` — hex ID, Extended/FD checkbox, DLC spinner, 8 byte hex edits
- ✅ Load from DBC combo, Send Once, Periodic (konfigurowalne ms), Stop
- ✅ Historia 50 ostatnich wysłanych ramek
- ✅ Podpięty do `CanSniffer::writeFrame()`, nowa zakładka "Nadajnik ramek"

**Wynik: 159/159 testów ✅**

---

## Sesja 2026-05-12, część 13: Protokoły diagnostyczne + nowe narzędzia ✅

### Testy: 192 → 294 (+102 testów, 20 test suites)

### CAN Gateway (commit `5a0ca16`)
- ✅ `CanGateway.h/cpp` — mostkowanie między dwoma ICanDriver: PassAll / Whitelist / Blacklist
- ✅ `CanGatewayWidget.h/cpp` — UI z edytorem reguł remappingu ID
- ✅ 8 testów; bug naprawiony: Blacklist tryb (switch zamiast boolean OR)

### UDS Sequence Runner (commit `483f09f`)
- ✅ `UdsSequenceRunner.h/cpp` — automat stanów: kroki UDS, retry, timeout, positive/negative response
- ✅ `UdsSequenceWidget.h/cpp` — 3 wbudowane szablony (ECU ID, DTC read/clear, Security Access)
- ✅ 6 testów

### Wireshark-style Filter Expression (commit `f2d6f21`)
- ✅ `CanFilterExpr.h/cpp` — recursive descent parser, AST, pola: can.id/dlc/data[N]/ext/fd/err/ts
- ✅ Integracja w `CanFilterProxy` — wyrażenie zastępuje filtr tekstowy
- ✅ 19 testów: wszystkie operatory, AND/OR/NOT, nawiasy, pola boolowskie, błędy

### LIN Bus Parser (commit `d28d5e2`)
- ✅ `LinFrame.h` + `LinParser.h/cpp` — ISO 17987, PID (P0/P1 parity), Classic/Enhanced checksum
- ✅ `LinWidget.h/cpp` — tabela ramek z walidacją PID i checksum
- ✅ 16 testów

### Per-ID Statistics Profiler (commit `278216c`)
- ✅ `CanIdStats.h/cpp` — per-ID: frame count, intervals (min/max/avg), byte stats, CSV export
- ✅ `CanIdStatsWidget.h/cpp` — sortowalna tabela z filtrem i eksportem CSV
- ✅ 17 testów

### KWP2000 / ISO 14230 Parser (commit `8ea8c1b`)
- ✅ `Kwp2000Parser.h/cpp` — request / positive response / negative response, LRC (XOR), NRC decoder
- ✅ `Kwp2000Widget.h/cpp` — kolorowe wiersze (zielony/czerwony), NRC opisy
- ✅ 17 testów

### XCP/CCP Calibration Protocol Parser (commit `64502fa`)
- ✅ `XcpParser.h/cpp` — ASAM MCD-1 XCP, komendy (CONNECT/DOWNLOAD/SET_MTA/DAQ/PGM), error codes
- ✅ `XcpWidget.h/cpp` — decode z wyborem kanału (CMD/RESP/Auto)
- ✅ 15 testów; uwaga: PID 0xFF/0xFE/0xFD/0xFC są jednocześnie komendami i markerami response

### CAN Replay Filter / Transformer (commit `2eee779`)
- ✅ `CanReplayFilter.h/cpp` — reguły per-ID: drop, remap ID, byte transform (scale/offset, clamp 0-255)
- ✅ `CanReplayFilterWidget.h/cpp` — edytor reguł + tabela podglądu
- ✅ 13 testów

### Live DBC Signal Monitor (commit `05678b9`)
- ✅ `CanSignalMonitor.h/cpp` — śledzi bieżące wartości fizyczne wszystkich sygnałów DBC, alarmy, stale detection
- ✅ `CanSignalMonitorWidget.h/cpp` — odświeżana tabela 250ms, filtr nazwy, konfigurowalny próg stale
- ✅ Podpięty do wszystkich 3 ścieżek ładowania DBC + `frameProcessedThrottled`
- ✅ 13 testów

### Periodic CAN Frame Scheduler (commit `06b0405`)
- ✅ `CanPeriodicSender.h/cpp` — wiele ramek na niezależnych timerach, tick 10ms, per-entry enable/disable
- ✅ `CanPeriodicSenderWidget.h/cpp` — tabela harmonogramu, add/remove, Start/Stop, live TX count
- ✅ 11 testów

### Nowe zakładki w MainWindow
| Zakładka | Klasa |
|----------|-------|
| CAN Gateway | CanGatewayWidget |
| Sekwencje UDS | UdsSequenceWidget |
| LIN Bus | LinWidget |
| ID Statistics | CanIdStatsWidget |
| KWP2000 | Kwp2000Widget |
| XCP | XcpWidget |
| Replay Filter | CanReplayFilterWidget |
| Live Signals | CanSignalMonitorWidget |
| Periodic Sender | CanPeriodicSenderWidget |

**Wynik: 294/294 w 20 suites ✅**

---

## Sesja 2026-05-13, część 1: SQLite Frame DB + CI/CD fix ✅

### Testy: 294 → 307 (+13 testów, 21 test suites) | Commit: `27fb969`

### CI/CD — naprawione
**Problem:** `CMakeLists.txt` hardkodował `C:/msys64/ucrt64/qt6-static` i flagi `-static` zawsze,
niezależnie od środowiska. CI (GitHub Actions) używa dynamicznego Qt6 z `/ucrt64`.

**Rozwiązania:**
- ✅ `CMakeLists.txt`: prefiks statycznego Qt6 otoczony `if(NOT DEFINED ENV{CI} AND EXISTS "C:/msys64/ucrt64/qt6-static")`
- ✅ Flagi `-static-libgcc -static-libstdc++ -static` otoczone `if(NOT DEFINED ENV{CI})`
- ✅ Dodano `Qt6::Sql` do `find_package` + `target_link_libraries` dla obu targetów
- ✅ `build.yml`: dodano `-DCMAKE_PREFIX_PATH=/ucrt64`, `qt6-sql`, `QT_QPA_PLATFORM=offscreen`
- ✅ `ci.yml`: poprawiono prefiks `/ucrt64/qt6-static` → `/ucrt64`, dodano `qt6-sql`

### CanObservationDb — SQLite-backed frame storage ✅
- **Schema**: `sessions` (id, label, started, ended) + `frames` (session_id, ts_us, can_id, extended, dlc, data BLOB)
- **Indeksy**: `idx_frames_can_id`, `idx_frames_session`, `idx_frames_ts`
- **WAL mode** + `PRAGMA synchronous=NORMAL` — szybkie zapisy bez blokowania
- **Batched inserts**: buforowanie do `kBatchSize=500` ramek → jednorazowa transakcja
- **API**: `open/close`, `beginSession/endSession`, `recordFrame`, `flush`, `queryByCanId`, `totalFrameCount`, `listSessions`, `reset`
- Fix Qt warning: `m_db = QSqlDatabase()` przed `removeDatabase()` (zwolnienie wewnętrznej referencji)
- ✅ `CanObservationDbWidget.h/cpp` — UI: ścieżka DB, Open/Close, New Session, Reset, Query by CAN ID
- ✅ 13 testów — `QCoreApplication` przez `testing::Environment` (gtest globalny hook)
- ✅ Nowa zakładka "Frame DB" w MainWindow

---

## Sesja 2026-05-13, część 2: Alert Pipeline + PCAP + SQLite Analytics ✅

### Testy: 307 → 330 (+23 testów, 23 test suites) | Commit: `5e8539f`

### CanAlertEngine — rule-based alert system ✅
- **4 typy reguł**: `NewCanId`, `DlcChange`, `ByteValue` (byte[N] op threshold), `RateAnomaly` (sliding window 20 timestamps, baseline po 8+ próbkach)
- `reset()` czyści stan uczenia i licznik alertów
- ✅ `CanAlertWidget.h/cpp` — UI: edytor reguł, tabela aktywnych reguł, kolorowany log
- ✅ 15 testów
- ✅ Nowa zakładka "Alerts"

### PcapExporter — eksport PCAP Wireshark-compatible ✅
- LINKTYPE_CAN_SOCKETCAN (227), global header magic `0xa1b2c3d4` (LE, µs), version 2.4
- Per-frame: 16-byte packet header + 16-byte socketcan payload (can_id z EFF/FD flag)
- ✅ Przycisk "🦈 Eksportuj PCAP" w toolbarze MainWindow
- ✅ `CanObservationDb::exportToPcap(path, canId, sessionId, limit)`
- ✅ 8 testów

### CanObservationDb — nowe metody analityczne ✅
- `queryTimeRange(fromUs, toUs, sessionId, limit)` — zapytanie po oknie czasowym
- `findDlcAnomalies(sessionId)` — SQL GROUP BY + mode detection
- `computeIdFrequencies(sessionId)` — per-ID: frameCount + avgIntervalUs

---

## Sesja 2026-05-13, część 3: CanProtocolTimelineWidget ✅

### Testy: 330 → 344 (+14 testów, 24 test suites) | Commit: `b2b5e67`

### CanTimelineModel ✅
- Zdarzenia: `{tsUs, protocolName, canId, label, color}` — max `kMaxEvents=5000`
- `events(windowUs, bucketCount)` → agregacja do kubełków dla rysowania osi czasu
- `clearBefore(tsUs)` — sliding window

### CanProtocolTimelineWidget ✅
- Qt6 Charts scatter chart — `QCategoryAxis` (Y: nazwy protokołów) + `QValueAxis` (X: czas w sekundach)
- Klasyfikacja ramki per protokół, rolling 30s okno
- Fix: `QCategoryAxis::remove()` przyjmuje `const QString&`, nie `QStringList`
- ✅ Nowa zakładka "Timeline"
- ✅ 14 testów

---

## Sesja 2026-05-13, część 4: HTTP REST API + CanObservationDb fix ✅

### Testy: 344 → 361 (+17 testów, 25 test suites) | Commit: `32acd36`

### HttpRestServer ✅
- Serwer HTTP/1.0 na `QTcpServer` (Qt6::Network)
- **7 endpointów**: GET /api/status, /stats, /frames?limit=N&id=0xXXX, /ids, /alerts + POST /api/send, /start, /stop
- CORS: `Access-Control-Allow-Origin: *`, 204 dla OPTIONS
- `setAlertEngine(CanAlertEngine*)` + ring buffer 100 alertów z `std::mutex`
- ✅ 17 testów — `doRequest()` helper z `QCoreApplication::processEvents()` spin

### CanObservationDb — fix przepełnienia bufora ✅
- `std::copy(frame.data.begin(), frame.data.end(), row.data)` kopiowało 64B do `data[8]`
- Fix: `std::copy_n(frame.data.begin(), std::min(frame.dlc, 8), row.data)`

### Dokumentacja ✅
- README.md: 2.1.0 → 2.2.0, Mermaid diagram, nowe sekcje (Frame DB, Alerts, Timeline)
- README.tex: nowe sekcje, TikZ SQLite/GTest nodes

---

## Sesja 2026-05-13, część 5: CanByteHeatmap + crash fix ✅

### Testy: 361 → 374 (+13 testów, 26 test suites) | Commit: `4dd56dd`

### SIGSEGV crash fix — aplikacja nie startowała ✅
- **Diagnoza (GNU GDB)**: SIGSEGV w `CanAlertWidget::onTypeChanged(int)` podczas budowy `MainWindow`
- Przyczyna: `m_nameEdit->parentWidget()->parentWidget()` → `nullptr` → `qobject_cast<QGroupBox*>(nullptr)->layout()` = SIGSEGV
- Fix: usunięte 2 martwe linie (cały łańcuch castów był martwym kodem)
- Uwaga: fałszywy crash przy `QT_QPA_PLATFORM=offscreen` na statycznym buildzie Windows — fix: `Remove-Item Env:QT_QPA_PLATFORM`

### CanByteHeatmapModel ✅
- Per-ID `std::deque` timestamps + byte values, ring buffer cap (`maxFramesPerid=5000`)
- `heatmapData(id, windowUs, bucketCount)` → `vector<array<double,8>>` — średnia per kubełek; `-1.0` dla pustych

### CanByteHeatmapWidget ✅
- Custom `QWidget::paintEvent` — gradient 5-stopniowy (`0x00`=ciemny granat → `0xFF`=żółty/biały)
- Hover: QToolTip z Bajt/kubełek/średnia; Prev/Next per ID; combo okno czasu + kubełki; Timer 500ms
- ✅ 13 testów, nowa zakładka "Byte Heatmap"

### Dystrybucja Windows ✅
- 31 DLL-ek MSYS2/UCRT64 w `build_native/`, śledzone w Git
- Qt6 linkowany statycznie (brak Qt6*.dll), ~62 MB exe

---

## Sesja 2026-05-13, część 6: CAN FD stats + ARXML + Alert ML + Lua + CanGaugeWidget ✅

### Testy: 374 → 400 (+26 testów, 28 test suites) | Commit: `ab1a4fa`

### CAN FD extended stats ✅
- `CanFrame.h`: dodane `bool brs = false` (Bit Rate Switch) + `bool esi = false` (Error State Indicator)
- `CanIdStats`: `fdFrameCount`, `brsCount`, `esiCount`, `maxFdDlc` + eksport CSV + nowe kolumny w UI
- +6 testów

### AUTOSAR ARXML import ✅
- `ArxmlParser.h/cpp` (~270 LOC, `QXmlStreamReader`): I-SIGNAL → I-SIGNAL-I-PDU → FRAME → CAN-FRAME-TRIGGERING
- Zwraca `QVector<DbcMessage>` — bezpośrednio kompatybilny z `DbcParser`
- **CRITICAL**: zmienna `signals` → `sigDefs` (Qt `#define signals = public`)
- Przycisk "ARXML" w toolbarze MainWindow, merge z istniejącym DBC, wire 10 widgetów
- +8 testów

### Alert ML (IsolationForest → Alert Rule) ✅
- `LearningEngine::scoreLatestWindow()` — Isolation Forest score dla ostatniego okna [0=normal, 1=anomaly]
- `CanAlertEngine`: nowy `AlertType::IsolationForestScore`, pola `isThreshold` + `scorer`
- `MainWindow`: `m_alertWidget->setScorer([this](){ return m_learner->scoreLatestWindow(); })`
- +3 testy

### Lua hook w Alert Pipeline ✅
- `LuaScriptEngine`: slot `callOnAlert(const CanAlert&)` — wywołuje opcjonalną Lua `onAlert(ruleName, canId, desc, data[], tsUs)`
- `MainWindow`: `connect(alertEngine, alertTriggered, luaEngine, callOnAlert)`

### CanGaugeWidget ✅
- 3 tryby: Bar / Digital / Compact
- `setRange/setValue`, `isOutOfRange()`, `minObserved/maxObserved`, `setStaleTimeout/checkStaleness`
- +9 testów (`GaugeTest` fixture z `SetUpTestSuite()`)

### Fix: QApplication w środowiskach testowych ✅
- `test_httprestserver.cpp`: `QCoreApplication` → `QApplication`
- `test_canobservationdb.cpp`: guard `!QApplication::instance()`
- `test_cangaugewidget.cpp`: `SetUpTestSuite()` tworzy `QApplication` jeśli brak

---

## Sesja 2026-05-13, część 7: CanModuleProfiler — detekcja bicia serca modułu CAN ✅

### Testy: 400 → 414 (+14 testów, 29 test suites) | Commit: `3fa95ec`

### CanModuleProfiler ✅
- **Faza uczenia** (`startLearning`): Welford online statistics per ID → cykliczne ID (`frames ≥ 5` AND `jitter < 35% × avgInterval`), pary req/resp (okno 150ms, ≥5 wystąpień)
- **Faza detekcji** (`startDetecting`): Timer 250ms → `checkDetection(nowUs)` — deadline = `lastSeen + (avg + max(3σ, 50ms)) * 1000`
- `moduleOffline/moduleOnline` sygnały; `initNowUs=UINT64_MAX` = realny zegar (produkcja), `=0` = baza testowa
- **Symulacja**: przez wewnętrzny `CanPeriodicSender`

### CanModuleProfilerWidget ✅
- Uczenie: pole nazwy, spinner 2-60s, pasek postępu; Lista profili: Wykrywaj/Stop, Symuluj/Stop, Usuń, Zapisz/Wczytaj JSON
- Status: ● ONLINE (zielony) / ● OFFLINE (czerwony)
- Alerty OFFLINE routowane do `CanAlertEngine::submitExternalAlert()`

### CanAlertEngine ✅
- Nowa metoda `submitExternalAlert(CanAlert)` — routing alertów spoza systemu reguł

### Bug naprawiony ✅
- `startDetecting()` inicjalizował `m_lastSeenUs` realnym zegarem (~1.7×10¹⁵ µs), testy podawały relatywny czas → sentinel `UINT64_MAX` rozwiązuje problem

**14 testów** (InitialState, StartLearning, Periodic/Aperiodic/TooFew detection, ReqResp, Detection offline/online, Recovery, StopDetecting, JsonRoundtrip, EmptyProfile)

---

## Sesja 2026-05-13, część 8: CanModuleProfiler — uczenie różnicowe ✅

### Testy: 414 → 419 (+5 testów, 29 suites) | Commit: `4059569`

### Uczenie różnicowe 2-fazowe ✅
- **Algorytm**: Faza 1 (moduł podłączony) → Zbiór A; Faza 2 (moduł odłączony) → Zbiór B; Profil = A \ B
- `ModuleProfile`: nowe pole `bool isDifferential = false`, serializowane w JSON
- `CanModuleProfiler`: nowy stan `LearningBackground`, `startLearningBackground(phase1, name, durationMs)`, `finalizeBackgroundProfile()` — oblicza A \ B, emituje `backgroundLearningFinished`
- Widget: ukryty panel "Faza 2: uczenie tła" — pojawia się PO zakończeniu fazy 1; tag `[diff]` w liście profili

**5 testów**: BackgroundIdRemoved, UniqueIdKept, EmptyBackgroundKeepsAll, JsonRoundtrip, StateTransitions

---

## Sesja 2026-05-13, część 9: CanPrototypeExporter — eksport kodu ✅

### Testy: 419 → 435 (+16 testów, 30 suites) | Commit: `3bc6c62`

### Formaty wyjściowe
| Format | Opis |
|--------|------|
| **Python** (python-can) | Klasy wiadomości, wątki periodyczne, obsługa sygnałów DBC, konfiguracja socketcan/pcan |
| **Lua** | Tabele z metodą encode(), pętla symulacji |
| **Arduino C++** | Szkic `.ino` z MCP2515 library, setup()/loop(), timing millis() |

### Implementacja ✅
- Źródła: CanFrameModel + DbcParser + ModuleProfile.periodicIds + historia krocząca (64 ramki/ID)
- Detekcja auto-increment: ≥60% par kolejnych ramek z `byte[n+1] - byte[n] == 1 (mod 256)` → `_ctr_byteN++`
- Python: klasa per wiadomość, `build_message() → can.Message`, `_periodic()` helper z threading
- Arduino: zmienne globalne per wiadomość, `encode_X()` z `constrain()`, opcjonalny RX loop
- `CanPrototypeExporterWidget`: selektor formatu/interfejsu/kanału, podgląd kodu, eksport do pliku
- Nowa zakładka "Eksport kodu", podpięty do `frameProcessed` + 4 ścieżki ładowania DBC

**16 testów** (Python/Lua/Arduino: header, period, signals, counter, receiver; MultipleMessages, NonPeriodicFrame)

---

## Sesja 2026-05-13, Linux build fix | Commit: `093d414`

### Środowisko
- System: Kali Linux, GCC 15.2.0, Qt 6.10.2 (dynamiczne), CMake 4.2.3

### Zainstalowane pakiety
```bash
apt-get install -y qt6-serialport-dev liblua5.4-dev libzstd-dev libgl-dev qt6-base-dev qt6-charts-dev qt6-websockets-dev
```

### Naprawione błędy kompilacji
| Plik | Błąd | Naprawa |
|------|------|---------|
| `src/core/SocketCanDriver.cpp:90` | `std::min(uint8_t, int)` — GCC 15 nie deducuje przy niezgodnych typach | `std::min((int)frame.dlc, 64)` |
| `src/core/CanObservationDb.cpp:88,99,100` | `int64_t` (= `long` na Linux) niejednoznaczny dla `QVariant` — 7 kandydatów | `static_cast<qlonglong>(...)` |
| `CMakeLists.txt` | `pkg_check_modules(ZSTD)` tylko w bloku `WIN32` — linker nie znajdował `libzstd` na Linux | Przeniesione poza `if(WIN32 OR MINGW)` |

### Build
```bash
cmake -B build_linux -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build_linux --parallel $(nproc)
# Wynik: build_linux/MagistralaCAN4 (3.1 MB ELF 64-bit)
```

### Ostrzeżenie (niekrytyczne)
`QSortFilterProxyModel::invalidateFilter()` deprecated w Qt 6.10 — działa poprawnie, można zostawić.
