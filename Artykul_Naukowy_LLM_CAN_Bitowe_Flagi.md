---
status: SZKIC ROBOCZY — dokument żywy, aktualizowany po każdej kolejnej iteracji badań
wersja: 0.7
data ostatniej aktualizacji: 2026-07-30
autorzy: [Dominik Maślak] — do uzupełnienia: afiliacja, wykładowca/promotor jako współautor
---

# Ograniczenia promptowania a destylacja symboliczno-neuronowa w wykrywaniu upakowanych flag bitowych przez modele LLM w ramkach magistrali CAN

**Tytuł roboczy (angielski, do doprecyzowania przed submisją)**: *Prompting Limits and
Symbolic-to-Neural Distillation for Detecting Packed Bit-Flags in CAN Frames with
Large Language Models*

> **Uwaga dla współautorów**: ten plik jest celowo prowadzony jako pojedynczy,
> aktualizowany dokument (nie kolejne wersje w osobnych plikach) — sekcja 8
> ("Historia zmian") na końcu odnotowuje, co zostało dodane w każdej iteracji.
> Wszystkie liczby w tym dokumencie pochodzą BEZPOŚREDNIO z surowych raportów JSON
> w repozytorium (ścieżki podane przy każdej tabeli) — żadna nie jest szacunkiem
> z pamięci. Sekcje oznaczone **[DO UZUPEŁNIENIA]** wymagają decyzji/pracy
> wykładowcy przed submisją (przegląd literatury, afiliacje, wybór czasopisma).

---

## Abstrakt

**[DO UZUPEŁNIENIA — wersja robocza]**

Dekodowanie nieznanych ramek magistrali CAN (Controller Area Network) bez dostępu
do pliku DBC pozostaje istotnym problemem praktycznym w diagnostyce i badaniach
bezpieczeństwa pojazdów. Duże modele językowe (LLM) są naturalnym kandydatem do
automatyzacji tego zadania, jednak w tej pracy pokazujemy, że cztery niezależne
strategie promptowania (zero-shot, few-shot, wymuszona analiza entropii per-bajt,
naprawa zanieczyszczenia kontekstu historycznego) konsekwentnie zawodzą przy
rozpoznawaniu jednego konkretnego, ale powszechnego wzorca: bajtu pakującego kilka
niezależnych flag bitowych (np. stany świateł, drzwi, kierunkowskazów) — modele
zamiast dekompozycji na osobne sygnały binarne proponują pojedynczą wartość
skalarną. Testujemy 4 modele LLM (Claude Sonnet 5, GPT-5.6-sol, DeepSeek-v4-pro,
Gemini-3.6-flash) na w pełni kontrolowanym, syntetycznym zestawie sygnałów ze
znanym ground truth, przy użyciu rzeczywistego sprzętu (ESP32 + MCP2515) w pętli.
Pokazujemy następnie, że deterministyczny, klasyczny klasyfikator działający
RÓWNOLEGLE do LLM (nie zamiast niego) podnosi skuteczność wykrywania flag bitowych
z 0–36% (zero-shot) do 90,9–97,0%, bez pogorszenia wyników na sygnałach ciągłych.
Proponujemy interpretację tego zjawiska oraz szkicujemy plan doświadczenia
sprawdzającego, czy tę zdolność można "wszczepić" bezpośrednio w wagi modelu przez
fine-tuning na korpusie etykietowanym przez ów klasyczny klasyfikator (destylacja
symboliczno-neuronowa) — eksperyment ten jest **planowany, nie wykonany** w obecnej
wersji tej pracy.

**Słowa kluczowe**: duże modele językowe, CAN bus, reverse engineering, prompt
engineering, fine-tuning, destylacja wiedzy, systemy wbudowane

---

## 1. Wprowadzenie

Magistrala CAN (Controller Area Network) jest standardowym protokołem komunikacji
wewnątrzpojazdowej. Znaczenie poszczególnych bajtów ramki danej wiadomości CAN
definiuje plik DBC, który w praktyce jest własnością producenta pojazdu i rzadko
jest publicznie dostępny. Brak DBC utrudnia niezależną diagnostykę, badania
bezpieczeństwa (security research) oraz rozwój systemów telematycznych/ADAS
opartych na danych z magistrali pojazdu. Ręczny reverse engineering nieznanych
ramek CAN jest pracochłonny — stąd naturalne pytanie, czy duże modele językowe
(LLM), z ich zdolnością do rozpoznawania wzorców i wnioskowania z kontekstu, mogą
zautomatyzować ten proces.

**[DO UZUPEŁNIENIA]** — tu należy dodać przegląd istniejącej literatury o: (a)
automatyzacji reverse engineeringu protokołów binarnych, (b) zastosowaniach LLM w
cyberbezpieczeństwie pojazdów, (c) znanych ograniczeniach LLM w zadaniach
wymagających precyzyjnej analizy numerycznej/strukturalnej surowych danych. Autor
(Claude) nie ma dostępu do zweryfikowanej bazy bibliograficznej i celowo nie
wprowadza tu fikcyjnych cytowań — to zadanie do wykonania przez
człowieka-współautora przed submisją.

W tej pracy stawiamy pytanie węższe i bardziej sprawdzalne: czy LLM potrafią
poprawnie rozpoznać, że pojedynczy bajt danych CAN koduje **kilka niezależnych
sygnałów binarnych** (flag bitowych), a nie jedną wartość skalarną — częsty,
praktyczny wzorzec kodowania w rzeczywistych DBC (np. bajt statusu z bitami dla
świateł, drzwi, kierunkowskazów, hamulca ręcznego). Budujemy w pełni kontrolowane,
syntetyczne środowisko testowe ze znanym ground truth (opisane w sekcji 3),
testujemy 4 współczesne modele LLM w pięciu niezależnych wariantach metodologii
(sekcja 4), i proponujemy oraz weryfikujemy rozwiązanie hybrydowe łączące LLM z
klasycznym, deterministycznym algorytmem (sekcja 4.5).

---

## 2. Tło teoretyczne i pojęcia kluczowe

### 2.1 Destylacja wiedzy (knowledge distillation)

Destylacja wiedzy to technika transferu kompetencji z jednego modelu ("nauczyciela")
do drugiego ("ucznia") — klasycznie duży, kosztowny model uczy mały, tani model
[DO UZUPEŁNIENIA: cytowanie Hinton, Vinyals, Dean, "Distilling the Knowledge in a
Neural Network", 2015 — praca powszechnie znana, ale wymaga weryfikacji dokładnego
cytowania przez współautora przed submisją]. Wariant zastosowany w tej pracy różni
się od klasycznego schematu: nauczycielem jest **deterministyczny algorytm
symboliczny** (klasyczny klasyfikator wzorców bitowych), nie druga sieć neuronowa —
bliższe podejściu uczenia przez demonstrację reguł niż destylacji sieć-do-sieci.

### 2.2 Prompt engineering a fine-tuning

Prompt engineering (w tym few-shot learning i strukturalne instrukcje w oknie
kontekstu) modyfikuje wyłącznie zapytanie kierowane do modelu — wagi modelu
pozostają niezmienione. Fine-tuning (SFT — supervised fine-tuning) modyfikuje same
wagi modelu na podstawie zbioru przykładów treningowych. Wyniki tej pracy (sekcja
4) sugerują, że badany problem nie jest natury kontekstowej (niewystarczające dane
w oknie zapytania), lecz głębiej osadzony w domyślnym sposobie interpretacji danych
liczbowych przez model — co czyni fine-tuning naturalnym kierunkiem dalszych badań
(sekcja 6).

### 2.3 Definicje

- **Sygnał skalarny** — sygnał CAN kodowany jako jedna wartość liczbowa w jednym
  lub kilku bajtach (np. `wartość_fizyczna = surowa_wartość × skala + offset`).
- **Sygnał dyskretny / flaga bitowa** — sygnał binarny (0/1) kodowany jako
  pojedynczy bit w bajcie, niezależny od pozostałych bitów tego samego bajtu.
- **Ground truth** — prawdziwe, znane a priori znaczenie sygnału (w tej pracy:
  zdefiniowane przez autorów, ponieważ ruch CAN jest syntetycznie generowany).
- **Cold Start** — moment pierwszego pojawienia się nieznanego (z punktu widzenia
  systemu) identyfikatora ramki CAN, wyzwalający zapytanie do LLM o interpretację.

---

## 3. Stanowisko badawcze i metodologia

### 3.1 Sprzęt

Rzeczywisty sprzęt w pętli badawczej (hardware-in-the-loop), nie symulacja:
mikrokontroler ESP32 z kontrolerem CAN MCP2515 (SPI), połączony przez WiFi/WebSocket
z aplikacją hosta, oraz adapter PEAK PCAN-USB podłączony do magistrali CAN
(SocketCAN, interfejs `can0`) generujący syntetyczny ruch. Firmware ESP32
(`esp_experiment_1_1.ino`) przekazuje surowe ramki CAN przez WebSocket bez żadnej
interpretacji po stronie urządzenia — cała logika decyzyjna (wykrywanie Cold Start,
zapytania LLM, ocena trafności) działa w aplikacji hosta.

### 3.2 Syntetyczny zestaw sygnałów (ground truth)

Ze względu na brak dostępu do rzeczywistego pojazdu/pliku DBC, zdefiniowano
syntetyczny "mini-DBC" — 10 sygnałów na 3 identyfikatorach CAN, generowanych z
realistyczną zmiennością w czasie (`esp_experiment_4_1/generate_traffic.py`):

| CAN ID | Sygnał | Typ | Bajt(y) |
|---|---|---|---|
| 0x100 | RPM silnika | ciągły, 2-bajtowy | 0–1 |
| 0x100 | Temperatura płynu chłodzącego | ciągły, 1-bajtowy | 2 |
| 0x100 | Położenie przepustnicy | ciągły, 1-bajtowy | 3 |
| 0x150 | Kąt skrętu kierownicy | ciągły, 2-bajtowy ze znakiem | 0–1 |
| 0x150 | Prędkość pojazdu | ciągły, 2-bajtowy | 2–3 |
| 0x200 | Lewy kierunkowskaz | dyskretny (bit) | 0, bit 0 |
| 0x200 | Prawy kierunkowskaz | dyskretny (bit) | 0, bit 1 |
| 0x200 | Światła | dyskretny (bit) | 0, bit 2 |
| 0x200 | Drzwi kierowcy | dyskretny (bit) | 0, bit 3 |
| 0x200 | Hamulec ręczny | dyskretny (bit) | 0, bit 4 |

Kluczowy dla tej pracy przypadek testowy: **CAN ID 0x200, bajt 0** — pięć
niezależnych flag bitowych upakowanych w jednym bajcie.

### 3.3 Protokół zapytań LLM i ewaluacji

Po wykryciu Cold Start dla danego CAN ID, aplikacja wysyła zapytanie do modelu LLM
zawierające: (a) prompt systemowy opisujący zadanie i oczekiwany format JSON
odpowiedzi, (b) ramkę wyzwalającą, (c) do 30 ostatnich ramek historycznych tego
samego ID. Model zwraca listę proponowanych sygnałów (nazwa, pozycja bajtu,
długość, maska bitowa, skala, offset). Odpowiedź jest następnie stosowana do
kolejnych odebranych ramek i porównywana z ground truth. Każdy pełny przebieg
eksperymentu obejmuje N=100 niezależnych prób Cold Start (round-robin po 3 CAN ID),
dający ok. 33-34 próby na identyfikator.

**Metryki**: *detection rate* (odsetek prób, w których model zaproponował regułę
na właściwej pozycji bajtu/bitu), oraz — dla sygnałów wykrytych — precyzja,
pełność, F1 (sygnały dyskretne) lub RMSE (sygnały ciągłe).

### 3.4 Testowane warianty metodologiczne

W kolejnych iteracjach badania przetestowano pięć niezależnych wariantów:

1. **Zero-shot** — baseline, prompt bez dodatkowych wskazówek.
2. **Few-shot** — prompt bazowy rozszerzony o 2 w pełni rozwiązane przykłady, w tym
   jeden wprost pokazujący dekompozycję bajtu na niezależne flagi bitowe.
3. **Entropy-analysis** — prompt wymuszający obowiązkową, sformułowaną wprost
   procedurę: dla każdego bajtu wypisać obserwowane wartości, sprawdzić czy tworzą
   "wzorzec flag bitowych" (mały, niesekwencyjny zbiór wartości) czy "wzorzec
   skalarny" (gładka progresja), PRZED zaproponowaniem interpretacji.
4. **Context-fix** — naprawa błędu architektonicznego: wspólny bufor historii
   ramek dla 3 CAN ID zastąpiono osobnym buforem per CAN ID, tak by kontekst
   `recentFrames` faktycznie odpowiadał obietnicy promptu ("recent frames FOR
   THIS ID") zamiast zawierać ~67% ramek z innych, niepowiązanych sygnałów.
5. **Hybrydowy override** (opisany szczegółowo w sekcji 3.5) — deterministyczny
   klasyfikator działający równolegle do oceny "surowego" LLM.

### 3.5 Hybrydowy override klasyczny — projekt

Deterministyczny klasyfikator ocenia, niezależnie od odpowiedzi LLM, czy
obserwowane wartości danego bajtu (w historii ramek) wyglądają jak niezależne
flagi bitowe. Klasyfikacja opiera się WYŁĄCZNIE na obserwowanych danych (nie na
ground truth) i wymaga łącznie:

1. Co najmniej 2, ale nie więcej niż 6 z 8 bitów bajtu wykazuje w historii OBA
   stany (0 i 1) — prawdziwe pola flag wykorzystują PODZBIÓR bitów bajtu
   (pozostałe zarezerwowane/stałe); bajt wykorzystujący niemal cały zakres (7–8
   bitów) jest z dużym prawdopodobieństwem szeroko-zakresowym skalarem.
2. Większość (≥50%) zmian wartości między kolejnymi (w kolejności czasowej)
   próbkami stanowi "duży skok" (różnica bezwzględna > 3) — typowe dla
   przełączenia bitu wyższego rzędu, w odróżnieniu od płynnej progresji
   skalara/licznika.

Jeśli LLM zaproponuje pojedynczy skalar dla bajtu spełniającego oba kryteria, kod
programowo zastępuje tę regułę zestawem reguł per-bit (po jednej na każdy bit
wykazujący niezależne przełączanie). Metryki z override'em są raportowane
RÓWNOLEGLE do metryk "surowego" LLM — nie zastępują ich, umożliwiając bezpośrednie
porównanie.

**Pierwsza wersja tej heurystyki** wymagała tylko jednego niezależnie
przełączającego się bitu (bez kryterium 1 powyżej) i okazała się fałszywie
klasyfikować sygnały ciągłe o szerokim zakresie wartości jako flagi bitowe
(29 z 34 prób na CAN ID 0x100 w teście N=100) — błąd znaleziony i naprawiony w
trakcie badania (zob. sekcja 5.3).

---

## 4. Wyniki

### 4.1 Baseline zero-shot — porównanie 4 modeli LLM

Źródło danych: `Eksperyment_4.1_DecodingAccuracy_{Claude,GPT,DeepSeek,Gemini}_v2_*/decoding_accuracy_report.json`, N=100 na model.

| Model | Średnia detekcja (10 sygnałów) | Średnia detekcja — flagi bitowe (5 sygnałów) |
|---|---|---|
| GPT-5.6-sol | 67,9% | 35,8% |
| Gemini-3.6-flash | 50,9% | 3,0% |
| Claude Sonnet 5 | 47,9% | **0,0%** |
| DeepSeek-v4-pro | 41,5% | 6,7% |

Wszystkie 4 modele osiągają wysoką skuteczność na sygnałach ciągłych (79–100%),
ale drastycznie niższą na sygnałach dyskretnych — Claude Sonnet 5 nie wykrył
poprawnie ŻADNEJ z 5 flag bitowych w 100 próbach.

### 4.2 Interwencje promptowe — wyniki negatywne

Źródło danych: `Eksperyment_4.1_DecodingAccuracy_Claude_{FewShot,Entropy}_*/decoding_accuracy_report.json`, N=100, Claude Sonnet 5.

| Wariant | Średnia detekcja (10 sygnałów) | Flagi bitowe (5 sygnałów) | Sygnały ciągłe (5 sygnałów) |
|---|---|---|---|
| Zero-shot (baseline) | 47,9% | 0,0% | 92,1%* |
| Few-shot | 46,2% | 0,0% | 92,4% |
| Entropy-analysis | 46,7% | 0,0% | 93,5% |

*obliczone jako średnia z 5 sygnałów ciągłych z tego samego raportu bazowego.

Żadna z dwóch interwencji promptowych nie zmieniła detekcji flag bitowych
(pozostała dokładnie 0,0%), a obie NIEZNACZNIE pogorszyły średnią ogólną — dłuższy
prompt (z przykładami/instrukcją) nie przyniósł korzyści tam, gdzie i tak działał
dobrze, kosztem nieznacznej "utraty uwagi" modelu.

### 4.3 Naprawa zanieczyszczenia kontekstu — wynik neutralny/negatywny (nieoczekiwany)

Źródło danych: `Eksperyment_4.1_Naprawa_Kontekstu_Raport_20260728.md` (obliczone z
odpowiednich `decoding_accuracy_report.json`), N=100 na model, wariant zero-shot.

| Model | Średnia przed | Średnia po | Δ | Flagi przed | Flagi po | Δ |
|---|---|---|---|---|---|---|
| Claude Sonnet 5 | 47,9% | 47,4% | −0,6pp | 0,0% | 0,0% | 0,0pp |
| GPT-5.6-sol | 67,9% | 43,8% | **−24,1pp** | 35,8% | **0,0%** | **−35,8pp** |
| DeepSeek-v4-pro | 41,5% | 38,8% | −2,7pp | 6,7% | 3,0% | −3,6pp |
| Gemini-3.6-flash | 50,9% | 50,7% | −0,2pp | 3,0% | 6,1% | +3,0pp |

Naprawa architektonicznie poprawnego błędu (kontekst `recentFrames` faktycznie
zawierający wyłącznie ramki właściwego CAN ID) **nie poprawiła** wyników żadnego
modelu, a u GPT-5.6-sol spowodowała drastyczną regresję. Liczba prób, w których
GPT-5.6-sol w ogóle zaproponował dekompozycję na ≥2 sygnały dla bajtu 0x200 spadła
z 12/33 do 0/33 po naprawie. Robocza hipoteza: rzadkie próbkowanie (odstępy rzędu
dziesiątek sekund) w połączeniu z rzadkim przełączaniem flag (co 3–20s w naszym
generatorze) sprawia, że "czysty" kontekst bywa mało zróżnicowany, podczas gdy
przypadkowy szum z innych ID (przed naprawą) mógł nieumyślnie zwiększać pozorną
złożoność obserwowanych danych, skłaniając model do rozważenia bardziej złożonej
interpretacji.

### 4.4 Podsumowanie wyników negatywnych (sekcje 4.2–4.3)

Cztery niezależne interwencje na poziomie promptu/kontekstu — obejmujące zarówno
przykłady, jak i wymuszone procedury analityczne, jak i poprawę jakości danych
wejściowych — dały identyczny rezultat: **0,0% detekcji flag bitowych u Claude
Sonnet 5, niezależnie od strategii**. Sugeruje to, że problem nie leży w tym, co
mówimy modelowi, ani jak czysty jest dostarczony kontekst, lecz w tym, że modele
zawodnie wykonują systematyczną analizę entropii/niesekwencyjności bajtu z
surowego zapisu liczbowego, nawet gdy wprost o to poproszone.

### 4.5 Hybrydowy override klasyczny — wynik pozytywny

Źródło danych: `Eksperyment_4.1_DecodingAccuracy_Claude_OverrideV2_20260728_131600/decoding_accuracy_report.json`, N=100, Claude Sonnet 5, wariant zero-shot z override'em (Kierunek B, wersja poprawiona — zob. sekcja 5.3).

| Sygnał | Surowy LLM | LLM + override |
|---|---|---|
| RPM | 100,0% | 100,0% |
| Temperatura płynu | 97,1% | 97,1% |
| Przepustnica | 97,1% | 97,1% |
| Kąt skrętu | 100,0% | 100,0% |
| Prędkość | 97,0% | 97,0% |
| Lewy kierunkowskaz | 3,0% | 97,0% |
| Prawy kierunkowskaz | 0,0% | 97,0% |
| Światła | 3,0% | 93,9% |
| Drzwi kierowcy | 0,0% | 97,0% |
| Hamulec ręczny | 3,0% | 90,9% |
| **Średnia (10 sygnałów)** | **50,0%** | **96,7%** |

Wszystkie 5 sygnałów dyskretnych: F1 = 0 → **1,000**. Żaden sygnał ciągły nie
uległ pogorszeniu — override nie ingeruje w bajty, które klasyfikator uznaje za
skalarne. To pierwszy pozytywny wynik w całej serii pięciu testowanych wariantów.

---

## 5. Dyskusja

### 5.1 Dlaczego prompt engineering zawodzi, a override działa

Wyniki sekcji 4.2–4.4 wskazują, że problem nie jest deficytem INFORMACJI dostępnej
modelowi (dane były te same we wszystkich wariantach, tylko opakowanie promptu się
zmieniało) — jest deficytem SPOSOBU, w jaki model domyślnie przetwarza surowe dane
liczbowe. Klasyczny algorytm (sekcja 3.5) rozwiązuje dokładnie to samo zadanie
bezbłędnie i przy zerowym koszcie obliczeniowym w porównaniu z zapytaniem LLM —
sugeruje to, że zadanie samo w sobie NIE jest "trudne", tylko niedopasowane do
sposobu, w jaki obecne architektury LLM podchodzą do rozumowania numerycznego bez
zewnętrznego wsparcia.

### 5.2 Nieoczekiwana regresja GPT-5.6-sol po naprawie kontekstu

Wynik z sekcji 4.3 (regresja GPT-5.6-sol po architektonicznie poprawnej naprawie)
jest sam w sobie interesujący metodologicznie: pokazuje, że **poprawność kodu i
skuteczność modelu to niezależne osie** — naprawa błędu (zanieczyszczony kontekst)
nie gwarantuje poprawy wyniku, a w tym przypadku ujawniła wrażliwość modelu na
pozorną różnorodność danych wejściowych, niezależną od ich rzeczywistej trafności.
Wymaga to dalszej weryfikacji (np. bezpośredniego pomiaru entropii per-bajt w
oknach faktycznie widzianych przez model), którą pozostawiamy jako otwarty kierunek.

### 5.3 Iteracyjna korekta metodologii jako część wyniku

Warto odnotować przejrzyście: pierwsza wersja klasyfikatora override'u (sekcja
3.5) miała realny błąd (fałszywe trafienia na sygnałach ciągłych), znaleziony
dopiero w rzeczywistym teście N=100, nie w walidacji syntetycznej. Symulacja
walidacyjna w Pythonie, wykonana PRZED wdrożeniem pierwszej wersji, błędnie
modelowała dynamikę sygnału ciągłego (zakładała stały krok zmiany niezależnie od
odstępu czasowego między próbkami, podczas gdy rzeczywisty generator akumuluje
wiele małych kroków między rzadkimi próbkami) — co ujawniło się dopiero pod
obciążeniem prawdziwych danych. Wersja poprawiona (kryterium liczby przełączających
się bitów w przedziale [2,6], sekcja 3.5) została zwalidowana na ULEPSZONEJ
symulacji, uwzględniającej tę dynamikę, i potwierdzona empirycznie (fałszywe
trafienia: 29/34 → 1/34 na tym samym teście). Ta iteracja jest udokumentowana
celowo — ilustruje, że walidacja syntetyczna nie zastępuje testu na
rzeczywistych/reprezentatywnych danych.

### 5.4 Ograniczenia

- **Syntetyczne dane, nie rzeczywisty pojazd/DBC** — z powodów praktycznych
  (brak dostępu), co ogranicza pewność, że wynik uogólnia się na rzeczywiste,
  bardziej zróżnicowane wzorce kodowania.
- **Wąski zestaw testowy flag bitowych** — tylko jeden przypadek (5 flag w
  jednym bajcie, jeden CAN ID) — nieprzetestowane warianty: flagi w różnych
  pozycjach bajtu, różna liczba flag, flagi rozproszone na wielu bajtach.
- **N=100 na wariant/model** — pojedynczy przebieg per konfiguracja; nie
  przeprowadzono wielokrotnych powtórzeń dla oszacowania wariancji między
  przebiegami (zob. sekcja 6 — plan przyszłych powtórzeń).
- **Klasyfikator override'u dostrojony do jednego typu sygnału** — próg [2,6]
  bitów i próg skoku >3 są dobrane pod obserwowaną dynamikę tego konkretnego
  zestawu syntetycznego; ich uogólnienie na inne konfiguracje wymaga dalszych
  testów.

---

## 6. Praca przyszła (planowana, NIE wykonana w obecnej wersji)

**Eksperyment 4.3 — destylacja symboliczno-neuronowa przez fine-tuning.**
Proponujemy sprawdzenie, czy zdolność wykrywania flag bitowych — dziś dostarczana
przez zewnętrzny klasyfikator klasyczny (sekcja 3.5, 4.5) — może zostać
"wszczepiona" bezpośrednio w wagi modelu LLM przez fine-tuning na korpusie
etykietowanym automatycznie przez ten sam klasyfikator (bez udziału człowieka lub
droższego LLM w procesie etykietowania). Pełny opis metodologii, wymaganego
rozszerzenia syntetycznego generatora danych, oraz analiza kosztów (szacunkowo
~120 zł kosztu API i ~91 godzin sekwencyjnego czasu sprzętu dla pełnego planu na
4 modelach, możliwe do skrócenia przez zrównoleglenie na wielu jednostkach ESP32
podłączonych do tej samej magistrali CAN) — w dokumentach towarzyszących:
`Eksperyment_4.3_Propozycja_Bootstrapped_FineTuning_20260728.md` i
`Eksperyment_4.3_Bilans_Koszty_Korzysci_Infografika_20260728.pdf`.

Kluczowe pytanie badawcze tego eksperymentu, nierozstrzygnięte przez wyniki
niniejszej pracy: czy douczony model **generalizuje** zasadę (poprawnie
dekoduje konfiguracje bit-packingu NIEWIDZIANE podczas treningu), czy tylko
zapamiętuje konkretne przykłady treningowe — rozróżnienie kluczowe dla wartości
naukowej (nie tylko inżynierskiej) tego kierunku.

**Dodatkowe kierunki (niezrealizowane, zarysowane)**: test klasyfikatora
override'u na szerszym zestawie konfiguracji bit-packingu (różne pozycje
bajtu, różna liczba flag, flagi rozproszone na wielu bajtach).

Niezależna, sprzętowa weryfikacja czasów reakcji systemu (Eksperyment 1.2) przez
analizator stanów logicznych — zaplanowana w poprzedniej wersji tego dokumentu —
została **wykonana** w międzyczasie; wyniki w Dodatku A.

---

## 7. Wnioski

Cztery niezależne strategie promptowania (zero-shot, few-shot, wymuszona analiza
entropii, naprawa jakości kontekstu) konsekwentnie zawodzą w tym samym, wąskim
zadaniu — wykrywaniu upakowanych flag bitowych w ramce CAN — u wiodącego
testowanego modelu (Claude Sonnet 5: 0,0% detekcji w każdym wariancie). Wynik ten
sugeruje twardą granicę czysto-promptowego podejścia do tego typu zadania.
Deterministyczny klasyfikator klasyczny, działający równolegle do modelu (nie
zamiast niego), rozwiązuje to samo zadanie ze skutecznością 90,9–97,0%, bez
szkody dla pozostałych sygnałów — pokazując, że zadanie samo w sobie jest
rozwiązywalne, tylko niedopasowane do domyślnego sposobu przetwarzania danych
liczbowych przez LLM. Czy tę zdolność można efektywnie przenieść w wagi modelu
przez fine-tuning na korpusie etykietowanym przez ów klasyfikator — pozostaje
otwartym pytaniem badawczym, planowanym jako bezpośrednia kontynuacja tej pracy.

---

## Dodatek A — Niezależna weryfikacja sprzętowa czasu Hot Execution (Eksperyment 1.2)

Choć Eksperyment 1.2 dotyczy odrębnego zagadnienia (czas reakcji systemu na
znaną już regułę, `t_resp`, w odróżnieniu od głównego przedmiotu tej pracy —
trafności identyfikacji nieznanej reguły przez LLM), stanowi część tej samej
infrastruktury badawczej i motywuje jej kluczowe założenie: że czas oczekiwania
na odpowiedź LLM podczas fazy Cold Start (sekundy) dominuje nad czasem reakcji
"na gorąco" po wdrożeniu już poznanej reguły (mikrosekundy) — stąd zasadność
skupienia głównego wysiłku badawczego (sekcje 4–6) na trafności LLM, nie na
efektywności sprzętowej.

Źródło danych: `Eksperyment_1.2_Weryfikacja_AnalizatorLogiczny_20260728/`.

**Metodologia**: niezależny, zewnętrzny pomiar czasu `t_resp` (od przerwania
sprzętowego MCP2515 INT do reakcji GPIO) analizatorem stanów logicznych (klon
Saleae Logic, 8 kanałów, chip fx2lafw, obsługiwany przez sigrok-cli), wykonany
RÓWNOLEGLE do i niezależnie od wcześniejszego pomiaru wewnętrznym zegarem ESP32
(`esp_timer_get_time()`, zob. `Eksperyment_1.2_Hot_Execution_20260725_163438/`).
Dwie wcześniejsze próby takiej niezależnej weryfikacji (oscyloskop Hantek
1008C — zawodny software'owy mechanizm wyzwalania; analizator ATK-Logic DL16 —
trwale niesprawny firmware) zostały porzucone we wcześniejszych etapach
projektu; przedstawiony tu analizator jest pierwszym, który skutecznie domknął
ten wątek.

**Wynik (N=570)**:

| Metoda pomiaru | Średnia t_resp | Odch. std. | Mediana | N |
|---|---|---|---|---|
| Timer wewnętrzny ESP32 | 109,70 µs | 1,52 µs | 109,0 µs | 1000 |
| Analizator logiczny (zewnętrzny) | 112,45 µs | 1,54 µs | 112,0 µs | 570 |

Dwie całkowicie niezależne metody pomiarowe dają praktycznie zgodny wynik —
różnica średnich (~2,75 µs, ~2,5% względnie) mieści się w oczekiwanej
rozdzielczości obu metod (obie ~1 µs) oraz typowym opóźnieniu propagacji
sygnału elektrycznego względem programowego znacznika czasu branego w
przerwaniu. Odchylenia standardowe obu metod są niemal identyczne (~1,5 µs).
Stanowi to pierwsze skuteczne, sprzętowo niezależne potwierdzenie wartości
`t_resp ≈ 110 µs` raportowanej w Eksperymencie 1.2.

**Napotkane trudności techniczne** (odnotowane dla przejrzystości metodologicznej,
zgodnie z konwencją tej pracy — zob. sekcja 5.3): użyty analizator wykazał twardy
limit ciągłego przechwytu przy wysokiej częstotliwości próbkowania (24 MHz —
urządzenie dostarczało jedynie ok. 126 tys. próbek niezależnie od zadanej
liczby, prawdopodobnie ograniczenie bufora/przepustowości USB tego klonu),
rozwiązany obniżeniem częstotliwości próbkowania do 1 MHz (nadal wystarczającej
rozdzielczości dla mierzonego zjawiska rzędu ~110 µs), przy której urządzenie
dostarczało pełne żądane próbki bez obcięcia. Sprzętowy mechanizm wyzwalania
tego urządzenia okazał się niewiarygodny w tym środowisku — zastąpiono go
przechwytem ciągłym z analizą zboczy wykonywaną programowo po fakcie.

### A.1 Rozbicie t_resp na składowe — podsłuch magistrali SPI

Rozszerzono powyższy pomiar o 4 dodatkowe kanały analizatora podłączone do
magistrali SPI między ESP32 a kontrolerem MCP2515 (SCLK→GPIO18, MOSI→GPIO23,
MISO→GPIO19, CS→GPIO5 — domyślne piny VSPI). Biblioteka sterownika MCP2515
(`autowp-mcp2515`) korzysta z zegara SPI 10 MHz; przy próbkowaniu 1 MHz
(konieczne ze względu na ograniczenie z sekcji A, opisane wyżej) nie da się
zdekodować pojedynczych bajtów SPI (do tego potrzeba ~24–48 MHz), ale w
zupełności wystarcza to do zlokalizowania GRANIC poszczególnych transakcji SPI
po zboczach linii CS — co jest wystarczające do rozbicia `t_resp` na składowe
czasowe.

Źródło danych: `Eksperyment_1.2_Weryfikacja_AnalizatorLogiczny_20260728/spi_breakdown/`, N=300 (wszystkie zdarzenia przechwycone w jednym oknie 2-sekundowym).

Wszystkie 300 zaobserwowanych zdarzeń wykazały identyczną liczbę transakcji SPI
(dokładnie 5) w odpowiedzi na jedną ramkę wyzwalającą:

| Składowa | Czas (średnia ± odch. std.) | Udział |
|---|---|---|
| INT → pierwsza transakcja SPI | 9,90 ± 0,46 µs | 8,8% |
| Suma czasu aktywnych transakcji SPI (5 transakcji) | 47,38 ± 1,28 µs | 42,2% |
| Przerwy MIĘDZY transakcjami SPI | 48,09 ± 1,23 µs | 42,8% |
| Ostatnia transakcja → reakcja GPIO | 7,02 ± 0,52 µs | 6,2% |
| **Razem (t_resp)** | **112,40 ± 1,56 µs** | **100%** |

**Wynik nieoczywisty**: niemal połowa całkowitego czasu `t_resp` (42,8%) to nie
czas transferu danych po magistrali SPI, tylko przerwy MIĘDZY kolejnymi
transakcjami — czysto softwarowy narzut biblioteki sterownika (sprawdzanie
warunków, rozgałęzienia, koszt wywołań funkcji) między kolejnymi operacjami
odczytu rejestrów kontrolera. Sam transfer danych po magistrali SPI (42,2%)
jest niemal dokładnie równoważny co do wielkości temu narzutowi softwarowemu.
Uszczegóławia to wcześniejszy wniosek Eksperymentu 1.2 ("~99% kosztu t_resp to
SPI+firmware, nie sam kontroler CAN") — z tych ~99%, połowa to faktyczny
transfer SPI, a połowa to narzut sterujący biblioteki, sugerując, że
optymalizacja liczby/organizacji transakcji SPI (np. odczyt całego rekordu w
jednej transakcji zamiast pięciu oddzielnych) mogłaby obniżyć `t_resp` nawet
bez zmiany sprzętu — potencjalny kierunek dalszych badań, niezrealizowany w
obecnej wersji tej pracy.

**Czy warto zaimplementować tę optymalizację?** Kontroler MCP2515 udostępnia
sprzętową instrukcję "READ RX BUFFER", pozwalającą odczytać identyfikator, DLC
i dane ramki w JEDNEJ transakcji SPI, z automatycznym skasowaniem odpowiedniej
flagi przerwania jako udokumentowanym efektem ubocznym — zaprojektowaną
specjalnie w celu redukcji liczby transakcji. Użyta biblioteka sterownika nie
wykorzystuje tej optymalizacji, co jest technicznie możliwe do poprawienia.
Wartość praktyczna takiej zmiany dla architektury tego projektu jest jednak
niska: nawet przy optymistycznej redukcji `t_resp` (np. do ~70–80 µs,
niezweryfikowane), pozostaje to ~20–40 tysięcy razy szybsze niż opóźnienie
zapytania LLM w fazie Cold Start, które i tak dominuje cały system — co
potwierdza (nie zmienia) pierwotny wniosek Eksperymentu 1.2, że czas reakcji
"na gorąco" nie jest wąskim gardłem architektury. Traktujemy to jako
zwalidowaną, testowalną hipotezę na przyszłość, nie jako uzasadniony kierunek
dalszej pracy w ramach niniejszego projektu.

---

## Dodatek B — Rozszerzenie infrastruktury badawczej poza główny wątek: test zasięgu radiowego (Eksperyment 3.1) i przygotowanie profilowania JTAG (Eksperyment 5.1)

Analogicznie do Dodatku A (Eksperyment 1.2), poniższe prace dotyczą odrębnych
zagadnień infrastruktury sprzętowej projektu CAN-Edge-AI, nie głównego
przedmiotu tej pracy (trafność LLM w dekodowaniu flag bitowych). Odnotowane tu
dla kompletności obrazu prowadzonych równolegle prac — żadna z nich nie
dostarcza jeszcze danych pomiarowych na moment tej wersji dokumentu.

### B.1 Eksperyment 3.1 — wpływ odległości/przeszkód na opóźnienia i stratność pakietów (WiFi/BLE)

**Status: przygotowanie zakończone (firmware + aplikacja pomiarowa + serwer
odbiorczy), pomiar terenowy jeszcze NIE wykonany.**

- Firmware `esp_experiment_3_1_wifi/esp_experiment_3_1_wifi.ino`: ESP32 jako
  własny punkt dostępowy (SoftAP), prosty protokół ping-pong przez UDP
  (`PING:<seq>` → `PONG:<seq>`), mierzący surowy zasięg radia ESP32
  niezależnie od konkretnego routera pośredniczącego.
- Pierwsza w tym projekcie aplikacja mobilna: `android_experiment_3_1/`
  (Kotlin) — mierzy RTT na zegarze telefonu (nie wymaga synchronizacji
  zegarów obu urządzeń), RSSI, oraz — po konsultacji z użytkownikiem —
  geolokalizację GPS jako niezależną, obiektywną weryfikację zmierzonego
  dystansu (z jawnie udokumentowanym ograniczeniem dokładności ~3–5 m,
  typowo gorszym w scenariuszu NLOS z powodu wielodrogowości sygnału w
  pobliżu zabudowań — akurat tam, gdzie GPS miałby najbardziej pomóc).
  Wyniki zapisywane lokalnie do CSV jako źródło prawdy (odporne na
  przerwanie łącza pod testem), eksport na żądanie po zakończeniu serii
  (POST do prostego serwera domowego `server_receiver.py`, lub natywny
  ekran "Udostępnij" Androida).
- Przed napisaniem jakiegokolwiek kodu spisano 10 otwartych kwestii
  metodologicznych do dyskusji z wykładowcą
  (`Pytania_Do_Wykladowcy_Eksperyment_3.1_20260729.md`) — m.in. architektura
  testu (SoftAP vs router pośredniczący, bliższy docelowemu wdrożeniu), role
  fizyczne (ESP32 stacjonarne + telefon mobilny, odwrotnie niż dosłowne
  brzmienie metodyki), zredukowany rozmiar próby względem metodyki
  (proponowane 1000–2000 zamiast 10 000 pakietów/punkt) oraz interpretacja
  zapisu "Liczba pomiarów do weryfikacji: 10" w metodyce (10 niezależnych
  powtórzeń całego przebiegu, czy ogólna wskazówka liczebności próby).
- Aplikacja Android zbudowana i skompilowana (build debug APK powiódł się);
  pomiar terenowy oczekuje odpowiedzi wykładowcy na powyższe kwestie
  metodologiczne przed uruchomieniem.

### B.2 Eksperyment 5.1 — profilowanie JTAG (SystemView/esp_apptrace) i efekt obserwatora w pomiarze CPU

**Status: DOKOŃCZONY — infrastruktura JTAG zweryfikowana end-to-end, pomiar
IDLE/PARSING wykonany w 3 wariantach porównawczych.**

Jak odnotowano przy Eksperymencie 5.1 (profilowanie CPU/RAM), narzędzia
trace FreeRTOS (`vTaskGetRunTimeStats`) okazały się niewiarygodne dla
architektury pollującej używanej w tym firmware (zerowy przyrost licznika
czasu zadania mimo aktywnego przetwarzania — `loopTask` nigdy nie oddaje
sterowania), a pełny profil per-zadanie wymagałby JTAG (ESP-Prog +
SystemView/esp_apptrace), co z kolei wymaga przejścia z czystego szkicu
Arduino na natywny projekt ESP-IDF. Port firmware (`esp_experiment_5_1_jtag/`,
logika klasyfikacji 1:1 z `esp_experiment_5_1.ino`, sterownik MCP2515
zwendorowany — nie przepisany od zera, dla uniknięcia błędów w rejestrach
SPI kontrolera) został dokończony i skompilowany. Fizyczne połączenie JTAG
(ESP-Prog-2, natywny interfejs USB Espressif) zweryfikowane w pełni: oba
rdzenie ESP32 wykrywane przez OpenOCD, przechwyt SystemView potwierdzony
testowo (0 utraconych bajtów danych trace).

**Eksperyment poboczny: czy sam pomiar JTAG zniekształca mierzoną
wielkość?** Uruchomiono ten sam skrypt pomiarowy (`run_experiment_5_1.py`,
bez modyfikacji — identyczny protokół sterujący przez CAN) w czterech
konfiguracjach firmware, N=40 (20×IDLE, 20×PARSING) każda, żeby ocenić,
czy samo podłączenie i użycie JTAG/SystemView zaburza wynik pomiaru
obciążenia CPU. Pierwszy przebieg (3 warianty) ujawnił niestabilność w
wariancie kontrolnym (opis diagnozy niżej); po jej usunięciu wszystkie
4 warianty powtórzono na naprawionej architekturze:

| Wariant | CPU IDLE | CPU PARSING | Status |
|---|---|---|---|
| Oryginalny firmware Arduino (bez ESP-IDF/JTAG) | 0,598% | 0,599% | 40/40 OK |
| Port ESP-IDF, `apptrace`/JTAG całkowicie wyłączony | 0,817% | 0,823% | 40/40 OK |
| Port ESP-IDF + JTAG podłączony, SystemView bierny (bez streamu) | 1,789% | 1,799% | 40/40 OK |
| Port ESP-IDF + JTAG, SystemView aktywnie nagrywający | 2,476% | 2,500% | 40/40 OK |

**Diagnoza i naprawa niestabilności wariantu kontrolnego.** Pierwszy
przebieg wariantu "bez apptrace" (miał być najprostszym punktem
odniesienia) kończył się powtarzalnie (2/2 prób) zadziałaniem watchdoga
zadań systemowych po 45–140 s działania. Pełny, wielolinijkowy raport
błędu (przechwycony dedykowanym skryptem diagnostycznym logującym każdą
linię konsoli, nie tylko pierwszą) wskazał precyzyjnie: zadanie systemowe
`IDLE0` rdzenia CPU0 było głodzone przez zadanie `main`, które nigdy nie
oddawało sterowania. Przyczyna źródłowa: ręcznie napisany punkt wejścia
`app_main()` uruchamiał pętlę `loop()` bezpośrednio w zadaniu ESP-IDF
"main" (domyślnie przypiętym do CPU0) zamiast pozwolić komponentowi
`arduino-esp32` dostarczyć własny, poprawny `app_main()`, który tworzy
dedykowane zadanie `loopTask` na drugim rdzeniu (CPU1) i zwalnia CPU0 dla
jego własnego zadania bezczynności. Naprawa (`CONFIG_AUTOSTART_ARDUINO=y`
+ usunięcie ręcznego `app_main()`) w pełni usunęła problem — zweryfikowano
60/60 pomiarów bez jednego ostrzeżenia watchdoga po poprawce, wobec 40/60
ostrzeżeń przed nią (nieszkodliwych — domyślnie wyłączona panika watchdoga
oznaczała, że firmware nie ulegał realnej awarii, tylko generował hałaśliwy
log przerywający ciągłość skryptu pomiarowego). Naprawiona architektura
dała wyniki praktycznie identyczne z wynikami sprzed naprawy (różnica
rzędu 0,01 punktu procentowego), co dodatkowo potwierdza, że sam pomiar
`micros()` był wiarygodny niezależnie od architektury planisty zadań —
usterka psuła wyłącznie ciągłość długich przebiegów, nie poprawność
pojedynczego pomiaru.

**Wynik — w pełni potwierdzony, monotoniczny i odtwarzalny efekt
obserwatora**, rozłożony na trzy niezależne składowe:
- +0,22 punktu procentowego: sam toolchain ESP-IDF + Arduino-jako-komponent
  względem Arduino IDE (0,60%→0,82%) — nie dotyczy JTAG w ogóle, to koszt
  wyboru środowiska budowania.
- +0,97 punktu procentowego: samo skompilowanie firmware z kanałem trace
  JTAG skonfigurowanym, nawet bez aktywnego przechwytu danych
  (0,82%→1,79%) — zaskakująco duży koszt samej *gotowości* kanału trace,
  większy niż koszt jego aktywnego wykorzystania.
- +0,69 punktu procentowego: aktywne strumieniowanie danych SystemView
  przez JTAG (1,79%→2,49%).

Wniosek metodyczny analogiczny do sekcji 5.3 tej pracy: narzędzie
pomiarowe (tu: sonda JTAG) nie jest neutralne wobec mierzonej wielkości —
klasyczny problem "obserwator wpływa na obserwowane zjawisko" znany z
profilowania systemów wbudowanych, tu zmierzony ilościowo i w pełni
rozłożony na składowe przyczyny.

**Rekomendacja robocza dla docelowej tabeli metodyki (Grupa 5)**:
oryginalny pomiar firmware Arduino (0,6%) jako główny, najmniej inwazyjny
wynik; pełna tabela 4 wariantów jako materiał do dyskusji o granicach
metod profilowania systemów wbudowanych klasy Edge, nie jako zamiennik
wyniku głównego.

### B.3 Trzeci stan metodyki ("OTA Update") — zrealizowany

Metodyka (Grupa 5) wymaga trzech stanów pomiarowych: *Idle*, *Parsing &
Filtering* oraz *OTA Update* ("moment aktualizacji i kompilacji nowej
reguły w pamięci"). Trzeci stan pozostawał niezrealizowany od początku
istnienia Eksperymentu 5.1 w tym projekcie — mechanizm aplikowania reguły
LLM bezpośrednio na urządzeniu brzegowym nie istniał w architekturze;
reguły LLM żyły wyłącznie po stronie komputera (`DecodingAccuracyRunner`,
sekcja 3.5 tej pracy).

**Projekt i implementacja.** Rozszerzono firmware (`esp_experiment_5_1_jtag`)
o tryb `MODE_OTA`, w którym urządzenie przyjmuje przez CAN i "kompiluje"
regułę dekodującą o polach identycznych ze strukturą `LlmSignalRule`
używaną po stronie komputera (`byteIdx`, `byteLen`, `littleEndian`,
`isSigned`, `bitMask`, `scale`, `offset`) — świadoma decyzja spójności
architektonicznej z resztą systemu zamiast projektowania nowego formatu
reguły od zera. Ponieważ jedna reguła nie mieści się w pojedynczej,
8-bajtowej ramce CAN, jej dostawa wymaga czterech ramek sterujących
(nagłówek, maska bitowa, skala, przesunięcie + wyzwolenie kompilacji) —
naturalne ograniczenie przepustowości magistrali, warte odnotowania jako
praktyczny aspekt projektowania protokołu OTA dla systemów CAN. Sama
"kompilacja" to rzeczywisty zapis reguły do tabeli aktywnych reguł na
urządzeniu (do ośmiu jednocześnie, analogicznie do tabeli klasyfikacji
stanu *Parsing*), a nie operacja pusta — mierzona tym samym mechanizmem
bezpośredniego pomiaru czasu co klasyfikacja ramek w stanie *Parsing*.

**Wynik (N=20, to samo binarium i konfiguracja co pozostałe dwa stany dla
uczciwego porównania)**:

| Stan | CPU [%] | RAM użyte [kB] | Flash [kB] | Parametr obciążenia |
|---|---|---|---|---|
| Idle | 1,792 | 29,50 | 272,4 | 50 ramek/s (bez dodatkowej pracy) |
| Parsing & Filtering | 1,800 | 29,50 | 272,4 | 50 ramek/s (klasyfikacja per-ID) |
| OTA Update | 1,487 | 29,50 | 272,4 | 10 reguł/s = 40 ramek sterujących/s |

Wszystkie pomiary bez błędu (100% dostarczonych i skompilowanych reguł w
każdym oknie pomiarowym). Zużycie RAM i Flash jest identyczne między
stanami — tabele reguł i statystyk są przydzielane statycznie niezależnie
od aktywnego trybu, więc różni je tylko faktyczne wykorzystanie, nie sam
fakt alokacji. Niższe CPU stanu *OTA Update* względem pozostałych dwóch
NIE oznacza, że kompilacja reguły jest tańsza obliczeniowo niż pojedyncza
klasyfikacja — pojedyncza kompilacja to więcej pracy niż pojedyncza
klasyfikacja ramki. Różnica wynika z niższej częstotliwości zdarzeń
przyjętej dla tego stanu (10 reguł/s, realistyczne dla rzadkich
aktualizacji reguł) względem ciągłego strumienia ramek danych (50/s) dla
pozostałych dwóch stanów — każdy stan ma własny, jawnie udokumentowany
parametr obciążenia, spójnie z konwencją przyjętą w całej tej pracy.

**Zastrzeżenie terminologiczne**: "OTA Update" oznacza tu aktualizację
pojedynczej reguły interpretacji sygnału (dosłowne brzmienie metodyki
"kompilacji nowej reguły"), nie klasyczną aktualizację całej binarki
firmware (typowe znaczenie skrótu OTA w ekosystemie ESP32) — rozróżnienie
istotne dla poprawnej interpretacji wyniku w finalnej pracy.

Kod: `esp_experiment_5_1_jtag/main/main.cpp` (`MODE_OTA`, struktura
`OtaRule`), `esp_experiment_5_1/run_experiment_5_1_ota.py` (sterownik
pomiaru — celowo osobny od `run_experiment_5_1.py`, który pozostaje
niezmieniony dla zachowania porównywalności już opublikowanych wyników
stanów *Idle*/*Parsing*). Dane: `Eksperyment_5.1_3Stany_20260730/`,
`Eksperyment_5.1_OTA_20260730/`.

---

## Dostępność danych i kodu

Pełny kod źródłowy (aplikacja C++/Qt6, firmware ESP32, skrypty generatora ruchu
i analizy), surowe dane (raporty JSON ze wszystkimi próbami, N=100 na
konfigurację) oraz niniejszy dokument są dostępne w repozytorium projektu
(`MagistralaCAN4` — **[DO UZUPEŁNIENIA: link do publicznego repo/DOI przed
submisją]**).

---

## Podziękowania

**[DO UZUPEŁNIENIA]**

---

## Literatura

**[DO UZUPEŁNIENIA — pełny przegląd literatury i formatowanie cytowań zgodnie z
wymogami docelowego czasopisma; żadne cytowanie nie zostało tu wprowadzone przez
autora automatycznego (Claude), by uniknąć ryzyka nieistniejących/nieprecyzyjnych
odniesień bibliograficznych]**

---

## 8. Historia zmian tego dokumentu

- **2026-07-28, wersja 0.1** — pierwsza wersja robocza, na podstawie ukończonych
  eksperymentów: baseline 4 modeli (zero-shot), few-shot, entropy-analysis,
  context-fix (4 modele), hybrydowy override v1 i v2 (Claude). Sekcja 6
  (fine-tuning) i towarzyszący eksperyment weryfikacji sprzętowej (analizator
  logiczny) oznaczone jako planowane/w trakcie, nie zawarte w wynikach.
- **2026-07-28, wersja 0.2** — dodano Dodatek A: niezależna weryfikacja
  sprzętowa czasu Hot Execution (Eksperyment 1.2) analizatorem stanów
  logicznych, N=570, potwierdzająca wcześniejszy pomiar wewnętrznym zegarem
  ESP32 (różnica średnich ~2,5%). Pierwsze skuteczne domknięcie wątku
  niezależnej weryfikacji sprzętowej, po dwóch wcześniej porzuconych próbach
  (Hantek 1008C, ATK-Logic DL16). Sekcja 6 zaktualizowana — usunięto ten punkt
  z listy niezrealizowanych kierunków.
- **2026-07-28, wersja 0.3** — dodano Dodatek A.1: rozbicie `t_resp` na
  składowe czasowe przez podsłuch magistrali SPI (ESP32↔MCP2515), N=300,
  wszystkie zdarzenia z identyczną liczbą 5 transakcji SPI. Nieoczywisty
  wynik: ~43% czasu to przerwy międzytransakcyjne (narzut softwarowy
  biblioteki), nie sam transfer SPI (~42%) — sugeruje potencjalną optymalizację
  liczby transakcji SPI jako niezrealizowany kierunek dalszych badań.
- **2026-07-28, wersja 0.4** — dodano dyskusję zasadności implementacji
  optymalizacji SPI (5→1 transakcja przez instrukcję sprzętową MCP2515 "READ
  RX BUFFER") — technicznie uzasadniona, ale niska wartość praktyczna wobec
  dominacji kosztu Cold Start (LLM) nad Hot Execution; rekomendacja:
  udokumentować jako testowalną hipotezę, nie wdrażać w ramach tej pracy.
- **2026-07-29, wersja 0.5** — dodano Dodatek B: rozszerzenie infrastruktury
  badawczej poza główny wątek pracy — przygotowanie (bez jeszcze wykonanego
  pomiaru) Eksperymentu 3.1 (zasięg WiFi/BLE, pierwsza aplikacja mobilna w
  projekcie, 10 otwartych kwestii metodologicznych spisanych do dyskusji z
  wykładowcą) oraz nieukończony szkielet natywnego projektu ESP-IDF pod
  przyszłe profilowanie JTAG Eksperymentu 5.1. Żadna z tych prac nie zmienia
  wyników ani wniosków głównego wątku (sekcje 1–7) — dodane wyłącznie dla
  kompletności obrazu równolegle prowadzonych prac.
- **2026-07-30, wersja 0.6** — zaktualizowano Dodatek B.2 (Eksperyment 5.1):
  z "przygotowanie niedokończone" na pełny wynik. Infrastruktura JTAG/
  SystemView (ESP-Prog-2) zweryfikowana end-to-end. Nowy, dwuczęściowy
  wynik: (1) częściowo potwierdzony efekt obserwatora — aktywne
  strumieniowanie SystemView podnosi mierzone obciążenie CPU o ok. 0,7pp
  ponad sam fakt włączenia kanału trace w konfiguracji; (2) nieoczekiwany,
  ważniejszy wynik uboczny — wariant kontrolny bez mechanizmu trace okazał
  się niestabilny (powtarzalny w 2/2 próbach watchdog crash), podczas gdy
  oba warianty z aktywnym JTAG/apptrace przeszły pełne 40/40 pomiarów bez
  błędu; przyczyna niezdiagnozowana, jawnie oznaczona jako otwarty problem,
  nie wynik do cytowania. Żadna zmiana nie dotyczy głównego wątku pracy
  (sekcje 1–7, LLM/flagi bitowe) — Dodatek B pozostaje materiałem
  pobocznym, dokumentującym równoległą infrastrukturę badawczą projektu.
- **2026-07-30, wersja 0.7** — Dodatek B.2 zaktualizowany: przyczyna
  niestabilności wariantu kontrolnego z wersji 0.6 w pełni zdiagnozowana
  (błędny punkt wejścia `app_main()` głodzący zadanie bezczynności rdzenia
  CPU0) i naprawiona; wszystkie 4 warianty JTAG powtórzone na naprawionej
  architekturze, dając w pełni czystą (0 błędów), monotoniczną tabelę
  efektu obserwatora rozłożonego na trzy niezależne składowe (toolchain,
  gotowość kanału trace, aktywne strumieniowanie). Dodano nowy Dodatek
  B.3: trzeci, dotąd niezrealizowany stan metodyki Grupy 5 ("OTA Update")
  zaprojektowany i wdrożony — urządzenie przyjmuje i "kompiluje" reguły
  LLM przez CAN (format pól identyczny z `LlmSignalRule` używanym po
  stronie komputera), z pełnym wynikiem trzech stanów (Idle/Parsing/OTA)
  na tym samym binarium. Żadna zmiana nie dotyczy głównego wątku pracy
  (sekcje 1–7).
