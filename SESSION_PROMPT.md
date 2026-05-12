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
