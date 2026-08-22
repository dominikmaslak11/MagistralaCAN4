# Katalog wszystkich eksperymentów projektu CAN-Edge-AI

Data: 2026-08-20
Zakres: lipiec – sierpień 2026
Przeznaczenie: **dokument podręczny na spotkanie** — do szybkiego odnalezienia
konkretnego eksperymentu, liczby albo wniosku.

Wszystkie liczby pochodzą z raportów w repozytorium, nie z pamięci.

---

## Spis eksperymentów

| Nr | Nazwa | Status | Wynik |
|---|---|---|---|
| 1.1 | Cold Start Latency | zakończony | pozytywny (potwierdził tezę) |
| 1.2 | Hot Execution Latency | zakończony, zweryfikowany niezależnie | pozytywny |
| 2.1 | CAN Frame Throughput | zakończony | pozytywny |
| 2.2 | Buffer Overflow Threshold | zakończony | pozytywny + znaleziona luka |
| 3.1 | Zasięg radiowy WiFi/BLE | **przygotowany, NIE zmierzony** | — |
| 4.1 | Decoding Accuracy (5 wariantów) | zakończony | 4 negatywne, 1 pozytywny |
| 4.3 | Bootstrapped Fine-Tuning | Etapy A–C zrobione, **D wstrzymany** | mieszany |
| 4.4 | Qdrant retrieval warm-start | zakończony | **negatywny** |
| 4.5 | Ciągła obserwacja (RPi Zero W) | zakończony | pozytywny |
| 4.6 | Replikacja na Orange Pi Zero 3 | zakończony | pozytywny |
| 4.7–4.10 | Sieć neuronowa kontra reguła | zakończony | pozytywny + 3 hipotezy odrzucone |
| 4.11–4.12 | Klasyfikacja maski bitowej | zakończony | pozytywny, **po korekcie własnego wyniku** |
| 5.1 | Profilowanie CPU/RAM, JTAG, OTA | zakończony | pozytywny + wynik metodologiczny |

---

# GRUPA 1 — czasy reakcji

## Eksperyment 1.1 — Cold Start Latency

**Pytanie:** ile trwa cały cykl od pojawienia się nieznanej ramki do gotowej
reguły dekodowania, i która składowa dominuje?

**Metoda:** realny sprzęt ESP32+MCP2515 → WiFi/WebSocket → aplikacja C++/Qt6
(`ColdStartDetector`, `LlmQueryClient`, `LatencyProfiler`). N = 30 na model.

**Wyniki:**

| Model | t_llm [ms] | T_total [ms] |
|---|---|---|
| Claude Sonnet 5 | 4741 ± 1305 | 5772 ± 1726 |
| GPT-5.6-sol | 4889 ± 1111 | 5866 ± 1251 |
| Gemini-3.6-flash | 8522 ± 3321 | 9488 ± 3216 |
| DeepSeek-v4-pro | 18642 ± 1352 | 19550 ± 1502 |

**Wniosek:** czas wnioskowania LLM stanowi **ponad 50 %** czasu całkowitego
u każdego modelu. To uzasadnia skupienie dalszej pracy na **trafności** LLM,
a nie na optymalizacji sprzętu.

---

## Eksperyment 1.2 — Hot Execution Latency

**Pytanie:** ile trwa reakcja na ramkę, gdy reguła jest już znana — i od czego
ten czas zależy?

**Metoda:** pomiar `t_resp` od przerwania sprzętowego MCP2515 (pin INT) do
reakcji GPIO. N = 1000 na wariant.

**Wyniki — wpływ zegara i prędkości magistrali:**

| Wariant | t_resp [µs] | odch. std. |
|---|---|---|
| Bazowy (kwarc 8 MHz, 250 kbit/s) | 109,70 | 1,52 |
| Kwarc 16 MHz, 250 kbit/s | 109,67 | 1,53 |
| 125 kbit/s | 109,66 | 1,51 |
| 500 kbit/s | 109,72 | 1,56 |
| 1000 kbit/s | 109,66 | 1,47 |
| **Filtr sprzętowy MCP2515 + reakcja w ISR** | **1,01** | **0,10** |

**Wnioski:**
1. **Zegar MCP2515 nie ma znaczenia** (8 → 16 MHz: różnica 0,03 µs, mniejsza niż
   błąd standardowy). Sterownik przelicza prescaler tak, by czas bitu na
   magistrali pozostał ten sam.
2. **Prędkość magistrali nie ma znaczenia** (125–1000 kbit/s, rozpiętość 8×:
   wszystko w przedziale 109,66–109,72 µs). Bo `t_resp` liczy się **po** pełnym
   odebraniu ramki — odbiór zależy od bitrate, ale nie wchodzi do mierzonego okna.
3. **Filtr sprzętowy skraca czas 100×** (109,7 → 1,01 µs), kosztem architektury:
   reakcja musi wtedy nastąpić w przerwaniu.

**Weryfikacja niezależna** (Dodatek A artykułu):

| Metoda | Średnia | Odch. std. | N |
|---|---|---|---|
| Timer wewnętrzny ESP32 | 109,70 µs | 1,52 µs | 1000 |
| **Analizator logiczny (zewnętrzny)** | **112,45 µs** | 1,54 µs | 570 |

Różnica 2,75 µs (~2,5 %) mieści się w rozdzielczości obu metod. Wcześniej dwie
próby weryfikacji zakończyły się niepowodzeniem: oscyloskop Hantek 1008C
(zawodne wyzwalanie programowe) i analizator DL16 (trwale niesprawny firmware).
**To jest przykład wyniku potwierdzonego dwiema niezależnymi metodami.**

---

# GRUPA 2 — przepustowość i bufory

## Eksperyment 2.1 — CAN Frame Throughput

**Pytanie:** przy jakiej liczbie ramek na sekundę ESP32 zaczyna gubić pakiety?

**Metoda:** generator PEAK PCAN-USB, kroki co 100 ramek/s od 100 do 5000
(50 punktów pomiarowych), po 2 s na krok.

**Wyniki:**

| Prędkość | Zakres testu | Sufit magistrali | Straty (FLR) |
|---|---|---|---|
| 250 kbit/s | 100–5000 ramek/s | ~2260 ramek/s | **0,00 %** |
| 500 kbit/s | 100–5000 ramek/s | ~4365 ramek/s | **0,00 %** |

**Wniosek:** ESP32 **nie zgubił ani jednej ramki** w całym badanym zakresie,
na obu prędkościach. Sprzętowy licznik przepełnień bufora RX MCP2515 pozostał
na zerze. **Wąskim gardłem okazała się sama magistrala, nie mikrokontroler** —
osiągnięto fizyczny sufit przepustowości CAN, zanim ESP32 zdradził jakiekolwiek
oznaki przeciążenia.

---

## Eksperyment 2.2 — Buffer Overflow Threshold

**Pytanie:** przy jakiej częstotliwości i jakim rozmiarze bufora dochodzi do
utraty danych, gdy system czeka na odpowiedź LLM (~2,2 s)?

**Wyniki — potwierdzona empirycznie zależność `T = bufor / częstotliwość`:**

| Bufor [ramek] | Częstotliwość | Próg teoretyczny | Zmierzony max bezpieczny |
|---|---|---|---|
| 4 | 100 Hz | 40 ms | 40 ms |
| 4 | 1000 Hz | 4 ms | 6 ms |
| 8 | 500 Hz | 16 ms | 16 ms |
| 16 | 500 Hz | 32 ms | 32 ms |
| 16 | 1000 Hz | 16 ms | 16 ms |

**Znaleziona luka (istotna):** obecna architektura **nie ma bufora
aplikacyjnego** — każda ramka jest natychmiast przekazywana przez WebSocket.
Dla oczekiwania ~2,2 s przy 1000 Hz potrzeba bufora ~2200 ramek (~35 KB).
ESP32 ma ~270 KB wolnej pamięci, więc **jest to brak w oprogramowaniu, nie
ograniczenie sprzętu**.

---

# GRUPA 3 — łączność

## Eksperyment 3.1 — zasięg radiowy WiFi/BLE

**Status: PRZYGOTOWANY, POMIAR TERENOWY NIE WYKONANY.**

**Co jest gotowe:**
- firmware ESP32 jako własny punkt dostępowy (SoftAP), protokół ping-pong UDP —
  mierzy surowy zasięg radia, niezależnie od routera pośredniczącego,
- **pierwsza w projekcie aplikacja mobilna** (`android_experiment_3_1/`, Kotlin):
  mierzy RTT na zegarze telefonu (nie wymaga synchronizacji zegarów), RSSI oraz
  GPS jako niezależną weryfikację dystansu (z udokumentowanym ograniczeniem
  dokładności 3–5 m, gorszym w scenariuszu NLOS),
- serwer odbiorczy, zapis lokalny do CSV jako źródło prawdy,
- aplikacja zbudowana, APK debug kompiluje się poprawnie.

**Dlaczego pomiar nie ruszył:** przed napisaniem kodu spisano **10 otwartych
kwestii metodologicznych** do decyzji wykładowcy
(`Pytania_Do_Wykladowcy_Eksperyment_3.1_20260729.md`) — m.in. architektura testu
(SoftAP kontra router pośredniczący), role fizyczne urządzeń, oraz zredukowana
liczebność próby (proponowane 1000–2000 zamiast 10 000 pakietów na punkt).

**To jest miejsce, w którym projekt czeka na decyzję, a nie raportuje wynik.**

---

# GRUPA 4 — trafność LLM (trzon projektu)

## Eksperyment 4.1 — Decoding Accuracy, pięć wariantów

**Pytanie:** czy model językowy potrafi odgadnąć regułę dekodowania sygnału
z surowych ramek CAN?

### 4.1a — Baseline zero-shot, cztery modele (N = 100 na model)

| Model | Wszystkie sygnały (10) | **Flagi bitowe (5)** |
|---|---|---|
| GPT-5.6-sol | 67,9 % | 35,8 % |
| Gemini-3.6-flash | 50,9 % | 3,0 % |
| Claude Sonnet 5 | 47,9 % | **0,0 %** |
| DeepSeek-v4-pro | 41,5 % | 6,7 % |

Sygnały ciągłe: 79–100 % u wszystkich modeli. **Claude nie wykrył ani jednej
flagi w stu próbach.**

### 4.1b — Interwencje promptowe (WYNIK NEGATYWNY)

| Wariant | Wszystkie sygnały | Flagi bitowe |
|---|---|---|
| Zero-shot (odniesienie) | 47,9 % | 0,0 % |
| Few-shot | 46,2 % | **0,0 %** |
| Entropy-analysis | 46,7 % | **0,0 %** |

Obie interwencje **nieznacznie pogorszyły** średnią ogólną i **nie zmieniły
detekcji flag ani o punkt**.

### 4.1c — Naprawa zanieczyszczenia kontekstu (WYNIK NEGATYWNY, NIEOCZEKIWANY)

Naprawiono architektonicznie poprawny błąd: kontekst `recentFrames` faktycznie
zawierał wyłącznie ramki właściwego CAN ID.

| Model | Średnia przed → po | Δ | Flagi przed → po | Δ |
|---|---|---|---|---|
| Claude Sonnet 5 | 47,9 % → 47,4 % | −0,6 pp | 0,0 % → 0,0 % | 0,0 pp |
| **GPT-5.6-sol** | 67,9 % → 43,8 % | **−24,1 pp** | 35,8 % → **0,0 %** | **−35,8 pp** |
| DeepSeek-v4-pro | 41,5 % → 38,8 % | −2,7 pp | 6,7 % → 3,0 % | −3,6 pp |
| Gemini-3.6-flash | 50,9 % → 50,7 % | −0,2 pp | 3,0 % → 6,1 % | +3,0 pp |

Liczba prób, w których GPT-5.6-sol w ogóle zaproponował dekompozycję bajtu na
≥ 2 sygnały, spadła z **12/33 do 0/33**.

**Robocza hipoteza:** przypadkowy szum z innych CAN ID (przed naprawą) zwiększał
pozorną złożoność danych i skłaniał model do rozważenia bardziej złożonej
interpretacji. „Czysty" kontekst wyglądał na prostszy, więc model proponował
prostsze rozwiązanie.

### 4.1d — Podsumowanie wyników negatywnych

**Cztery niezależne interwencje** — przykłady, wymuszone procedury analityczne,
poprawa jakości danych wejściowych — dały **identyczne 0,0 %** u Claude.
Problem nie leży w tym, *co* mówimy modelowi ani jak czysty jest kontekst.

### 4.1e — Hybrydowy override (PIERWSZY WYNIK POZYTYWNY)

| | Surowy LLM | LLM + override |
|---|---|---|
| Średnia (10 sygnałów) | 50,0 % | **96,7 %** |
| Flagi bitowe (5 sygnałów) | F1 = 0 | **F1 = 1,000** |
| Sygnały ciągłe | — | **bez pogorszenia** |

Szczegóły mechanizmu: `Override_Hybrydowy_Wyjasnienie_20260820.md`.

**Błąd w wersji 1**, znaleziony w trakcie badań: heurystyka wymagała tylko
jednego przełączającego się bitu i fałszywie klasyfikowała szerokozakresowe
skalary jako flagi — **29 z 34 prób** na CAN ID 0x100. Naprawiono dodając
warunek 2–6 bitów.

---

## Eksperyment 4.3 — Bootstrapped Fine-Tuning

**Pytanie:** czy klasyfikator, który już działa, może posłużyć jako
**automatyczny nauczyciel** do douczenia modelu językowego? (destylacja
symboliczno-neuronowa — „nauczycielem" jest algorytm klasyczny, nie inna sieć)

| Etap | Zakres | Status |
|---|---|---|
| **A** | rozbudowa generatora ruchu (wiele mini-DBC, bajty mieszane, różne dynamiki) | zrobiony |
| **B** | automatyczne etykietowanie korpusu klasyfikatorem | zrobiony |
| **C** | format danych treningowych | zrobiony |
| **D** | właściwy fine-tuning | **WSTRZYMANY** |

**Kluczowy wynik Etapu B — klasyfikator na szerszym korpusie:**

| Metryka | Wąski mini-DBC (4.1) | **Szeroki korpus (4.3)** |
|---|---|---|
| Skuteczność ogólna | ~97–100 % | Precision 82,6 %, **Recall 55,9 %**, F1 66,7 % |
| Trafność maski (które bity) | nie mierzona | **15,8 %** |

**Dlaczego Etap D został wstrzymany:** ~44 % przykładów z flagami w zbiorze
treningowym byłoby błędnie oznaczonych. Zdiagnozowano dwie przyczyny:
(a) za krótkie okno obserwacji, (b) bajty mieszane (flaga + skalar w jednym
bajcie). **Pytanie do wykładowcy z 6 sierpnia:** naprawiać klasyfikator, czy
uczyć mimo szumu?

*Obie przyczyny zostały naprawione w eksperymentach 4.5–4.12 — Etap D jest
dziś odblokowany.*

---

## Eksperyment 4.4 — Qdrant retrieval warm-start (WYNIK NEGATYWNY)

**Pytanie:** czy podpowiedź z bazy wektorowej — „ten sygnał wygląda jak sygnały,
które już rozpoznaliśmy" — poprawi trafność modelu?

**Metoda:** 7-wymiarowy wektor cech behawioralnych na sygnał, embedded Qdrant,
100 realnych okien Cold Start z żywej magistrali, 4 modele LLM.

**Wyniki:**

| Model | Baseline | Warm-start | Δ |
|---|---|---|---|
| Claude Sonnet 5 | 43,0 % | 42,8 % | −0,3 pp |
| GPT-5.6-sol | 33,2 % | 32,2 % | −1,0 pp |
| DeepSeek-v4-pro | 29,6 % | 32,1 % | **+2,4 pp** |
| Gemini-3.6-flash | 41,5 % | 37,2 % | **−4,3 pp** |

**W rozbiciu na typ sygnału:**

| Typ | n | Baseline | Warm-start | Δ |
|---|---|---|---|---|
| scalar | 24 | 70,3 % | 73,3 % | +3,0 pp |
| **bit_flag** | **116** | **17,1 %** | **13,5 %** | **−3,5 pp** |
| partial_scalar | 4 (mała próba) | 87,5 % | 100,0 % | +12,5 pp |

**Wniosek:** podpowiedź zbudowana z **innego przebiegu** miała ~70 % trafności
i **pogarszała** wyniki na flagach bitowych — czyli tam, gdzie pomoc była
najbardziej potrzebna. Bywało, że **utwierdzała model w istniejącym błędzie**
(regres do −5,2 pp).

Dodatkowe ustalenie techniczne: **516 z 799 podpowiedzi (64,6 %)** dotyczyło
bajtów niezawierających żadnego sygnału (padding). Po odfiltrowaniu trafność
realnych podpowiedzi wyniosła 71,4 %.

---

## Eksperyment 4.5 — ciągła obserwacja (Raspberry Pi Zero W)

**Pytanie:** czy słabości z 4.3 i 4.4 wynikały z **metody**, czy tylko ze **zbyt
krótkiego okna obserwacji** epizodycznej architektury ESP32?

**Metoda:** demon Python nasłuchujący `can0` godzinami, statystyki budowane
**przyrostowo** (O(1) na ramkę). Przebieg: **1 h, ~1,6 mln ramek**.

**Wyniki:**
- Faza 1 (klasyfikator ciągły): po strojeniu progu **Recall 85 %, Precision 100 %**
- Faza 2 (embedded Qdrant na żywo): **91,6 % / 91,6 %** within-corpus
- Faza 5 (embeddingi neuronowe MiniLM): **100 % cross-corpus** — ale wyłącznie
  offline na laptopie, bo PyTorch nie ma pakietów dla ARMv6

**Strojenie progu:** domyślne 0,5 ucinało 5 z 20 prawdziwych flag bez korzyści
w precyzji. Przyjęto **0,3**.

---

## Eksperyment 4.6 — replikacja na Orange Pi Zero 3

**Pytanie:** czy wynik 4.5 jest własnością **metody**, czy konkretnej płytki?

**Metoda:** ten sam kod, inna platforma — Allwinner H618 zamiast Broadcom,
aarch64 zamiast ARMv6, Armbian zamiast Raspberry Pi OS, SPI1 zamiast SPI0.

**Wynik:** **identyczny co do liczby** — Recall 85,0 %, Precision 100 %.
Przy wspólnym progu zbiory wykrytych bajtów pokrywają się w 11 z 12 pozycji.

**Dodatkowo:** Faza 5 po raz pierwszy uruchomiona **na sprzęcie docelowym** —
wyniki zgodne co do ostatniej cyfry z laptopem x86.

**Koszt integracji (uczciwie):** Armbian nie ma gotowego overlaya dla MCP2515;
trzeba było napisać własny. Publikowane w sieci tabele pinoutu są sprzeczne —
rozstrzygnięto oficjalnym manualem i odczytem z device tree.

---

## Eksperymenty 4.7–4.10 — sieć neuronowa kontra reguła

**Pytanie:** reguła ma sufit konstrukcyjny (warunek `bit_count ≤ 6` odrzuca bajty
z 7–8 flagami). Czy uczony klasyfikator go przełamie?

**Wynik na 2936 pozycjach ze sprzętu:**

| Metoda | Recall | Precision | F1 |
|---|---|---|---|
| reguła, limit 6 bitów (obecna) | 87,7 % | 94,4 % | 91,0 % |
| reguła, limit 7 bitów | 97,4 % | 94,5 % | 95,9 % |
| **sieć neuronowa (577 parametrów)** | **100 %** | **97,7 %** | **98,8 %** |

**Trzy hipotezy odrzucone:**

| Próba | Zmienna | Czy zmieniła statystyki? |
|---|---|---|
| 4.8 | prędkość magistrali 125–1000 kbit/s | **nie** — błąd projektowy |
| 4.9 | liczba CAN ID 5–60 | **nie** — błąd projektowy |
| 4.10 | skala okresów ramek 0,25–4× | **tak, 15×** |

Przy działającej manipulacji **hipoteza o większej odporności sieci została
odrzucona**: wszystkie metody stabilne (rozstępy 1,2–2,5 pp), a najstabilniejsza
okazała się **reguła**. Przewaga sieci wynika wyłącznie z jakości klasyfikacji.

**Wycofany wniosek:** na teście 132-pozycyjnym reguła wypadła lepiej niż sieć
i tak to zapisano. Przy próbie 22× większej kierunek się odwrócił — różnica
opierała się na **dwóch pozycjach**, czyli na szumie.

**Rekomendacja potwierdzona na dwóch korpusach:** zmiana limitu bitów 6 → 7 daje
**+5,0 pp i +4,9 pp** F1 (rozbieżność 0,1 pp), przy zerowym koszcie obliczeniowym.

**Koszt wdrożenia:** 577 parametrów, uczenie **68 s** na płytce, inferencja
282 µs, **bez PyTorcha** (wagi w JSON, czysty Python).

---

## Eksperymenty 4.11–4.12 — klasyfikacja maski bitowej

**Pytanie:** wiemy, *który bajt* zawiera flagi. Ale **które bity** nimi są?
(trafność maski z 4.3: **15,8 %**)

**Kluczowe doprecyzowanie problemu:** maska `seen0 & seen1` nie jest „ogólnie
kiepska" — jest **idealna dla bajtów z samymi flagami (100 %) i ma dokładnie
zero trafień dla mieszanych**. Bajty mieszane to ~22 % przypadków.

**Wynik pierwszy (korpus z ciągłymi zakresami bitów):**
maska w bajtach mieszanych **0/30 → 30/30 = 100 %**.

**Weryfikacja (4.12) — i korekta:** sprawdzono, czy sieć nauczyła się prawdziwej
zasady, czy regularności generatora (flagi zawsze na najniższych bitach, skalar
jako ciągły zakres nad nimi). Bity rozproszono i powtórzono pomiar na sprzęcie:

| Wariant | Model | Maska w bajtach mieszanych |
|---|---|---|
| bity ciągłe | uczony na ciągłych | 30/30 = **100 %** |
| **bity rozproszone** | **ten sam model** | **5/37 = 14 %** |
| bity rozproszone | uczony na reprezentatywnych | 22/37 = **59 %** |

**Model nie generalizował. Wynik 100 % był zawyżony przez konstrukcję korpusu.**

**Uczciwy bilans:**

| Metryka | odniesienie | sieć |
|---|---|---|
| Precyzja per bit | 26,8 % | **96,5 %** |
| F1 per bit | 42,3 % | **98,2 %** |
| Maska w bajtach mieszanych | 0 % | **59 %** |

---

# GRUPA 5 — zasoby

## Eksperyment 5.1 — profilowanie CPU/RAM, JTAG, OTA

**Pytanie:** ile zasobów ESP32 zużywa parsowanie ramek?

**Wynik — efekt obserwatora rozłożony na składowe:**

| Konfiguracja | CPU |
|---|---|
| Firmware Arduino (wyjściowy) | **0,60 %** |
| Port na ESP-IDF | 0,82 % |
| Sama **gotowość** kanału trace | 1,79 % |
| Aktywne śledzenie przez JTAG | 2,49 % |

**Wynik metodologicznie ciekawy:** samo przygotowanie kanału pomiarowego —
jeszcze **przed** rozpoczęciem pomiaru — kosztuje więcej niż mierzona praca
(0,82 → 1,79 pp to skok większy niż całe zużycie bazowe). Klasyczny problem
„obserwator wpływa na obserwowane zjawisko".

Jako wartość odniesienia przyjęto **0,6 %** — pomiar najmniej inwazyjny.

**Zrealizowano też trzeci stan metodyki: aktualizacja OTA.**

---

# Przekrojowe wnioski

1. **Sprzęt nie jest wąskim gardłem.** Zero zgubionych ramek do fizycznego sufitu
   magistrali, 110 µs reakcji, 0,6 % CPU. Wąskim gardłem jest wnioskowanie LLM:
   **5 sekund kontra 110 mikrosekund** — pięć rzędów wielkości.
2. **Prompt engineering ma twardy sufit.** Cztery niezależne interwencje, cztery
   razy 0,0 % na flagach. Piąte podejście — hybryda klasyczna + LLM — przeskoczyło
   to od razu: 50 % → 96,7 %.
3. **Retrieval nie pomógł tam, gdzie był potrzebny.** Qdrant poprawiał skalary
   i **pogarszał** flagi (−3,5 pp), czasem utwierdzając model w błędzie.
4. **Wnioski są własnością metody, nie sprzętu.** Replikacja na drugiej
   platformie dała wynik identyczny co do liczby.
5. **Uczenie maszynowe jest potrzebne selektywnie.** Przy wykrywaniu bajtów
   z flagami wystarczyła zmiana jednej stałej (+5 pp). Przy masce bitowej żadna
   prosta reguła nie wystarcza — tam ML jest konieczny.

# Wspólne ograniczenie wszystkich eksperymentów

**Cały ruch CAN jest syntetyczny**, z własnego generatora. Testy na innych
ziarnach ograniczają ryzyko zapamiętywania, ale inne ziarno to wciąż ten sam
generator. **Walidacja na magistrali prawdziwego pojazdu pozostaje zadaniem
otwartym** — i jest to najpoważniejsze ograniczenie całej pracy.

# Zadania otwarte

| Zadanie | Blokada |
|---|---|
| Eksperyment 3.1 — pomiar terenowy | decyzja wykładowcy w 10 kwestiach metodologicznych |
| Etap D — fine-tuning | **odblokowany**, klasyfikator-nauczyciel naprawiony |
| Walidacja na prawdziwej maszynie | dostęp do pojazdu |
| Wdrożenie „limit 6 → 7" do kodu produkcyjnego | potwierdzone na 2 korpusach, gotowe do wykonania |
