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
