# Sesja optymalizacji zakładki "Ruch CAN" — 2026-05-10

## Zrealizowane
- ✅ QToolBar przeniesiony z QMainWindow do layoutu zakładki "Ruch CAN"
- ✅ CanStatsPanel skompresowany do 26px (maxHeight)
- ✅ QTableView dosunięty do góry, spacing=0
- ✅ **#1 QSortFilterProxyModel** (commit `6b44aa3`) — CanFilterProxy, -45 linii z CanFrameModel::sort()
- ✅ **#2 Cache DisplayRole** (commit `cd3a978`) — lazy-fill cache 9 kolumn, O(1) po pierwszym wyświetleniu
- ✅ **#3 Wirtualne scrollowanie** (commit `4d6f843`) — ScrollPerPixel, smooth scrolling
- ✅ **#4 Batch'owanie frameUpdated** (commit `900cfcb`) — WebSocket wysyła paczkami JSON zamiast per-ramka
- ✅ **#5 Cykliczny bufor** — pre-alokacja m_maxFrames, m_head + m_size, zero kopiowania przy przepełnieniu
- ✅ **#6 Throttling slotów analizy** — frameProcessedThrottled (co 16-tą ramkę), osobny sygnał dla ciężkich widgetów
- ✅ **#7 Live podświetlanie zmienionych bajtów** — changedMask (64-bit) w CanFrame + DataHighlightDelegate
- ✅ **#8 Menu kontekstowe** — prawy przycisk: kopiuj ID/dane, filtruj po ID, wyczyść filtr
- ✅ **#9 Pasek szybkiego skoku** — HeatmapBar (14px, gęstość burstów, klik = skok)
- ✅ **#10 Presety filtrów ID** — zapis/odczyt z ~/.magistrala_can4/filter_presets.txt

## Pomysły na optymalizację / modernizację

### Wysoki priorytet
1. ~~QSortFilterProxyModel~~ ✅
2. ~~Cache'owanie data()~~ ✅
3. ~~Wirtualne scrollowanie~~ ✅

### Średni priorytet
4. ~~Batch'owanie `frameUpdated`~~ ✅
5. ~~Cykliczny bufor w modelu~~ ✅
6. ~~Throttling slotów analizy~~ ✅ — drugi sygnał frameProcessedThrottled, co 16-tą ramkę do widgetów

### Niski priorytet (UX)
7. ~~Live podświetlanie zmienionych bajtów w tabeli~~ ✅
8. ~~Menu kontekstowe prawego przycisku~~ ✅
9. ~~Pasek szybkiego skoku (heatmap scrollbar)~~ ✅
10. ~~Presety filtrów ID~~ ✅

## Kolejność implementacji
1. ~~QSortFilterProxyModel~~ ✅
2. ~~Cache data()~~ ✅
3. ~~Wirtualne scrollowanie~~ ✅
4. ~~Batch'owanie frameUpdated~~ ✅
5. ~~Cykliczny bufor~~ ✅
6. ~~Throttling slotów analizy~~ ✅
7. ~~Live podświetlanie zmienionych bajtów~~ ✅
8. ~~Menu kontekstowe~~ ✅
9. ~~Heatmap scrollbar~~ ✅
10. ~~Presety filtrów ID~~ ✅
