# Sesja optymalizacji — MagistralaCAN4 — 2026-05-10

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

## Wszystkie pozycje zrealizowane ✅

23 optymalizacje wdrożone w ciągu jednej sesji.

---

## Faza 3: Modernizacja uczenia asocjacyjnego (zaplanowane)

### Diagnoza stanu obecnego
`AssociativeLearner` (2578 LOC .cpp + 258 LOC .h = 2836 LOC) — monolit łączący UI, silnik ML i zarządzanie stanem w jednej klasie QWidget.

**Obecny zestaw metod:** Pearson, MIC, Mutual Information, DBSCAN, k-means, PCA+power iteration, FFT (Cooley-Tukey radix-2), MLP (3-warstwowy), łańcuch Markowa, auto-discovery, detekcja anomalii, eksport/import JSON, generator Lua, raport HTML.

**Problemy architektoniczne:**
- God-class: 2836 LOC, niemożliwy test jednostkowy
- Silnik ML wpleciony w QWidget — zero reuse bez GUI
- Brak incremental learning — każda operacja przelicza wszystko od zera
- DBSCAN alokuje pełną macierz O(N²) — crash przy >500 oknach
- K-means: WCSS to placeholder (wykres łokcia nie działa)
- MLP: brak minibatch, brak regularyzacji (overfitting), stały LR
- Stały wektor cech (67 dim), brak selekcji/dynamicznych cech
- Brak temporal decay — stare obserwacje = nowe
- Tylko jedna zmienna docelowa (`m_currentVariable`)

### Plan modernizacji (6 faz)

#### Faza A: Dekompozycja silnika (fundament)
- [ ] **#24 `LearningEngine`** — czysta klasa C++ bez Qt, cała logika ML
- [ ] **#25 `AssociativeLearner` → cienka warstwa UI** — deleguje do LearningEngine
- [ ] Stan (obserwacje, eventy, modele) przeniesiony do LearningEngine
- [ ] LearningEngine komunikuje się przez `std::function` callbacki
- [ ] Testy jednostkowe dla LearningEngine (headless)
- **Zysk:** testowalność, reuse, separacja concerns

#### Faza B: Online / incremental learning
- [ ] **#26 Ring buffer dla obserwacji** z configurowalną pojemnością (10k)
- [ ] **#27 Exponential forgetting** — waga `e^(-λ·Δt)` dla starych obserwacji
- [ ] **#28 Korelacje online** — Welford's algorithm, aktualizacja przyrostowa
- [ ] **#29 Anomalie: EWMA + adaptive threshold** zamiast rebuild całego modelu
- [ ] GUI nigdy się nie blokuje przy uczeniu
- **Zysk:** ciągłe uczenie, adaptacja do zmieniającego się ruchu

#### Faza C: Unowocześnienie modeli predykcyjnych
- [ ] **#30 MLP v2** — minibatch SGD + Adam + L2 regularization + early stopping
- [ ] **#31 Gradient Boosted Trees** (własna implementacja XGBoost-lite) — alternatywa dla regresji liniowej
- [ ] **#32 Przedziały ufności** dla predykcji (bootstrap residuals)
- [ ] **#33 Multi-target** — wsparcie dla wielu zmiennych jednocześnie
- **Zysk:** dokładniejsze predykcje, mniej overfittingu

#### Faza D: Wydajność i skalowalność
- [ ] **#34 Sparse cross-byte correlation** — pomijaj bajty o zerowej wariancji
- [ ] **#35 DBSCAN z k-d tree** (nanoflann) zamiast O(N²)
- [ ] **#36 Poprawny WCSS** + inicjalizacja k-means++
- [ ] **#37 PCA: pełna eigen-decomposition** (Jacobi dla małych macierzy 5×5)
- [ ] Cache Pearsona z inteligentną inwalidacją
- **Zysk:** 10-100x szybsze dla dużych logów

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
