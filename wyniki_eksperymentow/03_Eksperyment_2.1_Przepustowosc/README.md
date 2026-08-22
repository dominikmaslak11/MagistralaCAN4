# Eksperyment 2.1 — CAN Frame Throughput (przepustowość)

## 1. Co badano — wyjaśnienie dla osoby spoza tematu

Magistrala CAN potrafi przesyłać tysiące ramek na sekundę. Pytanie:
**przy jakim natężeniu ruchu mikrokontroler ESP32 zacznie gubić ramki?**

Zgubiona ramka to utracona informacja — w zastosowaniu diagnostycznym oznacza
lukę w danych. Trzeba wiedzieć, gdzie leży granica.

## 2. Czym i jak to zrobiono

**Generator ruchu:** PEAK PCAN-USB sterowany własnym skryptem w Pythonie przez
surowe gniazda SocketCAN.

**Metoda:** natężenie ruchu zwiększano **schodkowo co 100 ramek na sekundę**,
od 100 do 5000 — czyli **50 punktów pomiarowych**, po 2 sekundy na każdym.
Powtórzono dla dwóch prędkości magistrali: 250 i 500 kbit/s.

**Co mierzono:** ile ramek wysłano, ile dotarło, oraz **sprzętowy licznik
przepełnień** bufora odbiorczego w układzie MCP2515.

## 3. Pliki wynikowe

| Plik | Co zawiera |
|---|---|
| `raw_250kbps.csv` | wszystkie 50 punktów pomiarowych przy 250 kbit/s |
| `raw_500kbps.csv` | jw. przy 500 kbit/s |

### Kolumny

| Kolumna | Znaczenie |
|---|---|
| `bitrate_bps` | prędkość magistrali w bitach na sekundę |
| `target_rate_fps` | zadane natężenie (ramek na sekundę) |
| `actual_rate_fps` | **faktycznie osiągnięte** natężenie |
| `n_sent` | ile ramek wysłano |
| `n_enobufs` | ile razy system operacyjny odmówił wysłania (bufor nadawczy pełny) |
| `n_rcvd` | ile ramek odebrano |
| `overflow_events` | **sprzętowy licznik przepełnień w MCP2515** |
| `flr_percent` | **Frame Loss Rate** — odsetek zgubionych ramek |

## 4. Przykładowe dane

```
bitrate_bps,target_rate_fps,actual_rate_fps,n_sent,n_enobufs,n_rcvd,overflow_events,flr_percent
250000,100,100.5,201,0,201,0,0.0
250000,200,200.5,401,0,401,0,0.0
```

Odczyt: przy zadanych 100 ramkach/s faktycznie poszło 100,5; wysłano 201 ramek,
odebrano 201, zero przepełnień, **zero strat**.

## 5. Jak sprawdzano poprawność

Zastosowano **dwa niezależne wskaźniki utraty danych**:

1. **Porównanie liczników** — `n_rcvd` kontra `n_sent`. Różnica oznacza stratę.
2. **Sprzętowy licznik przepełnień MCP2515** (`overflow_events`) — układ sam
   raportuje, że nie zdążył opróżnić bufora.

Dodatkowo `actual_rate_fps` pozwala odróżnić dwie sytuacje: „urządzenie nie
nadążyło" od „generator nie zdołał wysłać tyle, ile chciał" (kolumna
`n_enobufs`). To rozróżnienie okazało się kluczowe dla wniosku.

## 6. Wynik końcowy (najlepsza, ostateczna wersja)

| Prędkość | Zakres testu | Sufit rzeczywisty magistrali | **Straty (FLR)** |
|---|---|---|---|
| 250 kbit/s | 100–5000 ramek/s | ~2260 ramek/s | **0,00 %** |
| 500 kbit/s | 100–5000 ramek/s | ~4365 ramek/s | **0,00 %** |

Sprzętowy licznik przepełnień MCP2515 **pozostał na zerze przez cały czas
trwania obu testów**.

## 7. Wniosek

**ESP32 nie zgubił ani jednej ramki w całym badanym zakresie, na obu
prędkościach magistrali.**

Kluczowa obserwacja: powyżej ~2260 ramek/s (przy 250 kbit/s) natężenie
przestawało rosnąć mimo zwiększania zadanej wartości — to **fizyczny sufit
przepustowości samej magistrali CAN**, nie granica mikrokontrolera.

**Wąskim gardłem okazała się magistrala, nie urządzenie.** ESP32 nigdy nie
zdradził oznak przeciążenia w całym praktycznie osiągalnym zakresie ruchu.

**Zastrzeżenie:** test obejmował wyłącznie odbiór i zliczanie ramek. Nie
obejmował jednoczesnego przetwarzania (parsowania sygnałów), które dokłada
własny narzut — patrz Eksperyment 5.1.
