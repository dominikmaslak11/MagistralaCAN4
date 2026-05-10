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

## Pomysły na dalszą modernizację

### 🔴 Krytyczne (stabilność)
- Plugin isolation (QProcess per plugin zamiast shared libs)

### 🟡 Wydajność
- Async export candump/CSV (QtConcurrent::run + progress bar)
- GPU correlator CPU fallback (SSE2/AVX2)
- Kompresja nagrań (zstd)

### 🟢 UX
- Adaptacja HeatmapBar/DataHighlightDelegate do jasnego motywu
- Undo/Redo dla operacji na modelu (clear, delete)
- Ostatnio otwierane pliki DBC/Lua (MRU)
- Auto-odświeżanie listy interfejsów CAN

## Kolejność rekomendowana
1. Async export candump/CSV ← następny
2. GPU correlator CPU fallback
3. Kompresja nagrań (zstd)
4. Plugin isolation (długoterminowy)
