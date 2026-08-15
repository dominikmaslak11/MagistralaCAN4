# Eksperyment 4.5 (dodatek) — Strojenie progu klasyfikatora `looks_like_bit_flags`

Data: 2026-08-08
Autorzy: Dominik Maślak (prowadzenie), Claude (asystent, implementacja i analiza)
Status: **ZAKOŃCZONY** — analiza post-hoc na już zebranych danych z godzinnego
przebiegu Eksperymentu 4.5 (Faza 1), zero nowego sprzętu/czasu obserwacji.
Zaimplementowana zmiana w kodzie.

Kontekst: bezpośrednia kontynuacja sekcji 6.3 `Eksperyment_4.5_Raport_Koncowy_20260808.md`
("Rzecz, która mnie zaskoczyła i uważam za wartą dalszego zbadania").

---

## 1. Pytanie

`looks_like_bit_flags()` (1:1 port `DecodingAccuracyRunner::looksLikeBitFlags`,
C++) odrzuca kandydata na `bit_flag`, jeśli stosunek "dużych skoków" wartości do
wszystkich zmian (`big_jumps / changed_pairs`) jest niższy niż **0.5** — próg
nigdy niestrojony, ustawiony pierwotnie bez systematycznej analizy. Faza 1
Eksperymentu 4.5 pokazała recall=60% dla flag bitowych po godzinie ciągłej
obserwacji, przy 100% precyzji. Pytanie: czy 60% to realna granica tej
heurystyki, czy artefakt akurat tego jednego progu?

---

## 2. Metoda

Bez żadnego nowego zbierania danych — wykorzystano `observer_state.json`
(pełny, surowy stan po 1h/1,6mln ramek: `seen0`, `seen1`, `changed_pairs`,
`big_jumps`, `n_samples` per 160 śledzonych pozycji bajtowych) i ground truth
schematu (seed=999, 20 prawdziwych pozycji `bit_flag`). Dla progu
`τ ∈ {0.00, 0.05, ..., 1.00}` przeliczono werdykt klasyfikatora i policzono
precyzję/recall względem ground truth — czysta reanaliza istniejących liczb,
kilka sekund obliczeń.

---

## 3. Wyniki

### 3.1 Krzywa precyzja/recall vs próg

| Próg τ | TP | FP | Precision | Recall |
|---|---|---|---|---|
| 0.00 – 0.46 | 17 | 0 | 100.0% | **85.0%** |
| 0.47 | 16 | 0 | 100.0% | 80.0% |
| 0.48 | 15 | 0 | 100.0% | 75.0% |
| 0.49 | 14 | 0 | 100.0% | 70.0% |
| **0.50 (domyślny)** | 12 | 0 | 100.0% | **60.0%** |
| 0.55 – 0.60 | 11 | 0 | 100.0% | 55.0% |
| 0.65 | 10 | 0 | 100.0% | 50.0% |
| 1.00 | 4 | 0 | 100.0% | 20.0% |

**Precyzja = 100% na CAŁYM zakresie 0.0–1.0** — próg ten nigdy, w żadnym
punkcie, nie generuje fałszywego alarmu na tym zbiorze danych. **Recall jest
płaski na 85% od 0.0 do 0.46, po czym gwałtownie spada** — urwisko leży
dokładnie między **0.46 a 0.47** (TP: 17→16, dalej monotonicznie w dół).

### 3.2 Rozbicie 8 pozycji utraconych przy domyślnym progu 0.5

Z 8 pozycji pomijanych przy τ=0.5: **5 odzyskuje się przy jakimkolwiek
τ≤0.46** (czysty, darmowy zysk — próg ratio był ich jedyną przeszkodą).
**Pozostałe 3 pozostają nieuchwytne nawet przy τ=0.0** — ich prawdziwa
przyczyna to zupełnie inne ograniczenie: `bit_count > 6`:

| Pozycja | bit_count | Przyczyna odrzucenia |
|---|---|---|
| (848,5) | 8 | górny limit bit_count≤6 |
| (912,1) | 7 | górny limit bit_count≤6 |
| (960,2) | 7 | górny limit bit_count≤6 |

### 3.3 Czy warto też poluzować `bit_count ≤ 6`? — NIE

Sprawdzono bezpośrednio: wśród 140 pozycji NIE będących flagami, **26 ma
bit_count 7 lub 8** (bajty skalarne o wysokiej entropii, naturalnie
eksplorujące większość/cały zakres bitowy). Poluzowanie górnego limitu
złapałoby 3 dodatkowe prawdziwe flagi kosztem **26 nowych fałszywych
alarmów** — zawaliłoby precyzję ze 100% do ~40%. **Ten limit jest dobrze
skalibrowany i nie powinien być ruszany.**

---

## 4. Wnioski i moje spostrzeżenia (Claude)

### 4.1 Główny wniosek

Sufit recall=60% z Fazy 1 **NIE jest granicą heurystyki `looks_like_bit_flags`
jako takiej** — to artefakt jednej konkretnej, nigdy niestrojonej stałej.
Realny sufit tej heurystyki (przy zachowaniu 100% precyzji, czyli bez
kompromisu) to **85%**, nie 60%. Różnica (25pp) była leżącym na ulicy,
darmowym usprawnieniem.

### 4.2 To, co mnie przekonuje, że to nie przypadek/przeuczenie na jednym zbiorze

Wynik ma dwie cechy, które budują zaufanie, że to prawdziwy efekt, nie
artefakt jednego seeda:
1. **Precyzja jest identyczna (100%) na całym zakresie progu** — gdyby to był
   przypadkowy szum, spodziewalibyśmy się choć jednego fałszywego alarmu
   gdzieś w zakresie 0.0-0.46 na 140 pozycjach nie-flagowych obserwowanych
   przez godzinę. Zero.
2. **Urwisko jest ostre i wąskie** (0.46→0.47, nie płynne osuwanie się od
   0.3 do 0.7) — to sugeruje, że w tym konkretnym korpusie istnieje
   naturalna, wyraźna granica między "wzorcem zachowania typowym dla flag"
   a "typowym dla czegoś innego", a próg 0.5 przypadkiem wylądował tuż nad
   nią, a nie że dane są rozmyte i próg jest arbitralny wszędzie.

### 4.3 Ograniczenie tego wniosku, które trzeba jawnie powiedzieć

To analiza na **jednym zbiorze danych (seed=999, jeden przebieg godzinny)**.
Dokładna lokalizacja urwiska (0.46/0.47) jest specyficzna dla tego korpusu —
inny rozkład sygnałów mógłby przesunąć urwisko gdzie indziej. To, co uważam
za bardziej przenośne niż sama liczba 0.46, to **kierunek i skala wniosku**:
próg 0.5 nigdy nie był dobierany systematycznie, i pierwsza systematyczna
analiza od razu znalazła dużą (25pp), darmową poprawę. To sugeruje, że warto
tę analizę powtórzyć na kolejnych, niezależnych przebiegach/seedach zanim
uzna się 0.3 (lub jakąkolwiek inną wartość) za ostateczną — na razie to
najlepsza dostępna, ale wciąż tymczasowa, dobrze umotywowana estymata.

### 4.4 Dlaczego wybrałem 0.3, nie 0.46 (górna granica płaskiej strefy) ani 0.0

0.46 to dokładnie krawędź urwiska — wybór tam zero marginesu bezpieczeństwa
na szum przy innym seedzie/przebiegu. 0.0 całkowicie wyłącza kryterium
"wielkości skoku", tracąc jego semantyczny sens (odróżnianie nagłych,
skokowych zmian typowych dla przełączania bitów od płynnego dryfu typowego
dla skalara) na wypadek innych danych, gdzie mogłoby to jednak mieć znaczenie.
**0.3** leży wygodnie w płaskiej strefie z marginesem ~0.16 od znanego urwiska,
zachowując przy tym częściowo dyskryminującą funkcję kryterium.

### 4.5 Sugestia na kolejny krok

Powtórzyć tę samą analizę threshold-sweep na NIEZALEŻNYM przebiegu (inny
seed niż 999) — jeśli urwisko wciąż leży w podobnej okolicy (0.3-0.5), to
mocny dowód, że 0.3 jest bezpiecznym, ogólnym wyborem. Jeśli przesuwa się
istotnie, warto rozważyć próg adaptacyjny (np. wyznaczany per-pojazd z
pierwszych N minut obserwacji) zamiast jednej stałej "zaszytej na sztywno".

---

## 5. Zmiany w kodzie

| Plik | Zmiana |
|---|---|
| `esp_experiment_4_5_rpi/pi_continuous_observer.py` | Nowa stała `BIG_JUMP_RATIO_THRESHOLD = 0.3` (było: `0.5` zaszyte na sztywno w kodzie). To jest wersja "produkcyjna", używana od teraz przy każdym kolejnym przebiegu na Pi. |
| `esp_experiment_4_3/etap_b_autolabel.py` | `looks_like_bit_flags()` przyjmuje teraz opcjonalny parametr `big_jump_ratio_threshold` (domyślnie **wciąż 0.5**, celowo niezmienione) + nowa flaga CLI `--threshold`. Domyślna wartość NIE została zmieniona, żeby zachować dokładną odtwarzalność wyników już opublikowanych w `Eksperyment_4.3_Raport`/`Eksperyment_4.4_Raport_Koncowy` (oba cytują liczby policzone przy progu 0.5). Do użycia postrojonej wersji: `python3 etap_b_autolabel.py --threshold 0.3`. |

Matematyczna równoważność między obiema implementacjami (`ByteStat` w
`pi_continuous_observer.py` vs `looks_like_bit_flags` w `etap_b_autolabel.py`)
**zweryfikowana ponownie przy nowym progu 0.3** — 2000 losowych testów, 0
rozbieżności.

**Nietknięte celowo**: `src/core/DecodingAccuracyRunner.cpp` (referencyjna
implementacja C++, używana w firmware ESP32 i innych eksperymentach tej
sesji) — zmiana tam wykracza poza zakres tej analizy i wpłynęłaby na kod
produkcyjny/firmware, nie tylko na skrypty analityczne. Jeśli wniosek z tej
analizy ma zostać przeniesiony do C++, warto zrobić to jako osobną,
świadomą decyzję, najlepiej po punkcie 4.5 (powtórzeniu na niezależnym
przebiegu).
