# Eksperyment 4.3 — uzasadnienie, wartość naukowa i dydaktyczna, wstęp teoretyczny

Data: 2026-07-28
Status: materiał uzasadniający i przygotowawczy do dyskusji z wykładowcą — towarzyszy
propozycji technicznej `Eksperyment_4.3_Propozycja_Bootstrapped_FineTuning_20260728.md`
i bilansowi kosztów `Eksperyment_4.3_Bilans_Koszty_Korzysci_Infografika_20260728.pdf`.

---

## 1. Czego dotyczy eksperyment — krótkie streszczenie

Eksperyment 4.1 wykazał, że modele LLM (Claude, GPT, DeepSeek, Gemini) konsekwentnie
zawodzą przy rozpoznawaniu bajtów CAN pakujących kilka niezależnych flag bitowych —
zamiast dekompozycji proponują pojedynczą wartość skalarną. Cztery kolejne, niezależne
interwencje na poziomie promptu (zero-shot, few-shot, wymuszona analiza entropii,
naprawa zanieczyszczenia kontekstu) **nie zmieniły tego wyniku**. Dopiero deterministyczny,
klasyczny klasyfikator (Kierunek B) — działający OBOK modelu, nie zamiast niego —
podniósł skuteczność z 0-50% do ~97%.

**Eksperyment 4.3 stawia pytanie: czy to, co dziś robi klasyczny kod jako zewnętrzna
łatka, da się "wszczepić" bezpośrednio w wagi modelu przez fine-tuning?** Zamiast uczyć
model przez przykłady w promptcie (co już zawiodło) albo przez dodatkową instrukcję
(co też zawiodło), proponujemy wygenerować DUŻY zbiór poprawnie oznaczonych przykładów
— etykietowanych automatycznie przez już zwalidowany klasyfikator, nie przez człowieka
ani przez droższy LLM — i na tej podstawie douczyć model.

### Ramy i założenia
- **Zakres modeli**: GPT-4.1/o4-mini (SFT/RFT przez API OpenAI) i DeepSeek (self-hosted,
  open-weight) — jedyne z 4 testowanych modeli z realnie dostępnym fine-tuningiem.
  Gemini częściowo (Vertex AI, ograniczony zakres modeli), Claude bez publicznego API.
- **Założenie kluczowe**: obecny syntetyczny mini-DBC (3 CAN ID, 1 przypadek flag
  bitowych) jest ZA WĄSKI do fine-tuningu bez ryzyka zapamiętywania zamiast generalizacji
  — warunkiem wstępnym jest rozbudowa generatora o wiele różnych konfiguracji
  bit-packingu.
- **Ewaluacja**: identycznym harnessem co Eksperyment 4.1 (N=100, zero-shot, ten sam
  format promptu/odpowiedzi) — pełna porównywalność z już opublikowanymi wynikami.
- **Poza zakresem**: prawdziwe pojazdy/DBC (niedostępne w tym projekcie od początku,
  za zgodą wykładowcy), stan "OTA Update" (nie istnieje mechanizm aplikacji reguły na
  urządzeniu).

---

## 2. Wstęp teoretyczny

### 2.1 Destylacja wiedzy (knowledge distillation)

Destylacja wiedzy to technika, w której jeden model ("nauczyciel") przekazuje swoją
wiedzę drugiemu ("uczeń") — klasycznie: duży, kosztowny model uczy mały, tani model,
tak by ten drugi osiągał zbliżoną skuteczność mniejszym kosztem inferencji (Hinton,
Vinyals, Dean, *Distilling the Knowledge in a Neural Network*, 2015). W naszym
przypadku wariant jest inny i mniej typowy: **nauczycielem jest nie sieć neuronowa,
tylko deterministyczny algorytm symboliczny** (klasyczny klasyfikator flag bitowych).
To bliższe podejściu "symbolic-to-neural distillation" / uczeniu przez demonstrację
reguł — nauczyciel nie jest kosztowny obliczeniowo (klasyczny kod, koszt bliski zeru),
ale ma WĄSKĄ, dobrze zdefiniowaną kompetencję (tylko ten jeden typ wzorca), w
odróżnieniu od klasycznej destylacji, gdzie nauczyciel jest ogólny, a uczeń węższy.

### 2.2 Fine-tuning kontra prompt engineering

Prompt engineering (w tym few-shot, instrukcje strukturalne jak nasza entropy-analysis)
modyfikuje TYLKO kontekst zapytania — wagi modelu pozostają niezmienione, model musi
"na bieżąco" wywnioskować zasadę z przykładów/instrukcji w oknie kontekstu. Fine-tuning
modyfikuje same wagi modelu na podstawie wielu przykładów treningowych — zasada
zostaje "wypalona" w parametrach, nie musi być za każdym razem re-derywowana z
kontekstu. Nasze 4 nieudane próby prompt engineeringu (opisane w
`Eksperyment_4.1_Naprawa_Kontekstu_Raport_20260728.md`) sugerują, że problem NIE jest
kontekstowy (za mało/złych informacji w oknie), tylko **głębiej osadzony w sposobie,
w jaki model domyślnie interpretuje dane liczbowe** — co czyni fine-tuning naturalnym
następnym krokiem: zmienia PRIORY modelu, nie tylko dostępny kontekst.

### 2.3 LLM a precyzyjne rozumowanie numeryczne

Znana w literaturze słabość dużych modeli językowych to zawodność w zadaniach
wymagających precyzyjnej, systematycznej analizy surowych danych liczbowych (w
odróżnieniu od zadań językowych/semantycznych, gdzie LLM są silne) — modele mają
tendencję do rozpoznawania "znajomych" wzorców statystycznych (np. "bajt = jedna
wartość") zamiast przeprowadzania systematycznej analizy bit po bicie, nawet gdy
wprost o to poproszone. Nasze wyniki (Eksperyment 4.1, entropy-analysis) są
bezpośrednią, empiryczną ilustracją tego zjawiska w konkretnej, praktycznej domenie
(dekodowanie magistrali CAN), nie tylko w abstrakcyjnych testach syntetycznych.

### 2.4 Kontekst domenowy: reverse engineering magistrali CAN

Magistrala CAN (Controller Area Network) to standardowy protokół komunikacji w
pojazdach, gdzie znaczenie bajtów danej ramki (sygnały: obroty silnika, prędkość,
stan świateł itd.) definiuje plik DBC — często niedostępny publicznie (własność
producenta), co czyni automatyczne dekodowanie nieznanych ramek CAN praktycznym
problemem w diagnostyce, badaniach bezpieczeństwa pojazdów i systemach ADAS/telematyki.
Automatyzacja tego procesu przez LLM (zamiast ręcznej pracy inżyniera) ma realną
wartość praktyczną — stąd całe Grupa 4 eksperymentów w tym projekcie.

---

## 3. Kluczowe pojęcia (słowniczek)

| Pojęcie | Wyjaśnienie |
|---|---|
| **Ground truth** | Znane z góry, prawdziwe znaczenie sygnału (bo sami generujemy ruch CAN) — pozwala obiektywnie ocenić, czy LLM/model trafił poprawnie. |
| **Zero-shot** | Zapytanie do modelu bez żadnych przykładów w promptcie — model odpowiada wyłącznie na podstawie wcześniejszej wiedzy ogólnej. |
| **Few-shot** | Zapytanie z kilkoma przykładami (parami wejście→poprawna odpowiedź) w promptcie, mające pokazać wzorzec do naśladowania. |
| **Fine-tuning (SFT)** | Douczenie modelu na zbiorze przykładów (par prompt→odpowiedź), modyfikujące jego wagi — w odróżnieniu od prompt engineeringu. |
| **Bajt "flag bitowych"** | Bajt, w którym każdy bit (lub grupa bitów) niezależnie koduje osobny stan logiczny (np. bit0=światła, bit1=drzwi) — wymaga dekompozycji, nie jednej skali. |
| **Bajt "skalarny"** | Bajt (lub kilka bajtów) kodujący JEDNĄ wartość liczbową (np. temperaturę, prędkość) przez skalę i offset. |
| **Hybrydowy override (Kierunek B)** | Nasz deterministyczny klasyfikator, który wykrywa błędną klasyfikację LLM (skalar zamiast flag) na podstawie obserwowanych wzorców wartości i programowo ją koryguje — bez udziału LLM w samej decyzji. |
| **Destylacja symboliczno-neuronowa** | Wariant destylacji wiedzy, gdzie "nauczycielem" jest algorytm klasyczny/symboliczny (reguły, kod), nie inna sieć neuronowa. |
| **DBC** | Format pliku opisującego znaczenie sygnałów w ramkach CAN dla danego pojazdu — zwykle własność producenta, rzadko publiczny. |

---

## 4. Wartość naukowa

1. **Nowy wkład empiryczny do pytania "czy klasyczny algorytm może uczyć LLM
   precyzyjnych, wąskich reguł strukturalnych"** — literatura o destylacji wiedzy
   skupia się głównie na destylacji sieć→sieć; destylacja symboliczny-algorytm→LLM
   dla konkretnego, dobrze zdefiniowanego wzorca strukturalnego jest rzadziej
   badanym wariantem.
2. **Test granicy między generalizacją a zapamiętywaniem** w fine-tuningu na małych,
   syntetycznych, w pełni kontrolowanych zbiorach — metodologicznie czysty eksperyment
   dzięki znanemu ground truth (nie musimy zgadywać, czy model "naprawdę rozumie",
   możemy to zmierzyć wprost).
3. **Kontrastowy wynik naukowy niezależnie od wyniku 4.3**: mamy już (z Eksperymentu 4.1)
   udokumentowane, że 4 NIEZALEŻNE interwencje promptowe zawiodły identycznie — to
   samo w sobie jest publikowalnym wynikiem negatywnym (rzadkość w literaturze, a
   przez to wartościowe), pokazującym twardą granicę promptowania dla tego typu
   zadania. Eksperyment 4.3 albo potwierdzi, że fine-tuning tę granicę przełamuje
   (pozytywny kontrast), albo pokaże, że problem jest głębszy niż sam brak dotrenowania
   (jeszcze mocniejszy wynik negatywny).
4. **W pełni reprodukowalna metodologia** — syntetyczny generator + znany ground
   truth + jednolity harness ewaluacyjny (ten sam co przez cały Eksperyment 4.1) —
   rzadkość przy pracach dotyczących LLM, gdzie częstym problemem jest brak
   dostępu do ground truth przy realnych, nieznanych danych.

---

## 5. Wartość dydaktyczna

1. Praktyczna, kompletna ilustracja pełnego pipeline'u fine-tuningu (dane → etykiety
   → format treningowy → trening → ewaluacja) — rzadko dostępna w pracy inżynierskiej
   tego zakresu w całości, zwykle tylko fragmentarycznie.
2. Pokazuje, że klasyczne (deterministyczne, "staroszkolne") podejścia inżynierskie
   i nowoczesne ML nie są konkurencyjne, tylko komplementarne — klasyfikator jako
   nauczyciel, nie zamiennik modelu.
3. Ilustruje różnicę między prompt engineeringiem a fine-tuningiem na konkretnym,
   policzalnym przykładzie (nie abstrakcyjnie) — student/czytelnik widzi DOKŁADNIE,
   kiedy jedno podejście zawodzi, a drugie (potencjalnie) działa.
4. Uczy krytycznej oceny metodologii ML: jak rozpoznać przeuczenie/zapamiętywanie
   zamiast generalizacji (sekcja 3 propozycji technicznej — przeszkoda różnorodności
   danych) — częsty, niedoceniany błąd początkujących w ML.

---

## 6. Jaki artykuł naukowy można napisać na tej podstawie

**Proponowany tytuł roboczy**: *"Wykrywanie upakowanych flag bitowych w ramkach CAN
przez modele LLM: analiza ograniczeń promptowania i destylacja symboliczno-neuronowa
jako rozwiązanie"* (ang. *"Detecting Packed Bit-Flags in CAN Frames with LLMs: Prompting
Limitations and Symbolic-to-Neural Distillation as a Remedy"*).

**Struktura i kluczowe wkłady**:
1. **Wprowadzenie problemu** — automatyczne dekodowanie nieznanych ramek CAN przez
   LLM jako alternatywa dla ręcznego reverse engineeringu (kontekst: brak publicznych
   DBC, zastosowania w diagnostyce/bezpieczeństwie pojazdów).
2. **Ustalenie granicy promptowania** (wynik już mamy) — systematyczne porównanie
   4 strategii promptu na 4 modelach LLM, wszystkie dające 0-36% skuteczności na
   sygnałach bit-flagowych, niezależnie od strategii — mocny wynik negatywny.
3. **Hybrydowy override jako punkt odniesienia** (już mamy) — pokazuje, że problem
   jest ROZWIĄZYWALNY klasycznie (97% skuteczności), więc "trudność" nie leży w
   samym zadaniu, tylko w podejściu LLM do niego.
4. **Destylacja symboliczno-neuronowa** (Eksperyment 4.3, do wykonania) — czy
   fine-tuning na korpusie etykietowanym przez klasyfikator pozwala modelowi
   zinternalizować regułę bez zewnętrznej łatki w czasie inferencji.
5. **Dyskusja generalizacji** — analiza, czy douczony model radzi sobie z
   konfiguracjami bit-packingu NIEWIDZIANYMI podczas treningu (kluczowy test
   naukowej wartości wyniku, nie tylko inżynierskiej).

**Potencjalne miejsca publikacji**: warsztaty/konferencje na styku ML i systemów
wbudowanych/motoryzacyjnych (np. workshopy przy konferencjach o cyberbezpieczeństwie
pojazdów, ML for Systems, lub jako rozdział/case study w pracy dyplomowej z
odniesieniem do szerszej literatury o ograniczeniach LLM w zadaniach numerycznych).

**Ważne zastrzeżenie**: punkty 1-3 są JUŻ wykonane i udokumentowane — artykuł mógłby
powstać nawet bez realizacji Eksperymentu 4.3 (jako czysto negatywny wynik + hybrydowe
rozwiązanie klasyczne), 4.3 dodaje trzeci, pozytywny (lub kontrastowo negatywny) kątem
patrzenia.

---

## 7. Pytania do przedyskutowania z wykładowcą

1. Czy uznaje Pan/Pani wyniki JUŻ POSIADANE (4 negatywne próby promptowe + pozytywny
   hybrydowy override) za wystarczające do samodzielnego rozdziału/artykułu, czy
   Eksperyment 4.3 jest uznawany za NIEZBĘDNY do domknięcia tezy pracy?
2. Czy zakres publikacji to praca dyplomowa (wewnętrzna), czy jest ambicja/możliwość
   faktycznej publikacji zewnętrznej (workshop/konferencja) — to wpływa na priorytet
   rygoru (np. test generalizacji na niewidzianych konfiguracjach, sekcja 6 punkt 5)?
3. Czy korekta względem pierwotnej sugestii (patrz `Eksperyment_4.3_Propozycja_...md`,
   sekcja 1) — że nauczycielem jest klasyczny klasyfikator, nie LLM — jest zgodna
   z intencją, czy zależało Panu/Pani konkretnie na tym, żeby to LLM SAM odkrywał
   wzorce (inne, trudniejsze zadanie badawcze)?
4. Biorąc pod uwagę bilans kosztów (`Eksperyment_4.3_Bilans_Koszty_Korzysci_...pdf`:
   ~120 zł, ~91h sprzętu sekwencyjnie lub ~17,5h przy zrównolegleniu) — czy realizacja
   mieści się w ramach czasowych pracy, i w jakim zakresie (pilotaż na 1 modelu vs
   pełny plan na 4)?
5. Czy warto rozszerzyć dyskusję literaturową (sekcja 2) o konkretne cytowania przed
   ewentualną publikacją, czy to wystarczający szkic teoretyczny na obecnym etapie?
