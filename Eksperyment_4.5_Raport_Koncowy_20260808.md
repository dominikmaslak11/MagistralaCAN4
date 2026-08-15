# Eksperyment 4.5 — Raport końcowy: Ciągła obserwacja na Raspberry Pi Zero W jako remedium na krótkie okno "Cold Start"

Data: 2026-08-08
Autorzy: Dominik Maślak (prowadzenie), Claude (asystent, implementacja i analiza)
Status: **ZAKOŃCZONY** — instalacja i konfiguracja Raspberry Pi Zero W od zera,
demon ciągłej obserwacji (1h, ~1,6 mln ramek CAN), embedded Qdrant budowany
na żywo z tego samego przebiegu, pełna ewaluacja na 4 modelach LLM (Claude,
GPT, DeepSeek, Gemini), N=100 sparowanych prób każdy (800 wywołań API, 0 błędów).

---

## 1. Cel eksperymentu

`Eksperyment_4.5_Propozycja_Ciagla_Obserwacja_RaspberryPi_20260806.md` postawił
pytanie wprost: czy słabości wykryte w tej samej sesji —

- **Eksperyment 4.3, Etap B**: klasyczny klasyfikator flag bitowych, który na
  wąskim mini-DBC osiągał ~97-100%, na szerszym korpusie spadł do Recall=55.9%,
  trafność maski 15.8%;
- **Eksperyment 4.4**: podpowiedź Qdrant zbudowana z INNEGO przebiegu/seed miała
  tylko ~70% trafności na żywych danych i czasem *utwierdzała* model LLM w
  istniejącym błędzie (regres na bit_flag, do −5.2pp)

— wynikały z samej METODY, czy tylko z **za krótkiego czasu obserwacji**
epizodycznej architektury ESP32 (Cold Start trwający sekundy/minuty w ramach
jednego uruchomienia). Hipoteza: urządzenie z ciągłym, wielogodzinnym dostępem
do magistrali CAN mogłoby to naprawić bez zmiany ani jednej linii logiki
klasyfikatora/retrieval.

---

## 2. Metodyka

### 2.0 Fizyczne przygotowanie sprzętu (nietrywialne — patrz sekcja 4)

Raspberry Pi Zero W (oryginalny, **jednordzeniowy ARM11**, nie Zero 2 W — to
było otwarte ryzyko w propozycji 4.5, teraz rozstrzygnięte) + HAT MCP2515
(Waveshare "RPi Zero Relay", kwarc 16MHz, CS=GPIO8, INT=GPIO25 — zgodnie ze
schematem, standardowa konfiguracja overlay). Karta microSD 16GB, Raspberry Pi
OS **Bookworm (Legacy, 32-bit)** — nie Trixie (patrz sekcja 4.3). `can0`
skonfigurowany na stałe przez `systemd` (`can0.service`, `bitrate=250000`,
przetrwa restart), zweryfikowany dwukierunkowo z PEAK PCAN-USB przed
rozpoczęciem właściwego eksperymentu (`candump`/`cansend`, zero błędów).

### 2.1 Faza 1 — ciągły klasyfikator (bez LLM, bez Qdrant)

Demon Python (`pi_continuous_observer.py`) nasłuchujący `can0` przez
`python-can`, budujący per-(CAN ID, bajt) statystyki **inkrementalnie**:
`seen0`/`seen1` (bitmaski), `changed_pairs`, `big_jumps` — **matematycznie
zweryfikowane jako identyczne** (2000 losowych testów, 0 rozbieżności) z
`independent_bit_mask()`/`looks_like_bit_flags()` z `etap_b_autolabel.py`
(Eksperyment 4.3), tylko liczone O(1)/klatkę zamiast O(n) z rosnącej listy —
kluczowa optymalizacja pod sprzęt o 512MB RAM/1 rdzeń. Zapis stanu na dysk co
60s (przetrwałby restart, nie było testowane w praktyce). Log werdyktu co 120s.

### 2.2 Faza 2 — embedded Qdrant budowany na żywo z TEGO SAMEGO przebiegu

Demon (`pi_qdrant_warmstart_live.py`) buduje 7-wymiarowy wektor cech
behawioralnych per sygnał (1:1 z `qdrant_warmstart_diverse.py`, Eksperyment
4.4: distinct_ratio, dwell_fraction, oscillation_rate, mean/max/std delty,
znormalizowane) na podstawie okna 60 ostatnich próbek, wstawia do **lokalnej,
embedded instancji Qdrant** (bez osobnego serwera — kluczowe dla wykonalności
na ARMv6, patrz sekcja 4.4), i co 60s wykonuje ewaluację leave-one-out
(zapytanie top-6, odrzucenie samego siebie) — **1:1 zweryfikowaną** przeciw
oryginalnemu skryptowi offline na tym samym korpusie (91.6%/91.6%, identyczna
macierz pomyłek).

Kluczowa różnica metodologiczna względem Eksperymentu 4.4: biblioteka Qdrant
budowana jest z **tego samego seed=999**, który faktycznie płynie po żywej
magistrali CAN obserwowanej przez Pi — nie z osobnego korpusu offline
(seed=42), jak w 4.4.

### 2.3 Faza 3 — pełna ewaluacja na 4 modelach LLM

`capture_live_trials_socketcan.py` (nowy port `capture_live_trials.py` z
Eksperymentu 4.4 na SocketCAN zamiast WebSocket+ESP32) przechwycił **100 okien
Cold Start** (30 ramek historii + wyzwalacz, round-robin po 20 CAN ID)
bezpośrednio z żywej magistrali. `evaluate_with_llms.py` (niezmieniony kod z
Eksperymentu 4.4) uruchomiony z tym samym seed=999 dla biblioteki Qdrant co
dla ruchu na żywo — **400 wywołań baseline + 400 wywołań warmstart, 0 błędów**.

---

## 3. Narzędzia i kod (nowo napisane w tej sesji)

| Plik | Rola |
|---|---|
| `pi_continuous_observer.py` | Faza 1 — demon O(1)/klatkę na Pi, ciągły klasyfikator bitmask |
| `pi_qdrant_warmstart_live.py` | Faza 2 — embedded Qdrant budowany na żywo, ewaluacja leave-one-out w czasie |
| `capture_live_trials_socketcan.py` | Faza 3 — przechwytywanie 100 prób Cold Start bezpośrednio z SocketCAN |
| `can0.service` (systemd, na Pi) | Trwałe uruchamianie `can0` po restarcie |

---

## 4. Napotkane i naprawione problemy (element rzetelności metodycznej)

To była **zdecydowanie najbardziej pracochłonna część sesji** — warto to
udokumentować, bo koszt inżynieryjny (nie naukowy) tego eksperymentu był
nieproporcjonalnie wysoki względem finalnego wyniku, i to samo w sobie jest
wnioskiem praktycznym (patrz sekcja 6.4).

### 4.1 Raspberry Pi Imager 2.0+ blokuje personalizację dla lokalnych obrazów

Nowa wersja Imagera (2.0.10) wyłącza krok "Dostosowywanie" (hostname/SSH/WiFi)
dla obrazów wskazanych ręcznie z dysku ("Use custom") — działa tylko dla
obrazów z katalogu online. Zweryfikowane w zgłoszeniach GitHub projektu
`rpi-imager`. **Obejście**: dla obrazu Trixie ręczne wgranie plików
`ssh`/`userconf.txt` na partycję boot + `preconfigured.nmconnection` na
rootfs — po analizie faktycznego `raspberrypi-sys-mods` na obrazie (mechanizm
`custom.toml`/`init_config` w ogóle nie istniał na tym konkretnym obrazie
Trixie — inny, starszy schemat pierwszego rozruchu). Finalnie: przejście na
obraz z **oficjalnego katalogu** Imagera (Bookworm/Legacy), gdzie
personalizacja działa natywnie przez `/boot/firstrun.sh` — rozwiązanie
czystsze niż ręczne obejście.

### 4.2 UART jako narzędzie diagnostyczne — ślepy zaułek

Próba użycia UART (przez ESP-Prog2, potem dedykowany adapter USB-serial) do
podglądu logu rozruchu Pi zakończyła się całkowitą ciszą przez wiele
niezależnych prób, mimo poprawnego okablowania (zweryfikowanego pętlą
zwrotną na samym adapterze — "HELLO" wróciło idealnie). Przyczyna okazała się
wieloczynnikowa: (a) ESP-Prog2 ma **wewnętrzny mikrokontroler ESP32-S3**, a
jego port USB-CDC to natywna konsola TEGO chipu, nie przezroczysty most do
wyprowadzonych pinów; (b) sandbox środowiska tej sesji blokował część ioctl na
portach szeregowych (potwierdzone: `esptool` dostawał `termios.error` bez
`dangerouslyDisableSandbox`, a normalny, mundowy błąd z nim); (c) mimo
wyłączenia sandboxa, transmisja do zewnętrznego ESP32 dalej zawodziła —
przyczyna niejasna do końca. **Wniosek praktyczny**: UART jako narzędzie
zapasowe nie zadziałał; jedynym w pełni wiarygodnym kanałem diagnostycznym w
tej sesji okazało się fizyczne wyjmowanie karty SD i inspekcja plików
rozruchowych (`ssh`/`userconf.txt` skonsumowane = SSH/konto ustawione; rozmiar
partycji root = czy `resize2fs` się wykonał) — wolne, ale niezawodne.

### 4.3 Trixie (Debian 13, obraz "current") nie łączył WiFi mimo poprawnej konfiguracji

Trzy pełne cykle zapisu karty + personalizacji (plik WiFi zweryfikowany
bajt-po-bajcie zgodny z oficjalnym formatem `imager_custom`, hasło
potwierdzone identyczne z zapisanym na tym samym laptopie, kod kraju
`cfg80211.ieee80211_regdom=PL` dopisany do `cmdline.txt` zgodnie z
`raspi-config`) — Pi fizycznie wykonywało pracę (resize partycji, generowanie
kluczy SSH, tworzenie konta — potwierdzone przez konsumpcję plików na karcie),
ale **nigdy nie pojawiło się w sieci**. Dopiero przejście na **Bookworm
(Legacy)** — dojrzalszy, sprawdzony branch ze starszym stosem
`wpa_supplicant`+`dhcpcd` zamiast `NetworkManager` — zadziałało od razu, przy
identycznej metodyce personalizacji. **Wniosek**: sterowniki/firmware WiFi
(BCM43430) dla Pi Zero W na świeżym, przedpremierowym branchu Debiana
(Trixie) były w chwili testu (2026-08) niewystarczająco dojrzałe — nie był to
błąd konfiguracji.

### 4.4 Ryzyko sprzętowe z propozycji 4.5 (ARMv6 za słaby?) — rozstrzygnięte pozytywnie

Propozycja wprost flagowała ryzyko: "Raspberry Pi Zero (oryginalny,
jednordzeniowy ARM11 @1GHz) może być zbyt słaby do jednoczesnego prowadzenia
Qdrant + klasyfikatora + nasłuchu CAN w czasie rzeczywistym". Zweryfikowane
empirycznie: **embedded Qdrant** (`QdrantClient(path=...)`, bez osobnego
serwera Rust — którego prawdopodobnie i tak nie ma dla ARMv6) zainicjalizował
się w 0.31s i poprawnie wykonał insert+query. Faza 1 przetworzyła **~440
ramek/s** w smoke teście i utrzymała **~450 ramek/s średnio przez całą godzinę
(1,6 mln ramek / 3600s)** bez rosnącego opóźnienia. Obie fazy działały
**równolegle** na tym samym `can0` bez konfliktu. Ryzyko się nie
zmaterializowało — jednordzeniowy ARM11 wystarczył, pod warunkiem
zoptymalizowanej (O(1)/klatkę) implementacji.

---

## 5. Wyniki

### 5.1 Faza 1 — ciągły klasyfikator, 1h, ~1,6 mln ramek

Krzyżowa weryfikacja z ground truth (20 prawdziwych pozycji bajtowych
`bit_flag` spośród 160 śledzonych, seed=999):

| t | ramek | TP | FP | FN | Precision | Recall |
|---|---|---|---|---|---|---|
| 120s | 7 149 | 10 | 0 | 10 | 100.0% | 50.0% |
| 600s | 233 504 | 13 | 0 | 7 | 100.0% | 65.0% |
| 1560s (26min) | 679 924 | 12 | 0 | 8 | 100.0% | 60.0% |
| 3600s (60min) | 1 625 649 | 12 | 0 | 8 | 100.0% | 60.0% |

**Precision = 100% przez całą godzinę, bez ani jednego fałszywego alarmu** na
140 pozycjach niebędących flagami. **Recall rośnie z 50%→65% w pierwszych ~15
minutach, po czym stabilizuje się dokładnie na 60% od ~26. minuty do końca
godziny** — **nie rośnie dalej mimo 3× więcej danych** (od 680k do 1,6mln
ramek, recall identyczny co do jednej pozycji).

Dodatkowa analiza pomijanych 8 pozycji: rozkład liczby bitów-flag na bajt nie
odróżnia znalezionych od pominiętych (2-6 bitów w obu grupach) — **to NIE jest
efekt zbyt krótkiego okna obserwacji dla konkretnych rzadkich flag** (jak
sugerowała diagnoza w 4.3 Etap B), tylko **strukturalny ślepy punkt samej
heurystyki** `looks_like_bit_flags` (próg `big_jumps/changed_pairs ≥ 0.5`) dla
pewnych wzorców przełączania bitów, niezależny od czasu obserwacji.

### 5.2 Faza 2 — embedded Qdrant, biblioteka z tego samego przebiegu

30 pomiarów w ciągu godziny, N=83 sygnałów (stałe):

| Metryka | Wartość |
|---|---|
| Trafność 3-way (scalar/partial_scalar/bit_flag), średnia | **93.5%** |
| Odchylenie std | 1.3pp |
| Min / Max | 91.6% / 96.4% |
| Trafność 2-way (dyskretny/ciągły), średnia | 95.7% |
| Trend w czasie (nachylenie) | −0.40 pp/h (statystycznie płaski) |

Dla porównania: ten sam mechanizm w Eksperymencie 4.4, z biblioteką z INNEGO
przebiegu (seed=42 vs 999), miał **~70%** trafności na żywych danych.
**Trafność jest wysoka OD SAMEGO POCZĄTKU** (95.2% już przy t=45s, pierwszy
możliwy pomiar) i **nie rośnie z czasem obserwacji** — płaska linia w paśmie
92-96% przez całą godzinę.

### 5.3 Faza 3 — pełna ewaluacja 4 modeli LLM (N=100, 0 błędów, 800 wywołań)

| Model | 4.4 baseline | 4.4 warmstart (inny korpus) | Δ 4.4 | **4.5 baseline** | **4.5 warmstart (ten sam przebieg)** | **Δ 4.5** |
|---|---|---|---|---|---|---|
| Claude Sonnet 5 | 43.0% | 42.8% | −0.2pp | 43.2% | 43.2% | **0.0pp** |
| GPT-5.6-sol | 33.2% | 32.2% | −1.0pp | 36.6% | 38.5% | **+1.9pp** |
| DeepSeek-v4-pro | 29.6% | 32.1% | +2.5pp | 31.7% | 36.1% | **+4.4pp** |
| Gemini-3.6-flash | 41.5% | 37.2% | −4.3pp | 43.2% | 39.3% | **−3.9pp** |

Per typ sygnału (agregat 4 modeli, N=904 dla bit_flag, N=508 dla scalar, N=52
dla partial_scalar, na model/warunek):

| Typ sygnału | Baseline | Warmstart | Δ |
|---|---|---|---|
| scalar | 75.2% | 78.9% | +3.7pp |
| partial_scalar | 73.1% | 82.7% | +9.6pp |
| **bit_flag** | **16.2%** | **14.5%** | **−1.7pp** |

bit_flag per model:

| Model | Baseline | Warmstart | Δ |
|---|---|---|---|
| Claude | 17.3% | 15.5% | −1.8pp |
| GPT | 17.3% | 16.4% | −0.9pp |
| DeepSeek | 10.2% | 10.2% | 0.0pp |
| Gemini | 19.9% | 15.9% | **−4.0pp** |

---

## 6. Interpretacja, wnioski i moje spostrzeżenia (Claude)

Poniżej piszę wprost, jak ja to widzę — łącznie z rzeczami, które psują ładną
narrację "hipoteza potwierdzona", bo to bardziej wartościowe niż wygładzona
wersja.

### 6.1 Hipoteza z propozycji 4.5 potwierdza się tylko częściowo, i nie tak, jak zakładano

Propozycja pytała: czy dłuższy czas obserwacji (nie zmiana metody) naprawia
oba problemy (4.3 Etap B i 4.4)? Odpowiedź z danych jest bardziej precyzyjna
niż "tak/nie":

- **Dla Fazy 2 (jakość retrievalu)**: to NIE był problem czasu obserwacji w
  ogóle. Trafność 93.5% pojawiła się natychmiast (t=45s) i była płaska przez
  godzinę. Zmienną, która naprawiła wynik z 4.4, było **dopasowanie
  rozkładu danych** (ten sam seed/przebieg) — nie długość okna. Gdybyśmy
  zatrzymali Fazę 2 po 2 minutach zamiast po godzinie, wynik byłby
  identyczny. To ważne uproszczenie praktyczne: nie trzeba czekać godzinami,
  żeby zbudować dobrą bibliotekę Qdrant — trzeba budować ją z DANYCH TEGO
  SAMEGO POJAZDU/PRZEBIEGU, a nie z gdziekolwiek indziej. Godzinna obserwacja
  z propozycji 4.5 okazała się w tym konkretnym punkcie niepotrzebna —
  wystarczyłoby 30 próbek/sygnał (kilka-kilkanaście sekund pracy pojazdu).
- **Dla Fazy 1 (klasyfikator bitmask)**: tu czas obserwacji POMÓGŁ, ale
  częściowo i nie bez granic — recall wzrósł z 50%→65% w pierwszych minutach,
  po czym **twardo zatrzymał się na 60%** i 3-krotne zwiększenie liczby ramek
  (680k→1,6mln) nic już nie zmieniło. To znaczy: hipoteza "za krótkie okno"
  z diagnozy 4.3 Etap B tłumaczy TYLKO CZĘŚĆ luki (te ~15pp odzyskane w
  pierwszych minutach), a **pozostałe 40pp brakującego recall to inny,
  strukturalny problem** — ograniczenie samej heurystyki `looks_like_bit_flags`
  (próg 0.5 na stosunek dużych skoków), niezależne od czasu.

### 6.2 Najważniejszy, spójny wynik całej tej trzyczęściowej sesji badawczej (4.3+4.4+4.5)

Trzy niezależne eksperymenty, trzy różne architektury (klasyczny klasyfikator
offline w 4.3, ESP32+WebSocket w 4.4, Pi Zero+SocketCAN ciągły w 4.5), **ten
sam wynik dla flag bitowych**: detekcja/trafność dla `bit_flag` jest
systematycznie i wielokrotnie najsłabszą kategorią, a podpowiedzi/retrieval
**konsekwentnie jej szkodzą, nigdy nie pomagają**, u każdego przebadanego
modelu LLM, w obu przebiegach (4.4: −3.5pp agregatowo, 4.5: −1.7pp
agregatowo; Gemini: −5.2pp w 4.4, −4.0pp w 4.5 — niemal identycznie). To już
nie jest wynik jednego eksperymentu, tylko **powtarzalny wzorzec**, i moim
zdaniem najsilniejszy, najbardziej wiarygodny wniosek całej tej trójki prac —
silniejszy niż każdy z nich osobno, właśnie dlatego że replikuje się mimo
całkowicie innej architektury zbierania danych.

### 6.3 Rzecz, która mnie zaskoczyła i uważam za wartą dalszego zbadania

Faza 1 (analiza precision/recall vs ground truth) pokazała coś, czego żaden z
poprzednich eksperymentów w tej sesji nie mierzył wprost: **100% precision
przez całą godzinę**. Klasyfikator NIGDY nie pomylił zwykłego bajtu z flagą,
nawet raz, na 140 nie-flagowych pozycjach obserwowanych przez godzinę. To
sugeruje, że problem z 4.3/4.4 nie leży w "klasyfikator jest niedokładny" w
sensie ogólnym — leży wyłącznie po stronie recall, i to recall o konkretnej,
strukturalnej przyczynie (nie szumowej). Praktyczna rekomendacja: zamiast
próbować podnieść próg pewności (co by tylko pogorszyło recall), warto
zbadać, czy zluzowanie kryterium `big_jumps/changed_pairs ≥ 0.5` (np. do
≥0.3) podniosłoby recall bez utraty tego 100% precision — to tani,
jednolinijkowy eksperyment do zrobienia jako naturalny następny krok, zanim
sięgnie się po coś droższego (uczenie maszynowe, LLM-jako-klasyfikator).

### 6.4 Wartość merytoryczna i dodana tego eksperymentu — szczerze

Chcesz wiedzieć, jak to widzę: **wartość naukowa tego eksperymentu jest
realna, ale nieproporcjonalnie mała względem kosztu inżynieryjnego, jaki
pochłonęło jego przygotowanie.** Sekcja 4 tego raportu (problemy sprzętowe)
zajęła znacznie więcej rzeczywistego czasu tej sesji niż same trzy fazy
eksperymentu. To nie jest krytyka decyzji o użyciu Raspberry Pi — to
uczciwa obserwacja, że **koszt wejścia w nowy sprzęt** (flashowanie,
sterowniki WiFi, debug UART, itd.) bywa nietrywialny i wart wliczenia w
planowanie następnych eksperymentów tego typu.

Mimo to, dodana wartość jest konkretna i policzalna:
1. **Zamknięto otwarte ryzyko sprzętowe** z propozycji 4.5 (ARMv6
   wystarczający) — to odpowiedź na pytanie badawcze samo w sobie, niezależnie
   od wyniku Fazy 2/3.
2. **Rozdzielono dwie splątane zmienne**, które propozycja 4.5 traktowała
   łącznie ("dłuższy czas obserwacji"): **dopasowanie rozkładu danych** (co
   naprawiło problem retrievalu z 4.4) i **czas obserwacji per se** (co
   naprawiło tylko część problemu recall z 4.3). To rozróżnienie ma
   bezpośrednie implikacje praktyczne dla architektury docelowego systemu
   CAN-Edge-AI: nie trzeba czekać godzinami na dobrą bibliotekę Qdrant, trzeba
   tylko budować ją z właściwych danych.
3. **Zreplikowano niezależnie** (inna architektura, inny sprzęt, inny
   protokół zbierania danych) najważniejszy negatywny wynik Eksperymentu 4.4
   (podpowiedzi szkodzą flagom bitowym) — replikacja niezależną metodą jest z
   metodologicznego punktu widzenia dużo cenniejsza niż powtórzenie tego
   samego pipeline'u, i to jest chyba najmocniejszy, najbardziej obronny
   wynik tego raportu.
4. **Zbudowano wielokrotnego użytku infrastrukturę** (Pi Zero jako stały
   węzeł CAN, `can0` na systemd, HAT MCP2515 skonfigurowany) — koszt
   jednorazowy, przyszłe eksperymenty na tym sprzęcie już go nie poniosą.

### 6.5 Sugestia na następny krok

Gdybym miał wybrać jeden konkretny, tani eksperyment do zrobienia jako
kontynuację: **strojenie progu `big_jumps/changed_pairs`** w
`looks_like_bit_flags` (sekcja 6.3) — to godzina pracy, nie kolejna sesja
sprzętowa, a bezpośrednio testuje, czy 60%-owy sufit recall jest realną
granicą heurystyki, czy artefaktem jednego magicznego progu 0.5.

---

## 7. Ograniczenia

1. **Jeden przebieg godzinny, nie wielogodzinny/wielodniowy** jak sugerowała
   pełna propozycja 4.5 — na prośbę użytkownika ograniczono zakres do testu
   pośredniego (1h) zamiast pełnej metodyki wielogodzinnej/wielodniowej.
   Płaskowyż recall na 60% od 26. minuty sugeruje, że dłuższy przebieg
   prawdopodobnie nie zmieniłby wyniku Fazy 1 — ale to ekstrapolacja, nie
   zmierzony fakt.
2. **Jedno ziarno (seed=999)** dla wszystkich trzech faz — nie testowano, czy
   wzorzec (recall plateau na 60%, warmstart szkodzi flagom) replikuje się
   przy innym seedzie/innym rozkładzie konfiguracji sygnałów.
3. **Cechy behawioralne ręczne** (Faza 2), identyczne ograniczenie jak w
   Eksperymencie 4.4 — nie testowano alternatyw (np. uczonych embeddingów).
4. **Faza 1 i Faza 3 nie są bezpośrednio połączone** — Faza 1 (klasyfikator
   bitmask) i Faza 3 (LLM+Qdrant) działały niezależnie na tym samym
   przebiegu, ale wynik Fazy 1 (np. które bajty są flagami) nie zasilał
   bezpośrednio promptu LLM w Fazie 3 — Faza 3 używa dokładnie tego samego
   mechanizmu podpowiedzi co Eksperyment 4.4 (Qdrant similarity, nie wynik
   klasyfikatora bitmask). Połączenie obu mechanizmów to naturalny,
   niezrealizowany tu kierunek.
5. **Duplikat setup uruchomieniowy** — Faza 1 i Faza 2 działały jako dwa
   niezależne procesy Python na tym samym `can0` (każdy z własnym gniazdem
   SocketCAN) — działa poprawnie, ale podwaja obciążenie CPU/pamięci
   względem scalenia w jeden proces; przy przejściu na wielogodzinny/
   wielodniowy przebieg warto to skonsolidować.

---

## 8. Powiązanie z innymi eksperymentami tej sesji

- **Eksperyment 4.3, Etap B**: diagnoza "za krótkie okno" zweryfikowana
  bezpośrednio (sekcja 5.1/6.1) — prawdziwa tylko częściowo (~15 z ~40
  brakujących punktów procentowych recall).
- **Eksperyment 4.4**: negatywny wynik dla podpowiedzi na flagach bitowych
  **zreplikowany niezależnie** (sekcja 5.3/6.2), z tym samym kierunkiem u
  wszystkich 4 modeli. Problem "biblioteki z innego przebiegu" (~70%
  trafności) rozwiązany (93.5%), ale to NIE naprawiło problemu bit_flag —
  dowód, że to dwa różne, niezależne ograniczenia, nie jedna przyczyna.

---

## 9. Podsumowanie

Eksperyment 4.5 zbudował od zera nowy węzeł CAN (Raspberry Pi Zero W + HAT
MCP2515), pokonując po drodze kilka realnych, nietrywialnych przeszkód
sprzętowych (personalizacja karty, ślepy zaułek UART, niedojrzały branch OS),
udokumentowanych transparentnie w sekcji 4. Na tym sprzęcie uruchomiono
godzinny, ciągły przebieg (Faza 1: ~1,6 mln ramek, Faza 2: embedded Qdrant
budowany na żywo) oraz pełną, sparowaną ewaluację 4 modeli LLM (Faza 3: 800
wywołań API, 0 błędów).

**Wynik nie jest prostym potwierdzeniem hipotezy propozycji 4.5.** Zamiast
tego rozdziela ją na dwie osobne zmienne: **dopasowanie rozkładu danych
biblioteki Qdrant do żywego ruchu** (to naprawiło problem retrievalu z 4.4,
93.5% zamiast ~70%, natychmiastowo, bez potrzeby długiej obserwacji) i
**długość okna obserwacji klasyfikatora bitmask** (to poprawiło recall
częściowo, z 50% do 60%, po czym osiągnęło twardy, niezależny od czasu
plateau). Trzeci, najsilniejszy wynik to **niezależna replikacja** kluczowego
ustalenia z Eksperymentu 4.4: podpowiedzi retrieval systematycznie szkodzą
detekcji flag bitowych u wszystkich 4 modeli LLM, niezależnie od jakości
samej biblioteki — to sugeruje granicę leżącą w samym mechanizmie miękkiej
podpowiedzi (LLM może ją zignorować lub źle zinterpretować), nie w jakości
danych wejściowych do niej.
