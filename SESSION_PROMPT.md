# Sesja optymalizacji zakładki "Ruch CAN" — 2026-05-10

## Zrealizowane
- ✅ QToolBar przeniesiony z QMainWindow do layoutu zakładki "Ruch CAN"
- ✅ CanStatsPanel skompresowany do 26px (maxHeight)
- ✅ QTableView dosunięty do góry, spacing=0

## Pomysły na optymalizację / modernizację

### Wysoki priorytet
1. **QSortFilterProxyModel** zamiast ręcznego `setRowHidden()` + sortowania — filtrowanie O(1), sortowanie bez kopiowania
2. **Wirtualne scrollowanie** — `canFetchMore()`/`fetchMore()` w CanFrameModel, `setBatchSize(200)`
3. **Cache'owanie `data()`** — leniwe pola cache w CanFrame (hex string, kolory, nazwy DBC)

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
1. QSortFilterProxyModel (największy efekt / najmniej kodu)
2. Cache data()
3. Wirtualne scrollowanie
