# Eksperymenty 4.7-4.10 — sieć neuronowa kontra reguła ręczna w wykrywaniu flag bitowych

Data: 2026-08-15
Autorzy: Dominik Maślak (prowadzenie), Claude (asystent, implementacja i analiza)
Platforma: Orange Pi Zero 3 + MCP2515 (HAT Waveshare, kwarc 16 MHz), magistrala
wspólna z PEAK PCAN-USB
Status: **ZAKOŃCZONE**

---

## 1. Punkt wyjścia

`Eksperyment_4.6_Replikacja_OrangePiZero3_20260814.md` (sekcja 5c.2) wykazał, że
klasyfikator „Kierunku B" ma **sufit konstrukcyjny**: warunek
`2 <= bit_count <= 6` odrzuca bajty zawierające 7-8 niezależnie przełączających
się flag, mimo że ich współczynniki skokowości (0,97 / 0,84 / 0,82) przeszłyby
każdy próg. Recall zatrzymuje się na 85 % niezależnie od strojenia.

Podniesienie limitu do 8 daje Recall 100 %, ale Precision spada do ~50 % —
bajt, w którym wszystkie osiem bitów przełącza się niezależnie, jest dla prostej
reguły nieodróżnialny od szybko zmieniającego się skalara. **Granica jest
nieliniowa**, więc jest to zadanie dla klasyfikatora uczonego, nie dla progu.

---

## 2. Eksperyment 4.7 — konstrukcja i uczenie sieci

### 2.1. Architektura — świadomie minimalna

MLP **10 → 24 → 12 → 1**, **577 parametrów**. Wejściem są cechy liczone
przyrostowo (O(1) na ramkę) z tego samego stanu, który `pi_continuous_observer.py`
już utrzymuje: liczba bitów maski, współczynnik skokowości, częstość zmian,
udział wartości unikalnych, entropia, średnia i maksymalna delta, odchylenie
wartości, udział wartości skrajnych, odchylenie liczby zapalonych bitów.

Cechy wymagające pełnej historii próbek (FFT, autokorelacja) zostały świadomie
pominięte — sieć ma być wdrażalna w istniejącym demonie bez zmiany
charakterystyki pamięciowej.

### 2.2. Pułapka danych uczących — wykryta i ominięta

Pierwotnie dane uczące miały pochodzić z trybu `--dump-json` generatora.
Sprawdzono to najostrzejszym dostępnym testem: **uruchomiono na nich regułę
ręczną**, której zachowanie na żywej magistrali jest znane.

| Źródło danych | Recall | Precision | F1 |
|---|---|---|---|
| `--dump-json` (200 próbek/bajt) | 42,6 % | 74,3 % | 54,2 % |
| **vcan (realne taktowanie)** | **85,0 %** | **100 %** | **91,9 %** |
| Prawdziwy MCP2515 (przebieg 1 h) | 85,0 % | 100 % | 91,9 % |

Tryb offline symuluje czas skokowo (50 ms/próbkę) i daje zbyt mało próbek —
statystyki przełączeń wychodzą **zupełnie inne**. Sieć nauczona na takich danych
wyglądałaby dobrze na walidacji i rozsypała się przy wdrożeniu, a przyczyna
byłaby trudna do zlokalizowania.

**Rozwiązanie**: dane uczące zbierane z wirtualnych magistral SocketCAN (`vcan`),
12 równolegle, z realnym taktowaniem generatora. Rozkład odtworzony co do
dziesiątej części procenta względem prawdziwego sprzętu.

### 2.3. Protokół

- **uczenie**: ziarna 1-24 (vcan) — 4300 pozycji, 17,0 % pozytywnych
- **walidacja**: ziarna 25-30 (vcan) — 1022 pozycje, wyłącznie do wyboru epoki
- **test**: ziarna 100-105 na **prawdziwym MCP2515** — rozkład nigdy nie widziany

Ważenie klasy dodatniej w funkcji straty (flagi to ~17 % zbioru).

### 2.4. Koszt obliczeniowy — odpowiedź na pytanie o mocniejszy sprzęt

**Uczenie całej sieci na Orange Pi Zero 3 zajęło 21,8 s** (4 rdzenie Cortex-A53).
Na maszynie ośmiordzeniowej byłoby to może 10 s. **Mocniejszy sprzęt nie jest
tu wąskim gardłem niczego** — ani na etapie uczenia, ani inferencji (577
parametrów to ułamek milisekundy na bajt).

Gdyby kiedykolwiek potrzebna była realnie większa moc, wąskim gardłem byłby
**VRAM karty graficznej** (fine-tuning LLM z Eksperymentu 4.3 Etap D albo lokalny
model 7B), a nie liczba rdzeni CPU ani RAM.

---

## 3. Eksperyment 4.8 — porównanie na danych sprzętowych

### 3.1. Co poszło nie tak w założeniu (uczciwie)

Eksperyment zaprojektowano jako **test odporności poza punktem strojenia**:
hipoteza mówiła, że reguła ręczna, nastrojona przy 250 kbit/s, będzie degradować
się przy innych prędkościach magistrali, a sieć — uczona na 24 konfiguracjach —
utrzyma jakość.

**Hipotezy nie udało się przetestować, bo manipulacja nie zadziałała.** Liczby
ramek okazały się praktycznie identyczne na wszystkich czterech prędkościach:

| Ziarno | 125 k | 250 k | 500 k | 1000 k |
|---|---|---|---|---|
| 100 | 42 805 | 42 978 | 42 933 | 42 894 |
| 103 | 55 419 | 55 516 | 55 362 | 55 298 |

Przyczyna: generator wysyła ramki w **stałych odstępach czasowych**, niezależnie
od prędkości magistrali, a przy 20 konfiguracjach nawet 125 kbit/s ma zapas
przepustowości. Żadna ramka nie została opóźniona ani zgubiona, więc **reżim
próbkowania był identyczny** — a to on decyduje o statystykach, na których
pracuje klasyfikator.

Wniosek metodologiczny: **właściwą zmienną do testu odporności jest obciążenie
magistrali (liczba CAN ID / okresy ramek), a nie prędkość transmisji.** To
zadanie otwarte.

### 3.2. Co eksperyment mimo to dał — moc statystyczna

24 zbiórki × ~120 pozycji = **2936 pozycji, 424 z flagami**, wszystkie
z prawdziwego łańcucha MCP2515 → SocketCAN. To 22× więcej niż wcześniejszy test
na 132 pozycjach — i to wystarczyło, by rozstrzygnąć pytanie.

### 3.3. Wynik

| Metoda | TP | FP | FN | Recall | Precision | F1 |
|---|---|---|---|---|---|---|
| reguła, limit 6 bitów (obecna) | 372 | 22 | 52 | 87,7 % | 94,4 % | 91,0 % |
| reguła, limit 7 bitów | 413 | 24 | 11 | 97,4 % | 94,5 % | 95,9 % |
| **sieć neuronowa (577 par.)** | **424** | **10** | **0** | **100 %** | **97,7 %** | **98,8 %** |

Rozbicie na prędkości magistrali (płaskie, zgodnie z 3.1):

| Bitrate | reguła-6 F1 | reguła-7 F1 | sieć F1 |
|---|---|---|---|
| 125 k | 91,7 % | 95,8 % | 98,6 % |
| 250 k | 91,2 % | 96,3 % | 99,1 % |
| 500 k | 90,6 % | 96,3 % | 99,1 % |
| 1000 k | 90,3 % | 95,4 % | 98,6 % |

**Sieć wygrywa jednoznacznie**: +7,8 pp F1 wobec obecnej reguły i +2,9 pp wobec
najlepszego wariantu ręcznego. Osiąga przy tym **zerowy błąd pominięcia**
(wszystkie 424 flagi wykryte) przy jednocześnie **najlepszej precyzji** — czyli
nie jest to kompromis „więcej wykryć kosztem fałszywych alarmów", tylko poprawa
na obu osiach naraz.

### 3.4. Sprostowanie wcześniejszego wniosku

Na wcześniejszym, małym teście (132 pozycje, ziarno 999) reguła z limitem 7
wypadła lepiej niż sieć (F1 95,0 % wobec 93,0 %) i zapisano wtedy wniosek, że
„jednolinijkowa zmiana parametru bije sieć neuronową". **Ten wniosek był
przedwczesny i jest niniejszym obalony.** Różnica opierała się na dwóch
pozycjach — czyli na szumie. Przy 22× większej próbie kierunek jest odwrotny
i jednoznaczny.

Jest to zarazem argument metodologiczny na przyszłość: przy 20 pozytywach
w zbiorze testowym różnice rzędu kilku punktów procentowych **nie są
interpretowalne**.

---

## 3b. Eksperyment 4.9 — właściwy test odporności (zmienna: obciążenie magistrali)

Powtórzenie testu odporności z **poprawioną zmienną**: zamiast prędkości
transmisji (która w 4.8 nic nie zmieniła) manipulowano **liczbą CAN ID** —
5, 10, 40 i 60, przy stałych 250 kbit/s. Punkt uczenia sieci: 30 CAN ID.
4 ziarna (100-103) na poziom, 100 s każde, wszystko na prawdziwym MCP2515.

Manipulacja zadziałała na poziomie ruchu: liczba ramek wzrosła ~20× (z 5-15 tys.
przy 5 CAN ID do 114-138 tys. przy 60), zgubionych ramek 0.

### 3b.1. Wynik

| Metoda | 5 ID | 10 ID | 40 ID | 60 ID | rozstęp |
|---|---|---|---|---|---|
| reguła, limit 6 | 89,5 % | 92,3 % | 90,6 % | 92,0 % | 2,8 pp |
| reguła, limit 7 | 97,6 % | 97,1 % | 96,6 % | 96,0 % | **1,6 pp** |
| sieć neuronowa | 97,6 % | **100 %** | 98,7 % | 98,0 % | 2,4 pp |

(wartości F1)

Łącznie — 2742 pozycje, 426 flag:

| Metoda | TP | FP | FN | Recall | Precision | F1 |
|---|---|---|---|---|---|---|
| reguła, limit 6 | 368 | 11 | 58 | 86,4 % | 97,1 % | 91,4 % |
| reguła, limit 7 | 410 | 15 | 16 | 96,2 % | 96,5 % | 96,4 % |
| **sieć neuronowa** | **425** | 13 | **1** | **99,8 %** | 97,0 % | **98,4 %** |

**Przewaga sieci potwierdzona niezależnie** na drugim, w pełni świeżym zbiorze
(2742 pozycje, inne poziomy obciążenia): +2,0 pp F1 wobec najlepszego wariantu
ręcznego, +7,0 pp wobec obecnego.

### 3b.2. Hipoteza o odporności — ODRZUCONA

Hipoteza mówiła, że reguła, nastrojona w jednym punkcie pracy, będzie degradować
się poza nim szybciej niż sieć uczona na wielu konfiguracjach. **Dane tego nie
potwierdzają.** Wszystkie trzy metody okazały się bardzo stabilne przy 20-krotnej
zmianie natężenia ruchu, a **najstabilniejsza była reguła z limitem 7**
(rozstęp 1,6 pp wobec 2,4 pp sieci).

Wniosek: **przewaga sieci bierze się z lepszej klasyfikacji, nie z większej
odporności.** Uzasadnieniem dla uczenia maszynowego jest tu wyłącznie
przełamanie sufitu konstrukcyjnego reguły — i tego uzasadnienia nie należy
rozszerzać na argumenty, których pomiar nie potwierdza.

### 3b.3. Dlaczego dwie próby testu odporności z rzędu nie zadziałały

Mediana liczby próbek na bajt pozostała w przedziale ~1000-2000 na **wszystkich**
poziomach obciążenia, mimo 20-krotnej zmiany łącznego natężenia ruchu. Przyczyna
jest strukturalna: w generatorze **każdy CAN ID nadaje z własnym, stałym
okresem**. Dodanie kolejnych CAN ID zwiększa łączny ruch, ale **nie zmienia
reżimu próbkowania pojedynczego bajtu** — a to on decyduje o statystykach
`changed_pairs` / `big_jumps`, na których pracują oba klasyfikatory. Tak samo
było z prędkością magistrali w 4.8.

**Metodologiczne ustalenie**: statystyki per-bajt są w tym generatorze
**niewrażliwe na parametry poziomu magistrali** (prędkość, liczba węzłów).
Żeby realnie przetestować odporność, trzeba skalować **same okresy ramek** —
czego generator nie udostępnia jako parametru — albo użyć ruchu z prawdziwego
pojazdu. To jest właściwe zadanie otwarte, a nie kolejna odmiana tego samego
testu.

---

## 3c. Wdrożenie sieci w demonie obserwacyjnym

`esp_experiment_4_7_nn/pi_observer_nn.py` — demon liczący **oba klasyfikatory
równolegle** (regułę i sieć), wywodzący się z `pi_continuous_observer.py`.

### 3c.1. Trzy decyzje projektowe

1. **Zero PyTorcha w demonie.** Wagi eksportowane do JSON (12,4 KB), inferencja
   to kilkanaście linii czystego Pythona. Wdrożenie **nie dokłada ani jednej
   zależności** — zamiast 1,3 GB instalacji i kilku sekund importu przy każdym
   starcie.
2. **Pamięć pozostaje ograniczona.** Histogram wartości to tablica o stałej
   długości 256 (`array("I")`, ~1 KB na bajt), nie rosnąca struktura. Kluczowa
   własność oryginału — O(1) na ramkę, pamięć niezależna od długości przebiegu —
   jest zachowana.
3. **Reguła pozostaje domyślna.** Sieć jest dodatkiem; werdykt zawiera oba
   rozstrzygnięcia, więc można je porównywać na żywo bez uruchamiania dwóch
   demonów na jednej magistrali.

### 3c.2. Weryfikacja równoważności

Implementację w czystym Pythonie porównano z PyTorchem na 132 pozycjach:
**maksymalna różnica logitu 2,66·10⁻⁶** (zaokrąglenie float32/float64),
**zero rozbieżnych decyzji**.

### 3c.3. Zmierzony narzut (na żywym ruchu, 44 171 ramek w 60 s, 320 pozycji)

| Operacja | Koszt | Kiedy |
|---|---|---|
| akumulacja stanu | 152,7 µs/ramkę (11,2 % czasu) | każda ramka |
| policzenie 10 cech | 34,8 µs/pozycję | raz na snapshot |
| inferencja sieci | 282,4 µs/pozycję | raz na snapshot |
| **razem werdykt** | **317,3 µs/pozycję** | raz na snapshot |

Pełny snapshot 320 pozycji kosztuje ~0,1 s i wykonuje się raz na 30-60 s —
narzut pomijalny. Kosztem dominującym pozostaje akumulacja stanu (11,2 % jednego
rdzenia przy 736 ramkach/s), zbliżona do oryginalnego demona.

---

## 3d. Weryfikacja rekomendacji „limit 6 → 7" na dwóch niezależnych korpusach

Rekomendacja podniesienia górnego limitu liczby bitów wymagała potwierdzenia
poza jednym zbiorem. Sprawdzono ją na **dwóch korpusach zebranych osobno**,
przy różnych parametrach magistrali, oba z prawdziwego MCP2515:

| Korpus | limit 6 | limit 7 | limit 8 | zysk 6→7 |
|---|---|---|---|---|
| Eksperyment 4.8 (prędkości), n=2936 | 91,0 % | 95,9 % | 64,4 % | **+5,0 pp** |
| Eksperyment 4.9 (obciążenia), n=2742 | 91,4 % | 96,4 % | 65,3 % | **+4,9 pp** |
| **rozbieżność między korpusami** | 0,5 pp | 0,4 pp | 0,9 pp | **0,1 pp** |

(wartości F1)

**Rekomendacja potwierdzona.** Zysk wynosi ~5 pp F1 i jest praktycznie
identyczny na obu korpusach (różnica 0,1 pp). Załamanie przy limicie 8 również
odtwarza się zgodnie (64,4 % / 65,3 %), co potwierdza, że jest to własność
metody, a nie artefakt jednego zbioru.

**Do wdrożenia**: zmiana `bit_count > 6` na `bit_count > 7` w
`esp_experiment_4_3/etap_b_autolabel.py`, `src/core/DecodingAccuracyRunner.cpp`
oraz `esp_experiment_4_5_rpi/pi_continuous_observer.py`. Koszt obliczeniowy
zerowy, ryzyko regresji znikome (precyzja spada o ~0,1-0,6 pp przy wzroście
wykrywalności o ~10 pp).

---

## 3e. Eksperyment 4.10 — test odporności, który wreszcie zadziałał

Po dwóch nieudanych próbach (4.8: prędkość magistrali, 4.9: liczba CAN ID)
ustalono, że właściwą zmienną jest **skalowanie samych okresów nadawania**.
Generator rozszerzono o parametr `--period-scale` (domyślnie 1,0 — zachowanie
niezmienione, ground truth dla danego ziarna pozostaje bit-w-bit identyczny,
co zweryfikowano).

Skale: **0,25× / 0,5× / 2× / 4×** wobec punktu uczenia 1,0. 4 ziarna (100-103),
20 CAN ID, 250 kbit/s, 100 s każde, wszystko na prawdziwym MCP2515.

**Manipulacja tym razem zadziałała**: mediana liczby próbek na bajt zmieniła się
od **7481** (skala 0,25) do **498** (skala 4,0) — zakres **15×**, przy zerze
zgubionych ramek. To realna zmiana reżimu próbkowania, czyli dokładnie tego,
na czym pracują oba klasyfikatory.

### 3e.1. Wynik

| Metoda | 0,25× | 0,5× | 1,0×* | 2× | 4× | rozstęp |
|---|---|---|---|---|---|---|
| reguła, limit 6 | 89,6 % | 90,4 % | 91,4 % | 88,9 % | 88,9 % | 2,5 pp |
| reguła, limit 7 | 95,8 % | 95,8 % | 96,4 % | 95,2 % | 95,8 % | **1,2 pp** |
| sieć neuronowa | 98,6 % | 97,9 % | 98,4 % | 98,6 % | **99,3 %** | 1,4 pp |

(wartości F1; * punkt uczenia — wartość odniesienia z Eksperymentu 4.9)

Łącznie, z pominięciem punktu uczenia — 1944 pozycje, 284 flagi:

| Metoda | TP | FP | FN | Recall | Precision | F1 |
|---|---|---|---|---|---|---|
| reguła, limit 6 | 241 | 14 | 43 | 84,9 % | 94,5 % | 89,4 % |
| reguła, limit 7 | 276 | 17 | 8 | 97,2 % | 94,2 % | 95,7 % |
| **sieć neuronowa** | **284** | 8 | **0** | **100 %** | 97,3 % | **98,6 %** |

### 3e.2. Hipoteza o odporności — ODRZUCONA po raz trzeci, tym razem rozstrzygająco

Wcześniejsze odrzucenia (4.8, 4.9) były **nierozstrzygające**, bo manipulacje
nie zmieniały statystyk. Tutaj zmieniły je 15-krotnie — i mimo to:

- **wszystkie trzy metody są bardzo stabilne** (rozstępy 1,2-2,5 pp),
- **najstabilniejsza pozostaje reguła** z limitem 7 (1,2 pp wobec 1,4 pp sieci),
- sieć utrzymuje **100 % wykrywalności na każdej skali**, a jej przewaga
  (+2,9 pp F1 nad regułą-7) jest praktycznie stała.

**Wniosek ostateczny: przewaga sieci wynika wyłącznie z jakości klasyfikacji,
nie z większej odporności.** Statystyki `changed_pairs`/`big_jumps`, na których
pracują oba klasyfikatory, okazały się niezwykle odporne na warunki pracy
magistrali — co samo w sobie jest wartościowym wynikiem: **metoda „Kierunku B"
nie wymaga strojenia pod konkretny punkt pracy.**

### 3e.3. Bilans trzech prób testu odporności

| Eksperyment | Zmienna | Czy zmieniła statystyki? | Wynik |
|---|---|---|---|
| 4.8 | prędkość magistrali (125-1000 kbit/s) | **nie** | nierozstrzygający |
| 4.9 | liczba CAN ID (5-60) | **nie** | nierozstrzygający |
| **4.10** | **skala okresów ramek (0,25-4×)** | **tak, 15×** | **rozstrzygający** |

Dwie pierwsze próby były pomyłką projektową: manipulowały parametrami **poziomu
magistrali**, podczas gdy statystyki per-bajt zależą od **stosunku częstotliwości
próbkowania do dynamiki sygnału**. Dopiero trzecia zmienna to zmienia.

---

## 4. Wnioski

1. **Sieć neuronowa przełamuje sufit konstrukcyjny reguły** — 100 % Recall przy
   97,3-97,7 % Precision, wobec ~87 % / ~94 % obecnego rozwiązania. Robi to
   modelem o 577 parametrach, uczonym w 21,8 s na urządzeniu brzegowym.
   Przewaga potwierdzona na **trzech niezależnych zbiorach sprzętowych**
   (4.8, 4.9, 4.10 — łącznie ~7600 pozycji) i utrzymuje się na każdym
   testowanym punkcie pracy: +2,9 pp F1 nad najlepszym wariantem ręcznym.
2. **Jeśli sieć jest z jakiegoś powodu niepożądana**, sama zmiana limitu bitów
   z 6 na 7 daje +4,9 pp F1 przy zerowym koszcie obliczeniowym i jednej linii
   zmiany w trzech plikach (`etap_b_autolabel.py`, `DecodingAccuracyRunner.cpp`,
   `pi_continuous_observer.py`).
3. **Mocniejszy sprzęt nie jest potrzebny** — ani do uczenia, ani do wdrożenia.
4. **Metoda „Kierunku B" nie wymaga strojenia pod punkt pracy.** Trzy niezależne
   manipulacje — prędkość magistrali (8×), liczba węzłów (12×) i gęstość
   próbkowania (15×) — zmieniły F1 wszystkich metod o najwyżej 2,5 pp. To mocny
   argument praktyczny: raz nastrojony klasyfikator działa w szerokim zakresie
   warunków bez ponownej kalibracji.
5. **Hipoteza o większej odporności sieci została odrzucona rozstrzygająco**
   (3e.2). Uzasadnieniem dla uczenia maszynowego jest tu wyłącznie wyższa
   jakość klasyfikacji — i tak należy je przedstawiać.

---

## 5. Zastrzeżenia

1. **Cały ruch jest syntetyczny**, z generatora Eksperymentu 4.3. Test na innych
   ziarnach ogranicza ryzyko zapamiętywania, ale inne ziarno to wciąż ten sam
   generator. Wynik jest mocną przesłanką, **nie dowodem** na prawdziwej
   magistrali samochodowej.
2. Test odporności poza punktem strojenia **nie został przeprowadzony** —
   wybrana zmienna (bitrate) okazała się nieskuteczna (sekcja 3.1).
3. Sieć uczona na `vcan`, testowana na sprzęcie — zgodność rozkładów
   zweryfikowano regułą ręczną (sekcja 2.2), ale tylko na jednym ziarnie (999).

---

## 6. Zadania otwarte

1. Porównanie trzech metod (reguła / retrieval Qdrant / sieć) na jednym
   korpusie i jedną metryką — zaplanowane osobno.
2. Decyzja, czy sieć ma zastąpić regułę jako domyślny klasyfikator. Argumentem
   ZA jest wyłącznie wyższa jakość (+2,0 pp F1 wobec reguły-7, potwierdzone na
   dwóch niezależnych zbiorach po ~2800 pozycji). Argumentem PRZECIW —
   dodatkowy plik modelu, 10 cech zamiast 2 i nieco większy koszt akumulacji
   stanu. **Hipoteza o większej odporności sieci została odrzucona** (3b.2),
   więc nie może być użyta jako uzasadnienie.

3. **Walidacja na ruchu z prawdziwego pojazdu** — jedyne zastrzeżenie, którego
   ta sesja nie zdjęła. Cały ruch pozostaje syntetyczny.

**Zrealizowane w trakcie tej sesji** (pierwotnie na tej liście):
wdrożenie sieci w demonie z pomiarem narzutu (3c), weryfikacja „limit 6 → 7"
na dwóch niezależnych korpusach (3d), test odporności z właściwą zmienną (3e).

---

## 7. Pliki

- `esp_experiment_4_7_nn/build_dataset.py` — cechy per-bajt + wariant offline
- `esp_experiment_4_7_nn/collect_live.py` — zbieranie z SocketCAN (surowe gniazdo)
- `esp_experiment_4_7_nn/train_nn.py` — uczenie i porównanie z regułą
- `esp_experiment_4_7_nn/analyze_e48.py` — analiza wyników 4.8 (na płytce)
- Dane: `/root/proj/nn_data/` (30 ziaren vcan), `/root/proj/data/e48_*.json`
  (24 zbiórki sprzętowe), model `/root/proj/data/model_47.pt`

### Uwagi wykonawcze (dwa błędy warte zapamiętania)

1. **Zmiana bitrate potrafi się udać tylko po jednej stronie** — znacznik `sudo`
   jest przypisany do terminala i wygasa niezauważenie. Skrypt musi odczytać
   prędkość z obu urządzeń i przerwać przy rozbieżności.
2. **Kolektor uruchomiony przez `nohup ... &` po SSH kończy się natychmiast**
   (proces nie żyje po 4 s mimo zadanych 20) i **zapisuje poprawnie wyglądający
   plik z zerem ramek**. Ten sam kolektor na pierwszym planie działa. Błąd był
   groźny, bo nie wyglądał na błąd — wychwyciło go dopiero to, że wszystkie
   cztery prędkości dały identyczne zero, co fizycznie nie miało sensu.
