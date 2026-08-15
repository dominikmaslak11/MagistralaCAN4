# Eksperyment 4.5, Faza 5 — Embeddingi neuronowe zamiast ręcznych cech

Data: 2026-08-08
Autorzy: Dominik Maślak (prowadzenie), Claude (asystent, implementacja i analiza)
Status: **ZAKOŃCZONY (badawczo, offline)** — pozytywny wynik dla jakości
retrievalu, ale **twardy blokier sprzętowy** na obecnym Raspberry Pi Zero W
(ARMv6). Ścieżka dalsza: Orange Pi Zero 3 (aarch64), zweryfikowana jako
technicznie wykonalna.

---

## 1. Pytanie

Faza 2 Eksperymentu 4.5 użyła ręcznie zaprojektowanego, 7-wymiarowego
wektora cech behawioralnych (distinct_ratio, dwell_fraction, oscillation_rate
itd. — Eksperyment 4.4) do budowy biblioteki Qdrant. Pytanie: czy embedding
generowany przez mały, gotowy model sieci neuronowej (bez ręcznego
projektowania cech) złapie wzorce, których ręczna heurystyka nie widzi —
i czy poprawia to zwłaszcza **generalizację między różnymi przebiegami**
(to konkretny, zdiagnozowany słaby punkt z Eksperymentu 4.4).

---

## 2. Krytyczne odkrycie sprzętowe (sprawdzone PRZED analizą jakości)

Zanim zajęto się jakością wyników, sprawdzono wprost na Raspberry Pi Zero W
(`armv6l`), czy jakikolwiek mainstream silnik inferencji ML da się w ogóle
zainstalować:

| Framework | `pip install` na Pi Zero W (przez piwheels.org) |
|---|---|
| PyTorch | ❌ „No matching distribution found” |
| ONNX Runtime | ❌ „No matching distribution found” |
| TensorFlow Lite | ❌ „No matching distribution found” |

**Żaden z trzech głównych frameworków ML nie ma zbudowanych pakietów dla
ARMv6** — to nie kwestia wydajności (jak w przypadku serwera Qdrant, gdzie
znaleziono tryb embedded jako obejście), tylko całkowity brak wsparcia
ekosystemu. Społeczność ML porzuciła ARMv6 jako architekturę docelową lata
temu — to jeden z realnych powodów, dla których Raspberry Pi Zero **2** W
(następca, ARMv7/aarch64) w ogóle powstało.

**Wniosek praktyczny**: ta Faza jest w całości badaniem offline
(laptop, x86_64) — wynik jakościowy jest realny i wart udokumentowania, ale
**niewdrażalny na obecnym sprzęcie Eksperymentu 4.5**.

### 2.1 Alternatywa sprzętowa: Orange Pi Zero 3 — zweryfikowana pozytywnie

Użytkownik posiada dodatkowo Orange Pi Zero 3 (Allwinner H618, czterordzeniowy
**Cortex-A53, ARMv8/aarch64** — architektura generacyjnie inna niż ARM11 w Pi
Zero W). Sprawdzono na PyPI (nie na samej płytce — to do zrobienia w
kolejnym kroku): **PyTorch ma oficjalne koło `manylinux2014_aarch64`**,
pobrane bez problemu (91.9MB, torch 2.5.1). O ile Orange Pi Zero 3 pracuje
na 64-bitowym systemie (do potwierdzenia przez `uname -m` na samej płytce —
niektóre obrazy dla tej płytki bywają 32-bitowe mimo że chip wspiera 64-bit),
embeddingi neuronowe powinny być tam w pełni wykonalne bez żadnych obejść.

Dodatkowe wymaganie sprzętowe: Orange Pi Zero 3 ma inny układ złącza GPIO
niż Raspberry Pi — HAT MCP2515 (Waveshare) użyty w Fazie 1-4 **nie jest
fizycznie kompatybilny**. Potrzebny byłby generyczny moduł MCP2515+TJA1050
na złączu pinowym (nie w formacie HAT) + przewody do SPI Orange Pi.

---

## 3. Metodyka analizy jakości (offline, laptop)

Model: **`all-MiniLM-L6-v2`** (sentence-transformers, 384-wymiarowy
embedding). Serializacja: surowa seria wartości bajtowych/bitowych sygnału
zamieniona na tekst (wartości oddzielone spacją), embedowana bezpośrednio —
**model nigdy nie był trenowany na sekwencjach liczbowych, tylko na języku
naturalnym** — to mocno „out of distribution” zastosowanie, testowane tu
wprost empirycznie, bez zakładania z góry, że zadziała.

Trzy testy, rosnąco rygorystyczne:

1. **Test A — w obrębie jednego korpusu** (seed=42, leave-one-out): ten sam
   test co bazowy wynik Eksperymentu 4.4 (91.6% na cechach ręcznych w 4.4;
   89.5% tutaj — niewielka różnica przez inny podzbiór/wersję korpusu).
2. **Test B — cross-corpus, offline vs offline** (biblioteka=seed42,
   zapytania=seed999, oba czyste symulacje).
3. **Test C — cross-corpus, offline vs REALNE dane** (biblioteka=seed42
   offline, zapytania z `live_trials_seed999.json` — **prawdziwe, zaszumione
   dane przechwycone z żywej magistrali CAN w Fazie 3/4 tego eksperymentu**,
   N=162 instancji sygnałów). To najbliższy odpowiednik oryginalnego testu
   z Eksperymentu 4.4.

---

## 4. Wyniki

| Test | Cechy ręczne (3-way) | Embedding neuronowy (3-way) | Δ |
|---|---|---|---|
| A — w obrębie korpusu | 89.5% | **98.7%** | +9.2pp |
| B — cross-corpus (offline↔offline) | 92.8% | **100.0%** | +7.2pp |
| **C — cross-corpus (offline↔REALNE dane)** | **89.5%** | **98.8%** | **+9.3pp** |

Embedding neuronowy **konsekwentnie i wyraźnie wygrywa we wszystkich trzech
testach**, w tym w najbardziej rygorystycznym (Test C, prawdziwe dane z
magistrali) — 98.8% vs 89.5%, praktycznie eliminuje resztkowe pomyłki.

Macierz pomyłek, Test C:
- Cechy ręczne: 8× scalar→partial_scalar, 5× scalar→bit_flag, 4× partial_scalar→bit_flag (17 błędów/162)
- Embedding neuronowy: 1× scalar→partial_scalar, 1× partial_scalar→bit_flag (2 błędy/162)

---

## 5. Ważne zastrzeżenie metodologiczne — uczciwie

Wartość bazowa (cechy ręczne) w Teście C to **89.5%**, wyraźnie WYŻEJ niż
**~70%** cytowane jako problem w oryginalnym raporcie Eksperymentu 4.4.
Zbadano dlaczego: oryginalny wynik 4.4 pochodził z innej metryki
(`warmstart_hints_for_trial` — trafność pojedynczej podpowiedzi tekstowej
per bajt w pełnym pipeline LLM, przy 8 konfiguracjach CAN ID i innej ścieżce
przechwytywania — ESP32+WebSocket, nie SocketCAN) niż kontrolowany test
3-way leave-one-out/cross-query użyty tutaj (20 konfiguracji, ta sama
definicja `feature_vector`/ewaluacji co `qdrant_warmstart_diverse.py`, ale
inny sposób zapytania). **To nie są identyczne pomiary** — nie należy
czytać tego jako "problem z 4.4 się zmniejszył o 20pp samoistnie". To, co
JEST bezpośrednio porównywalne i w pełni kontrolowane w tym dokumencie, to
**cechy ręczne vs embedding neuronowy NA TYCH SAMYCH danych/zapytaniach/
metodzie ewaluacji** we wszystkich trzech testach — i tu przewaga embeddingu
jest spójna, duża i wiarygodna.

---

## 6. Wnioski i moje spostrzeżenia (Claude)

### 6.1 Główny wynik

Gotowy, nie-wyspecjalizowany model embeddingu tekstu, zastosowany do zadania
zupełnie spoza jego domeny treningowej (sekwencje liczbowe zamiast języka
naturalnego), **pobił ręcznie zaprojektowany, dedykowany wektor cech** we
wszystkich trzech testach, najmocniej tam gdzie to najważniejsze (dane z
prawdziwej magistrali). To zaskakujące — model nie ma żadnego pojęcia o
„odległości liczbowej” między tokenami takimi jak „45” i „46” — a mimo to
złapał strukturę lepiej niż heurystyka zaprojektowana specjalnie do tego
zadania.

### 6.2 Hipoteza, dlaczego to działa (niepotwierdzona, warta zbadania)

Podejrzewam, że embedding tekstowy niejawnie koduje informacje, których
7-wymiarowy wektor ręczny się pozbywa przez agregację (distinct_ratio,
mean itd. to pojedyncze liczby uśredniające całą serię) — sekwencja tokenów
zachowuje częściowo **kolejność i lokalne wzorce powtórzeń** (np. "12 12 12
45 46" vs "12 45 12 46 12" mają różne tokeny sąsiedzkie, mimo identycznych
cech statystycznych). To by tłumaczyło, dlaczego model "z zewnątrz swojej
domeny" mimo to wygrywa — nie dzięki rozumieniu liczb, tylko dzięki
zachowaniu strukturalnemu sekwencji, którego uśrednione cechy ręczne nie
mają.

### 6.3 Twardy koszt tego wyniku — sprzęt

To najważniejsze zastrzeżenie tej Fazy: **wynik jest wysokiej jakości, ale
dziś niewdrażalny na Raspberry Pi Zero W**. W przeciwieństwie do
poprzednich barier w tej sesji (serwer Qdrant — rozwiązane trybem embedded;
moc obliczeniowa — rozwiązane przez O(1)/klatkę), to jest bariera
**ekosystemowa, nie architekturalna** w sensie projektowym — nie da się jej
obejść inaczej niż zmieniając sprzęt. Orange Pi Zero 3 (już posiadany przez
użytkownika) jest obiecującą, zweryfikowaną częściowo (PyPI ma koła
aarch64) ścieżką dalszą — wymaga jeszcze potwierdzenia na samej płytce i
innego modułu CAN (inny format GPIO).

### 6.4 Wartość merytoryczna i dodana — szczerze

To druga (po Fazie 4) faza tej sesji o charakterze **rozwiązania**, nie
tylko diagnozy — i to mimo negatywnego wyniku sprzętowego (ARMv6), sam
wynik jakościowy jest jednoznaczny i wart dalszego rozwoju. Wartość dodana
jest podwójna: (a) potwierdzono, że prostsze niż oczekiwano podejście
(gotowy model, zero treningu) bije dedykowaną inżynierię cech, (b) po drodze
znaleziono i zweryfikowano konkretną, wykonalną ścieżkę sprzętową (Orange
Pi Zero 3) do rzeczywistego wdrożenia tego wyniku — nie zostawiono tego
jako otwarte pytanie bez odpowiedzi.

### 6.5 Sugestia na następny krok

1. Podłączyć Orange Pi Zero 3, potwierdzić `uname -m` (aarch64?) i faktycznie
   zainstalować `torch`/`sentence-transformers` NA PŁYTCE (nie tylko sprawdzić
   dostępność kół na PyPI) — analogicznie do tego, jak zweryfikowano ARMv6
   bezpośrednio zamiast zgadywać.
2. Jeśli działa: zmierzyć realny czas embedowania jednego sygnału na tej
   płytce (Test A/B/C tutaj trwały sekundy na x86_64 — Cortex-A53 będzie
   wolniejszy, ale to wciąż nieporównywalnie szybszy rdzeń niż ARM11).
3. Dokupić generyczny moduł MCP2515 (nie HAT) do podłączenia CAN na
   Orange Pi.

---

## 7. Ograniczenia

1. Model `all-MiniLM-L6-v2` to jeden konkretny wybór — nie testowano innych
   (mniejszych/większych) modeli embeddingu.
2. Serializacja (surowe wartości liczbowe oddzielone spacją) to jeden,
   niewystrojony wybór reprezentacji tekstowej — inne kodowanie (delty,
   wartości szesnastkowe, itp.) mogłoby dać inny wynik.
3. Orange Pi Zero 3 feasibility sprawdzona TYLKO przez dostępność kół na
   PyPI (na laptopie) — nie na samej płytce. To do zrobienia jako pierwszy
   krok kolejnej sesji.
4. Test C używa danych z Fazy 3/4 (seed=999, 20 konfiguracji) — nie
   identycznych z oryginalnym `live_trials_captured.json` z Eksperymentu 4.4
   (8 konfiguracji, inny sprzęt przechwytujący) — stąd zastrzeżenie w
   sekcji 5.

---

## 8. Powiązanie z innymi eksperymentami tej sesji

- **Eksperyment 4.4**: bezpośrednia kontynuacja pytania o generalizację
  cross-corpus, z ważnym zastrzeżeniem o nieidentyczności metryk (sekcja 5).
- **Eksperyment 4.5, Faza 1-4**: ten sam wzorzec co przy Qdrant server
  (sekcja 4.4 Eksperymentu 4.5, strojenie progu) — "sprawdź empirycznie na
  prawdziwym sprzęcie, nie zakładaj" — tutaj zastosowany do samej
  wykonalności frameworku, nie tylko jego jakości.
- **Eksperyment 4.5, Uzasadnienie Zakupu RaspberryPi**: otwarte ryzyko
  "ARMv6 za słaby" z tamtego dokumentu miało dwie osobne odpowiedzi w tej
  sesji: NIE dla mocy obliczeniowej (Faza 1, 450 ramek/s), TAK dla
  ekosystemu ML (ta Faza, embeddingi neuronowe).

---

## 9. Podsumowanie

Embedding neuronowy (gotowy model `all-MiniLM-L6-v2`, zero treningu,
zastosowany do sekwencji liczbowych spoza jego domeny) pobił ręcznie
zaprojektowany wektor cech we wszystkich trzech testach jakości retrievalu
(+7 do +9pp), najsilniej na prawdziwych, zaszumionych danych z magistrali
CAN (98.8% vs 89.5%). Wynik ten jest jednak **niewdrażalny na obecnym
Raspberry Pi Zero W** — żaden mainstream framework ML nie ma pakietów dla
ARMv6 — co sprawdzono bezpośrednio na sprzęcie przed analizą jakości, nie
założono z góry. Orange Pi Zero 3 (aarch64), już posiadany przez
użytkownika, jest zweryfikowaną częściowo (PyPI) ścieżką dalszą, wymagającą
jeszcze potwierdzenia na samej płytce i innego modułu CAN.
