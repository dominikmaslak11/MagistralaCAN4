# Eksperyment 4.1 — Naprawa zanieczyszczenia kontekstu `recentFrames` i jej wpływ na 4 modele LLM

Data: 2026-07-28
Zakres: Claude Sonnet 5, GPT-5.6-sol, DeepSeek-v4-pro, Gemini-3.6-flash — N=100 prób każdy,
wariant zero-shot, identyczny sprzęt (ESP32 + MCP2515, PEAK PCAN-USB, syntetyczny generator
ruchu 10 sygnałów / 3 CAN ID) przed i po poprawce.

---

## 1. Cel i kontekst

Po zakończeniu Eksperymentu 4.1 (porównanie 4 modeli LLM w zadaniu dekodowania nieznanych
ramek CAN — patrz `Eksperyment_4.1_Model_Comparison_Raport_20260727.md`) oraz po dwóch
nieudanych próbach poprawy promptu (few-shot, entropy-analysis — obie bez wpływu na
Claude'a), przeanalizowaliśmy **już zebrane dane** pod kątem hipotezy o "oknie obserwacji":
czy sposób, w jaki budujemy kontekst `recentFrames` przekazywany modelowi, mógł systematycznie
zaniżać jego szansę na poprawną dekompozycję sygnałów.

Analiza (symulacja Python na rzeczywistej sekwencji prób z istniejącego raportu GPT_v2)
potwierdziła realny błąd architektoniczny — opisany niżej. Ten dokument opisuje: czym
dokładnie był ten błąd, dlaczego postawiliśmy hipotezę, że wpływa na wyniki, jak go
naprawiliśmy, oraz co pokazał test tej poprawki na wszystkich 4 modelach.

---

## 2. Czym jest "zanieczyszczenie kontekstu" (context pollution)

`DecodingAccuracyRunner` bada 3 CAN ID w kolejce round-robin (0x100 → 0x150 → 0x200 → 0x100 → ...).
Dla każdego ID, przy wykryciu Cold Start, do zapytania LLM dołączane jest pole `recentFrames` —
prompt systemowy WPROST obiecuje modelowi: *"Analyze the trigger frame and the recent frames
for this ID (which show how values change over time)"*.

**Błąd:** przed poprawką wszystkie 3 CAN ID współdzieliły JEDEN bufor FIFO
(`std::deque<CanFrame> m_frameHistory`, limit 30 ramek). Ponieważ ramki wyzwalające
(trigger frames) ze wszystkich 3 ID trafiały do tego samego bufora w kolejności
round-robin, po wypełnieniu okna (od ok. 30. próby) tylko **10 z 30 (33%)** ramek
w `recentFrames` faktycznie dotyczyło badanego aktualnie ID — pozostałe 20 (67%) to
ramki z dwóch INNYCH, kompletnie odmiennych sygnałów, mimo że prompt jawnie twierdzi
"recent frames FOR THIS ID".

Zweryfikowaliśmy to bez uruchamiania nowego eksperymentu — odtwarzając w Pythonie
dokładną, deterministyczną sekwencję CAN ID z `trialLog` istniejącego raportu
(`Eksperyment_4.1_DecodingAccuracy_GPT_v2_20260727_131847`) i symulując logikę
starego wspólnego bufora FIFO.

### Hipoteza
5 sygnałów dyskretnych (flagi bitowe na CAN ID 0x200) było jedyną kategorią sygnałów
ze skrajnie niską skutecznością detekcji u wszystkich modeli (Claude 0%, DeepSeek 6,7%,
Gemini 3,0%, GPT 35,8% w zero-shot). Hipoteza: zanieczyszczony kontekst mógł to
pogarszać — model, analizując rzekome "recent frames for this ID", w rzeczywistości
widział głównie szum z innych sygnałów, co utrudniało rozpoznanie wzorca "mały,
niesekwencyjny zbiór wartości = niezależne flagi bitowe".

---

## 3. Poprawka

Zastąpiono wspólny bufor osobnym buforem FIFO per CAN ID:

```cpp
// PRZED:
std::deque<CanFrame> m_frameHistory;

// PO:
QHash<uint32_t, std::deque<CanFrame>> m_frameHistoryByCanId;
```

Każde CAN ID ma teraz własne, czyste okno do 30 ramek-wyzwalaczy — `recentFrames`
przekazywane modelowi faktycznie zawiera WYŁĄCZNIE ramki badanego ID, zgodnie z tym,
co obiecuje prompt systemowy. Kod skompilowano bez błędów, zweryfikowano brak regresji
funkcjonalnej (Eksperyment 1.1 — latency-only — pozostał niezmieniony, korzysta
z osobnej klasy `ExperimentRunner`).

---

## 4. Metodologia testu

Dla każdego z 4 modeli uruchomiono N=100 prób, wariant **zero-shot** (bez few-shot/
entropy-analysis — celowo, żeby izolować WYŁĄCZNIE efekt poprawki kontekstu, bez
konfundowania z inną zmienną promptu), na naprawionym binarce, i porównano z
istniejącym baseline'em zero-shot (wyniki sprzed poprawki, ten sam generator ruchu,
ten sam sprzęt, ten sam N).

| model | raport bazowy (przed poprawką) | raport po poprawce |
|---|---|---|
| Claude Sonnet 5 | `..._Claude_v2_20260727_125414` | `..._Claude_ContextFix_20260727_191700` |
| GPT-5.6-sol | `..._GPT_v2_20260727_131847` | `..._GPT_ContextFix_20260728_080100` |
| DeepSeek-v4-pro | `..._DeepSeek_v2_20260727_143005` | `..._DeepSeek_ContextFix_20260728_084100` |
| Gemini-3.6-flash | `..._Gemini_v2_20260727_160932` | `..._Gemini_ContextFix_20260728_103800` |

---

## 5. Wyniki

### 5.1 Claude Sonnet 5

| sygnał | przed poprawką | po poprawce | delta |
|---|---|---|---|
| RPM | 100,0% | 97,1% | −2,9pp |
| CoolantTemp | 100,0% | 94,1% | −5,9pp |
| Throttle | 79,4% | 82,4% | +2,9pp |
| SteeringAngle | 100,0% | 100,0% | 0,0pp |
| VehicleSpeed | 100,0% | 100,0% | 0,0pp |
| 5× flagi bitowe (śr.) | 0,0% | 0,0% | 0,0pp |
| **średnia (10 sygnałów)** | **47,9%** | **47,4%** | **−0,6pp** |

### 5.2 GPT-5.6-sol

| sygnał | przed poprawką | po poprawce | delta |
|---|---|---|---|
| RPM | 100,0% | 94,1% | −5,9pp |
| CoolantTemp | 100,0% | 94,1% | −5,9pp |
| Throttle | 100,0% | 50,0% | **−50,0pp** |
| SteeringAngle | 100,0% | 100,0% | 0,0pp |
| VehicleSpeed | 100,0% | 100,0% | 0,0pp |
| 5× flagi bitowe (śr.) | 35,8% | **0,0%** | **−35,8pp** |
| **średnia (10 sygnałów)** | **67,9%** | **43,8%** | **−24,1pp** |

Próby dekompozycji bitowej na 0x200 (≥2 zaproponowane sygnały): **12/33 → 0/33**.

### 5.3 DeepSeek-v4-pro

| sygnał | przed poprawką | po poprawce | delta |
|---|---|---|---|
| RPM | 85,3% | 73,5% | −11,8pp |
| CoolantTemp | 41,2% | 73,5% | +32,4pp |
| Throttle | 73,5% | 55,9% | −17,6pp |
| SteeringAngle | 90,9% | 84,8% | −6,1pp |
| VehicleSpeed | 90,9% | 84,8% | −6,1pp |
| 5× flagi bitowe (śr.) | 6,7% | 3,0% | −3,6pp |
| **średnia (10 sygnałów)** | **41,5%** | **38,8%** | **−2,7pp** |

Próby dekompozycji bitowej na 0x200: 2/33 → 1/33.

### 5.4 Gemini-3.6-flash

| sygnał | przed poprawką | po poprawce | delta |
|---|---|---|---|
| RPM | 100,0% | 97,1% | −2,9pp |
| CoolantTemp | 94,1% | 85,3% | −8,8pp |
| Throttle | 100,0% | 94,1% | −5,9pp |
| SteeringAngle | 100,0% | 100,0% | 0,0pp |
| VehicleSpeed | 100,0% | 100,0% | 0,0pp |
| 5× flagi bitowe (śr.) | 3,0% | 6,1% | +3,0pp |
| **średnia (10 sygnałów)** | **50,9%** | **50,7%** | **−0,2pp** |

Próby dekompozycji bitowej na 0x200: 1/33 → 2/33 (w granicach szumu przy n=33).

### 5.5 Podsumowanie zbiorcze

| model | średnia przed | średnia po | delta | dyskretne przed | dyskretne po | delta dyskretne |
|---|---|---|---|---|---|---|
| Claude Sonnet 5 | 47,9% | 47,4% | −0,6pp | 0,0% | 0,0% | 0,0pp |
| GPT-5.6-sol | 67,9% | 43,8% | **−24,1pp** | 35,8% | 0,0% | **−35,8pp** |
| DeepSeek-v4-pro | 41,5% | 38,8% | −2,7pp | 6,7% | 3,0% | −3,6pp |
| Gemini-3.6-flash | 50,9% | 50,7% | −0,2pp | 3,0% | 6,1% | +3,0pp |
| **średnia (4 modele)** | **52,1%** | **45,2%** | **−6,9pp** | **11,4%** | **2,3%** | **−9,1pp** |

Wszystkie 4 przebiegi po poprawce zakończyły się bez błędów technicznych (0-2/100
niepowodzeń parsowania, porównywalnie do baseline'u) — to nie jest artefakt
techniczny, tylko rzeczywista zmiana zachowania modeli.

---

## 6. Wnioski

1. **Poprawka nie poprawiła wyników ŻADNEGO z 4 modeli.** Hipoteza wyjściowa
   ("czystszy kontekst → lepsza detekcja flag bitowych") została **obalona**.
2. **U GPT poprawka spowodowała drastyczną regresję** (67,9%→43,8% ogółem,
   35,8%→0% na flagach bitowych, próby dekompozycji 12/33→0/33) — to
   najważniejszy, zaskakujący wynik tej rundy testów.
3. **Robocza hipoteza wyjaśniająca:** bufor per-ID zawiera WYŁĄCZNIE prawdziwe
   ramki-wyzwalacze danego ID z kolejnych rund round-robin — ale flagi bitowe
   przełączają się losowo raz na 3-20 sekund, co bywa rzadsze niż pełny cykl
   round-robin. Efekt: "czysty" bufor bywa mało zróżnicowany (te same wartości
   bajtu powtórzone), bo model rzadko trafia na moment przełączenia flagi.
   Stary, zanieczyszczony bufor przypadkiem pokazywał modelowi WIĘCEJ pozornej
   różnorodności wartości (choć błędnie przypisanej, bo z innych ID) — co mogło
   częściej "podpowiadać" hipotezę o złożonej strukturze bajtu, nawet jeśli
   podstawa tego rozumowania była nietrafna. Innymi słowy: **przypadkowy szum
   z innych sygnałów mógł działać jak nieumyślny "few-shot" sygnalizujący
   złożoność, którego zabrakło w czystych, ale ubogich w warianty danych.**
   To hipoteza robocza, nie potwierdzony mechanizm przyczynowy — wymagałaby
   dalszej weryfikacji (np. bezpośredniej inspekcji entropii per-bajt w
   faktycznie widzianych przez model oknach).
4. **Poprawka mimo to pozostaje w kodzie jako słuszna zmiana architektoniczna** —
   `recentFrames` teraz faktycznie odpowiada obietnicy z promptu ("recent frames
   FOR THIS ID"), co jest poprawnością koncepcyjną niezależną od tego, czy
   akurat pomaga w tym konkretnym zadaniu. Nie należy jej cofać — należy
   za to **zaprzestać oczekiwania**, że sama jakość/czystość kontekstu
   rozwiąże problem dekompozycji flag bitowych.
5. **To już czwarty z rzędu negatywny/neutralny wynik** dla interwencji na
   poziomie promptu/kontekstu (zero-shot baseline, few-shot, entropy-analysis,
   context-fix) w próbie poprawy dekompozycji flag bitowych. Silny sygnał, że
   problem nie leży w tym, CO mówimy modelowi ani JAK czysty jest kontekst,
   tylko w tym, że modele **zawodnie liczą entropię/niesekwencyjność bajtu
   z surowego zapisu hex** w jednym przebiegu wnioskowania — niezależnie od
   instrukcji czy jakości danych wejściowych.

---

## 7. Dalsze kierunki optymalizacji

Pełny opis w osobnym pliku: `Eksperyment_4.2_Propozycja_Dalszej_Optymalizacji_LLM_20260728.md`.
Skrót:

| # | Kierunek | Mechanizm | Priorytet |
|---|---|---|---|
| A | Statystyki per-bajt wstrzykiwane do promptu | My liczymy entropię/niesekwencyjność bajtu deterministycznie (C++), model tylko interpretuje gotowe dane | **Wysoki — rekomendowany Eksperyment 4.2** |
| B | Hybrydowy override klasyczny | Deterministyczny test nadpisuje błędną klasyfikację LLM (skalar→flagi) programowo | Wysoki, ale mniej "czysto LLM-owy" wynik |
| C | Dwuetapowe wywołanie (commit-then-decide) | Wymuszona klasyfikacja bajtów jako osobna, wymagana odpowiedź PRZED finalną listą sygnałów | Średni |
| D | Ensemble / głosowanie większościowe | Wielokrotne zapytanie, sygnał większościowy | Niski-średni, dobra łatka produkcyjna |
| E | Prawdziwy fine-tuning | SFT/DPO (GPT-4.1/o4-mini) lub self-hosted (DeepSeek, open-weight) | Niski jako pierwszy krok |
| F | Transponowany format prezentacji ramek | Tabela czasowa per bajt zamiast listy surowych ramek | Tani dodatek do A |

**Rekomendowany następny krok:** Eksperyment 4.2 — dostarczenie modelowi już
policzonych statystyk per bajt (liczba unikalnych wartości, niezależność
przełączania bitów, monotoniczność) zamiast liczenia ich samodzielnie z hexu.
Szczegóły, hipoteza, kryteria sukcesu/porażki i pytania do wykładowcy — w
pliku 4.2.
