# Eksperyment 1.2 — Hot Execution Latency (reakcja na znaną regułę)

## 1. Co badano — wyjaśnienie dla osoby spoza tematu

Gdy reguła dekodowania jest **już znana**, urządzenie nie musi nikogo pytać —
po prostu odbiera ramkę i reaguje. Pytanie: **ile trwa taka reakcja?**

Mierzony odcinek czasu (`t_resp`) zaczyna się w chwili, gdy układ MCP2515
sygnalizuje „mam nową ramkę" (przerwanie sprzętowe na wyprowadzeniu INT),
a kończy, gdy urządzenie zmieni stan wyjścia — czyli faktycznie zareaguje.

## 2. Czym i jak to zrobiono

**Metoda podstawowa:** pomiar wewnętrznym zegarem mikrosekundowym ESP32
(`esp_timer_get_time()`), N = 1000 prób na wariant.

**Metoda weryfikująca (niezależna):** **analizator stanów logicznych** — przyrząd
podłączony bezpośrednio do przewodów, mierzący napięcia. Nie ufa oprogramowaniu
ESP32 w żadnym stopniu. N = 570.

To rozróżnienie jest istotne: pierwsza metoda mierzy „co myśli o sobie
urządzenie", druga — „co faktycznie dzieje się na przewodach".

**Wcześniejsze, nieudane próby weryfikacji** (opisane dla uczciwości):
- oscyloskop Hantek 1008C — zawodny programowy mechanizm wyzwalania,
- analizator ATK-Logic DL16 — trwale niesprawne oprogramowanie układowe.

Dopiero trzecie podejście (klon Saleae Logic, obsługiwany przez `sigrok-cli`)
domknęło wątek.

## 3. Pliki wynikowe

| Plik | Co zawiera |
|---|---|
| `raw_timer_wewnetrzny.csv` | 1000 pomiarów zegarem ESP32 |
| `statystyki_timer_wewnetrzny.csv` | średnia, odchylenie, mediana |
| `raw_analizator_logiczny.csv` | 570 pomiarów niezależnym przyrządem |
| `statystyki_analizator_logiczny.csv` | jw. dla metody niezależnej |

### Kolumny plików `raw_*`

| Kolumna | Znaczenie |
|---|---|
| `Trial` | numer próby |
| `t_resp_us` | zmierzony czas reakcji w **mikrosekundach** |

## 4. Przykładowe dane

```
Trial,t_resp_us
21,109
22,109
23,110
```

Każdy wiersz to jedna ramka, która przyszła na magistralę, i czas, po którym
urządzenie na nią zareagowało.

## 5. Jak sprawdzano poprawność

**Weryfikacja krzyżowa dwiema niezależnymi metodami** — to jest główny mechanizm
kontroli w tym eksperymencie. Jeśli obie metody, oparte na zupełnie innych
zasadach fizycznych, dają zgodny wynik, pomiar jest wiarygodny.

Kryterium zgodności: różnica średnich powinna mieścić się w rozdzielczości obu
metod (~1 µs każda) plus typowe opóźnienie propagacji sygnału elektrycznego
względem programowego znacznika czasu.

## 6. Wynik końcowy (najlepsza, ostateczna wersja)

**Weryfikacja krzyżowa:**

| Metoda pomiaru | Średnia | Odch. std. | Mediana | N |
|---|---|---|---|---|
| Timer wewnętrzny ESP32 | **109,70 µs** | 1,52 µs | 109,0 µs | 1000 |
| Analizator logiczny (niezależny) | **112,45 µs** | 1,54 µs | 112,0 µs | 570 |

Różnica **2,75 µs (~2,5 %)** — mieści się w oczekiwanej rozdzielczości.
Odchylenia standardowe niemal identyczne (~1,5 µs).

**Warianty sprawdzone (wszystkie N = 1000):**

| Wariant | t_resp | Wniosek |
|---|---|---|
| Kwarc MCP2515 8 MHz | 109,70 µs | — |
| Kwarc MCP2515 16 MHz | 109,67 µs | **zegar nie ma znaczenia** (różnica 0,03 µs) |
| 125 kbit/s | 109,66 µs | — |
| 500 kbit/s | 109,72 µs | — |
| 1000 kbit/s | 109,66 µs | **prędkość magistrali nie ma znaczenia** |
| **Filtr sprzętowy + reakcja w przerwaniu** | **1,01 µs** | **100× szybciej** |

## 7. Wnioski

1. **Zegar układu MCP2515 nie wpływa na czas reakcji.** Sterownik przelicza
   dzielnik częstotliwości tak, by czas trwania bitu na magistrali pozostał ten
   sam — podwojenie zegara jest kompensowane.
2. **Prędkość magistrali też nie wpływa** (rozpiętość 8×: 125–1000 kbit/s).
   Powód: pomiar zaczyna się **po** pełnym odebraniu ramki, więc czas transmisji
   nie wchodzi do mierzonego okna.
3. **Filtr sprzętowy skraca czas stukrotnie** — ze 109,7 µs do 1,01 µs — ale
   wymaga, by reakcja nastąpiła wewnątrz procedury obsługi przerwania, co
   ogranicza to, co można w niej zrobić.
4. **99 % zmierzonego czasu to narzut komunikacji SPI i biblioteki**, nie sam
   mikrokontroler.

**Znaczenie dla projektu:** 110 mikrosekund wobec ~5 sekund zimnego startu
(Eksperyment 1.1) to różnica **pięciu rzędów wielkości**. Reakcja na znaną
regułę jest praktycznie darmowa.
