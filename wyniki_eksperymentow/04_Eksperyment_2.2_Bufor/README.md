# Eksperyment 2.2 — Buffer Overflow Threshold (próg przepełnienia bufora)

## 1. Co badano — wyjaśnienie dla osoby spoza tematu

Gdy urządzenie czeka na odpowiedź modelu językowego (~2,2 sekundy — patrz
Eksperyment 1.1), ramki **nadal napływają**. Muszą gdzieś poczekać — w buforze.

Bufor ma ograniczony rozmiar. Pytanie: **przy jakiej częstotliwości ruchu
i jakim rozmiarze bufora zaczniemy tracić dane?**

## 2. Czym i jak to zrobiono

**Sprzęt:** ESP32 + MCP2515, magistrala 250 kbit/s sterowana przez PEAK PCAN-USB.

**Metoda:** symulowano oczekiwanie na model — urządzenie celowo **wstrzymywało
odbiór** na zadany czas, podczas gdy ramki napływały ze stałą częstotliwością.
Zmieniano trzy parametry:
- rozmiar bufora: 4, 8, 16, 32 ramki,
- częstotliwość napływu: 100, 500, 1000 Hz,
- czas wstrzymania: rosnący, aż do wystąpienia straty.

**Hipoteza do sprawdzenia:** czas do przepełnienia powinien wynosić
`T = rozmiar_bufora / częstotliwość`.

## 3. Pliki wynikowe

| Plik | Co zawiera |
|---|---|
| `raw_wszystkie_bufory.csv` | wszystkie kombinacje parametrów i wyniki |

### Kolumny

| Kolumna | Znaczenie |
|---|---|
| `buffer_size` | zadany rozmiar bufora (w ramkach) |
| `buffer_size_confirmed` | rozmiar **potwierdzony** odczytem z urządzenia |
| `frequency_hz` | częstotliwość napływu ramek |
| `theoretical_ms` | **przewidywany** próg z wzoru `bufor / częstotliwość` |
| `factor` | mnożnik czasu wstrzymania względem progu teoretycznego |
| `wait_ms_target` | faktyczny czas wstrzymania odbioru |
| `n_sent` | ile ramek wysłano w tym czasie |
| `total_arrived` | ile dotarło |
| `dropped` | ile zgubiono |
| `loss` | czy wystąpiła strata (prawda/fałsz) |

## 4. Przykładowe dane

```
buffer_size,buffer_size_confirmed,frequency_hz,theoretical_ms,factor,wait_ms_target,n_sent,total_arrived,dropped,loss
4,4,100,40.0,0.3,12,18,18,0,False
4,4,100,40.0,0.6,24,19,19,0,False
```

Odczyt: bufor 4 ramki, ruch 100 Hz, próg teoretyczny 40 ms. Przy wstrzymaniu na
12 ms (0,3 progu) i 24 ms (0,6 progu) — **brak strat**, zgodnie z oczekiwaniem.

## 5. Jak sprawdzano poprawność

1. **Potwierdzenie rozmiaru bufora** — kolumna `buffer_size_confirmed` zawiera
   wartość **odczytaną z urządzenia**, nie zadaną. Chroni przed sytuacją, w której
   ustawienie nie zostało faktycznie zastosowane.
2. **Porównanie z przewidywaniem teoretycznym** — dla każdej kombinacji znany
   jest próg z wzoru. Zgodność pomiaru z przewidywaniem jest kryterium
   poprawności eksperymentu.
3. **Stopniowanie czasu wstrzymania** (kolumna `factor`) — pozwala znaleźć
   granicę, a nie tylko stwierdzić „strata / brak straty".

## 6. Wynik końcowy (najlepsza, ostateczna wersja)

Zależność `T = rozmiar_bufora / częstotliwość` **potwierdzona empirycznie
z dużą dokładnością**:

| Bufor [ramek] | Częstotliwość | Próg teoretyczny | Zmierzony maksymalny bezpieczny |
|---|---|---|---|
| 4 | 100 Hz | 40 ms | 40 ms |
| 4 | 500 Hz | 8 ms | 8 ms |
| 4 | 1000 Hz | 4 ms | 6 ms |
| 8 | 100 Hz | 80 ms | 80 ms |
| 8 | 500 Hz | 16 ms | 16 ms |
| 8 | 1000 Hz | 8 ms | 8 ms |
| 16 | 100 Hz | 160 ms | 160 ms |
| 16 | 500 Hz | 32 ms | 32 ms |
| 16 | 1000 Hz | 16 ms | 16 ms |

## 7. Wniosek i znaleziona luka

Zależność jest przewidywalna, więc **wymagany rozmiar bufora da się wyliczyć**
z góry dla dowolnego scenariusza.

**Znaleziona luka w istniejącej architekturze:** obecne oprogramowanie
**nie ma bufora aplikacyjnego** — każda odebrana ramka jest natychmiast
przekazywana dalej przez WebSocket, bez kolejkowania.

Skala problemu: dla typowego oczekiwania ~2,2 s przy ruchu 1000 Hz potrzeba
bufora na **~2200 ramek (~35 KB)**. ESP32 ma ~270 KB wolnej pamięci, czyli
**miejsca jest z zapasem**.

**To jest brak w oprogramowaniu, nie ograniczenie sprzętu** — do naprawienia
bez zmiany platformy.
