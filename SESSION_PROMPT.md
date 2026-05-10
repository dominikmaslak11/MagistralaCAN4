# Sesja optymalizacji zakładki "Ruch CAN" — 2026-05-10

## Zrealizowane
- ✅ QToolBar przeniesiony z QMainWindow do layoutu zakładki "Ruch CAN"
- ✅ CanStatsPanel skompresowany do 26px (maxHeight)
- ✅ QTableView dosunięty do góry, spacing=0
- ✅ **#1 QSortFilterProxyModel** (commit `6b44aa3`) — CanFilterProxy, -45 linii z CanFrameModel::sort()
- ✅ **#2 Cache DisplayRole** (commit `cd3a978`) — lazy-fill cache 9 kolumn, O(1) po pierwszym wyświetleniu

## Pomysły na optymalizację / modernizację

### Wysoki priorytet
1. ~~QSortFilterProxyModel~~ ✅
2. ~~Cache'owanie data()~~ ✅
3. **Wirtualne scrollowanie** — `setBatchSize(200)` + `ScrollPerPixel`

### Średni priorytet
4. **Batch'owanie `frameUpdated`** dla WebSocket — wysyłka paczkami JSON zamiast per-ramka
5. **Cykliczny bufor w modelu** zamiast `m_frames.erase(begin(), N)` — zero kopiowania
6. **Throttling slotów analizy** — pomijanie co N-tej ramki dla dashboardu, plug-in loader

### Niski priorytet (UX)
7. Live podświetlanie zmienionych bajtów w tabeli
8. Menu kontekstowe prawego przycisku
9. Pasek szybkiego skoku (heatmap scrollbar)
10. Presety filtrów ID

## Kolejność implementacji
1. ~~QSortFilterProxyModel~~ ✅
2. ~~Cache data()~~ ✅
3. Wirtualne scrollowanie ← teraz
4. Batch'owanie frameUpdated
5. Cykliczny bufor
