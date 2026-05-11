# Sesja optymalizacji — MagistralaCAN4 — 2026-05-11

## Faza 1: Pipeline "Ruch CAN" (zrealizowane)
- ✅ **#1 QSortFilterProxyModel** (commit `6b44aa3`) — CanFilterProxy, -45 linii z CanFrameModel::sort()
- ✅ **#2 Cache DisplayRole** (commit `cd3a978`) — lazy-fill cache 9 kolumn, O(1) po pierwszym wyświetleniu
- ✅ **#3 Wirtualne scrollowanie** (commit `4d6f843`) — ScrollPerPixel, smooth scrolling
- ✅ **#4 Batch'owanie frameUpdated** (commit `900cfcb`) — WebSocket wysyła paczkami JSON zamiast per-ramka
- ✅ **#5 Cykliczny bufor** (commit `ee83427`) — pre-alokacja m_maxFrames, m_head + m_size, zero kopiowania
- ✅ **#6 Throttling slotów analizy** (commit `d3bae1f`) — frameProcessedThrottled, co 16-tą ramkę do widgetów
- ✅ **#7 Live podświetlanie zmienionych bajtów** (commit `0fee24b`) — changedMask + DataHighlightDelegate
- ✅ **#8 Menu kontekstowe** (commit `e1076b1`) — kopiuj ID/dane, filtruj, wyczyść
- ✅ **#9 Heatmap scrollbar** (commit `e1076b1`) — HeatmapBar, gęstość burstów, klik = skok
- ✅ **#10 Presety filtrów ID** (commit `e1076b1`) — zapis/odczyt z ~/.magistrala_can4/filter_presets.txt

## Faza 2: Stabilność i infrastruktura (zrealizowane)
- ✅ **#11 QSettings persistence** — geometria okna, ostatni interfejs, baud, opcje, motyw
- ✅ **#12 Async I/O CanRecorder** — RingBuffer 65536 slotów, dedykowany wątek I/O, zero blokowania GUI
- ✅ **#13 Testy jednostkowe** — naprawiona kompilacja (usunięty QtConcurrent z PCH, dodany Qt6::Gui, J1939Parser, fix frameUpdated→frameBatchUpdated)
- ✅ **#14 MDF4Writer async I/O** — RingBuffer 65536 slotów, wątek I/O, finalizacja nagłówka IDBL
- ✅ **#15 RemoteCanClient exponential backoff** — 1s → 2s → 4s → ... → 30s, reset po auth
- ✅ **#16 Async export candump/CSV** — QtConcurrent::run + QProgressDialog, GUI nie blokuje się
- ✅ **#17 Adaptacja do jasnego motywu** — HeatmapBar + DataHighlightDelegate sprawdzają palette
- ✅ **#18 MRU plików DBC/Lua** — QToolButton z menu, max 5, persistencja w QSettings
- ✅ **#19 Auto-odświeżanie interfejsów** — QTimer co 5s, tylko gdy nie sniffujemy
- ✅ **#20 Kompresja nagrań zstd** — .mcan → .mcan.zst, level 1, usuwa oryginał
- ✅ **#21 Undo/Redo modelu** — snapshot przed clear/setOverwriteMode, max 5, Ctrl+Z/Y
- ✅ **#22 Plugin crash protection** — setjmp/longjmp + SIGSEGV handler, izoluje crashujące pluginy bez QProcess
- ✅ **#23 Odtwarzanie nagrań** — CanPlayer, .mcan/.mcan.zst, prędkość 0.5x–10x, UI w toolbarze

---

## Faza 3: Modernizacja uczenia asocjacyjnego (w trakcie)

### Stan po Fazie A
`AssociativeLearner`: 2593→1169 LOC .cpp + 258→203 LOC .h = **1372 LOC** (przed: 2836, **-52%**)
`LearningEngine`: 309 .h + 1651 .cpp = **1960 LOC** — nowy, czysty C++, thread-safe

### Plan (6 faz)

#### Faza A: Dekompozycja silnika ✅ (zrealizowane)
- ✅ **#24 `LearningEngine`** — czysta klasa C++, STL-only, `std::shared_mutex`, 37 algorytmów ML
- ✅ **#25 `AssociativeLearner` → cienka warstwa UI** — tylko konstruktor widgetów + populacja tabel
- ✅ Stan przeniesiony do LearningEngine (frameHistory, events, observations, linearModels, Markov, NN, anomaly)
- ✅ `GpuCorrelator` usunięty z AssociativeLearner; PCA liczone CPU przez LearningEngine
- ✅ `kMeans` — wersja sekwencyjna (bez QtConcurrent::blockingMap)
- ✅ Kompilacja przechodzi (MSYS2/MinGW, Qt6 static)
- **Zysk:** testowalność, reuse, separacja concerns, -52% LOC w UI

#### Faza B: Online / incremental learning (następna)
- [ ] **#26 Ring buffer dla obserwacji** z configurowalną pojemnością (10k)
- [ ] **#27 Exponential forgetting** — waga `e^(-λ·Δt)` dla starych obserwacji
- [ ] **#28 Korelacje online** — Welford's algorithm, aktualizacja przyrostowa
- [ ] **#29 Anomalie: EWMA + adaptive threshold** zamiast rebuild całego modelu
- **Zysk:** ciągłe uczenie, adaptacja do zmieniającego się ruchu

#### Faza C: Unowocześnienie modeli predykcyjnych
- [ ] **#30 MLP v2** — minibatch SGD + Adam + L2 regularization + early stopping
- [ ] **#31 Gradient Boosted Trees** (własna implementacja XGBoost-lite)
- [ ] **#32 Przedziały ufności** dla predykcji (bootstrap residuals)
- [ ] **#33 Multi-target** — wsparcie dla wielu zmiennych jednocześnie

#### Faza D: Wydajność i skalowalność
- [ ] **#34 Sparse cross-byte correlation** — pomijaj bajty o zerowej wariancji
- [ ] **#35 DBSCAN z k-d tree** (nanoflann) zamiast O(N²)
- [ ] **#36 Poprawny WCSS** + inicjalizacja k-means++ ✅ (WCSS już poprawiony w autoKMeans)
- [ ] **#37 PCA: pełna eigen-decomposition** (Jacobi dla małych macierzy 5×5)

#### Faza E: Zaawansowana analiza
- [ ] **#38 Granger causality** — przyczynowość CAN → zdarzenie
- [ ] **#39 Change-point detection** (PELT / Binary Segmentation)
- [ ] **#40 Cross-correlation z przesunięciem czasowym** — opóźnione zależności
- [ ] **#41 t-SNE wizualizacja** (opcjonalnie, GPU)

#### Faza F: Trwałość i odtwarzalność
- [ ] **#42 Model versioning** — checkpointy z timestampem, rollback
- [ ] **#43 Auto-checkpoint** co N iteracji
- [ ] **#44 Eksport do ONNX** — własny serializer
- [ ] **#45 Session resume** — pełne odtworzenie stanu po restarcie
