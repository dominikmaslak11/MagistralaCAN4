# Eksperyment 4.3 (propozycja) — Bootstrapped Fine-Tuning: klasyfikator jako automatyczny nauczyciel LLM

Data: 2026-07-28
Status: PROPOZYCJA do przedyskutowania z wykładowcą — nie wdrożone, nie uruchomione.
Geneza: odpowiedź wykładowcy na wynik Eksperymentu 4.1/hybrydowy override: „Ewentualnie
można zrobić bazę wzorców i na jej podstawie douczyć LLM. Czyli najpierw LLM wynajduje
wzorce. A potem douczamy inny lub ten sam LLM wyposażony w plik z listą wzorców/reguł.”

---

## 1. Punkt wyjścia i korekta względem pierwotnego sformułowania

Idea wykładowcy jest trafna w ogólnym kształcie — zbudować bazę wzorców/reguł i na
jej podstawie douczyć model — ale w jednym miejscu warto ją doprecyzować:

**"Wynajdywaczem wzorców" nie musi być LLM.** W Eksperymencie 4.1 (test hybrydowego
override'u, Kierunek B) pokazaliśmy, że do rozpoznania KONKRETNIE tego wzorca
(bajt pakujący niezależne flagi bitowe vs pojedynczy skalar) **wystarczy klasyczny,
deterministyczny klasyfikator** — bez żadnego uczenia, bez kosztu zapytań API,
ze 100% skutecznością na syntetycznych flagach bitowych i niskim odsetkiem
fałszywych trafień na sygnałach ciągłych (po poprawce heurystyki, patrz
`Eksperyment_4.1_Hybrydowy_Override_Infografika_20260728.pdf`).

Zamiast więc używać (drogiego, wolnego, niepewnego) LLM do "odkrywania wzorców",
proponujemy użyć **już zwalidowanego klasyfikatora jako automatycznego,
darmowego nauczyciela (etykieciarza)** — generuje on tysiące poprawnie
oznaczonych przykładów bez udziału człowieka i bez kosztu API. To jest
klasyczna **destylacja wiedzy z modelu symbolicznego do sieci neuronowej**
(symbolic-to-neural distillation) — dobrze ugruntowana technika, tylko że
"nauczycielem" jest nasz kod, nie LLM.

---

## 2. Hipoteza

Model LLM douczony (fine-tuned) na dużym, zróżnicowanym korpusie przykładów
wygenerowanych i oznaczonych przez klasyczny klasyfikator **zinternalizuje**
zasadę rozpoznawania flag bitowych — i będzie ją stosował SAM, bez potrzeby
zewnętrznego override'u przy każdym zapytaniu w czasie działania. To
przesunęłoby rozwiązanie z "LLM + stała klasyczna łatka" na "LLM, który
faktycznie się tego nauczył" — bardziej eleganckie i bardziej zgodne z
pierwotnym pytaniem wykładowcy ("czy można by któryś z modeli doszkolić").

---

## 3. Kluczowa przeszkoda: nie brak algorytmu, tylko brak różnorodności danych

Obecny syntetyczny mini-DBC (Eksperyment 4.1) ma tylko **3 CAN ID i JEDEN**
przypadek flag bitowych (0x200, 5 flag w jednym bajcie). Fine-tuning na tak
wąskim zbiorze nauczyłby model rozpoznawać **te konkretne 3 ramki**, nie
ogólną zasadę "mały, nie-monotoniczny zbiór wartości = flagi bitowe" —
klasyczny przypadek przeuczenia (overfitting) do zbioru ewaluacyjnego, który
already znamy na pamięć.

**Warunek konieczny przed fine-tuningiem**: rozbudowa generatora ruchu o
znacznie szerszy zestaw konfiguracji, np.:
- flagi bitowe w różnych pozycjach bajtu (nie tylko bajt 0),
- różna liczba flag w bajcie (2, 3, 4, 5, 6 — pamiętając, że nasz
  klasyfikator wymaga 2-6 niezależnie przełączających się bitów),
- flagi rozproszone na różnych bajtach tej samej ramki,
- bajty łączące flagę + wartość skalarną (częściowe wykorzystanie bajtu),
- skalary o różnych zakresach/szybkościach zmian (wąskie i szerokie,
  szybkie i wolne — żeby model uczył się rozróżniać, nie zapamiętywać),
- różne CAN ID, DLC, częstotliwości ramek.

Bez tego kroku fine-tuning nie ma sensu — trenowalibyśmy na obietnicy
generalizacji, której zbiór danych fizycznie nie jest w stanie dostarczyć.

---

## 4. Proponowany pipeline (4 etapy)

### Etap A — Rozbudowa generatora syntetycznego
Rozszerzyć `esp_experiment_4_1/generate_traffic.py` (lub nowy,
sparametryzowany generator) o dziesiątki/setki losowo konfigurowanych
"mini-DBC" wg wzorców z sekcji 3 — każdy z jawnie znanym ground truth
(potrzebnym do weryfikacji, że etykiety klasyfikatora są poprawne, i do
finalnej ewaluacji).

### Etap B — Automatyczne etykietowanie klasycznym klasyfikatorem
Uruchomić **klasyfikator z Kierunku B** (`looksLikeBitFlags()`,
`independentBitMask()` — już zaimplementowane w `DecodingAccuracyRunner`)
na wygenerowanym korpusie. Dla każdej próbki: historia ramek (jak
`recentFrames` w prawdziwym zapytaniu) + poprawna klasyfikacja per bajt
(skalar vs flagi, i jeśli flagi — które bity). Weryfikacja jakości etykiet
przez porównanie z ground truth generatora (nie tylko ufać klasyfikatorowi
w ciemno — jeśli klasyfikator się myli, błąd trafi do zbioru uczącego).

### Etap C — Formatowanie jako przykłady fine-tuningowe
Każda etykietowana próbka → para (prompt, completion) w DOKŁADNIE takim
samym schemacie, jakiego już używa `DecodingAccuracyRunner` (system prompt +
recentFrames + trigger frame → JSON z listą sygnałów) — żeby douczony model
dało się podstawić w istniejący pipeline BEZ ŻADNYCH zmian w kodzie
eksperymentu, tylko zmieniając nazwę modelu w konfiguracji.

### Etap D — Fine-tuning i ewaluacja
Douczyć kandydatów ograniczonych do faktycznie dostępnych opcji (ustalone w
Eksperymencie 4.1): **GPT-4.1/o4-mini** (SFT/RFT przez OpenAI API) lub
**DeepSeek** (self-hosted, open-weight, jedyny z pełną kontrolą). Ocena:
**dokładnie tym samym harnessem co Eksperyment 4.1** (N=100, zero-shot, BEZ
override'u) — bezpośrednio porównywalna z każdym dotychczasowym wynikiem.

**Kryterium sukcesu**: douczony model osiąga na sygnałach dyskretnych
detection rate zbliżony do wyniku z override'em (~94% w naszym teście),
SAMODZIELNIE, bez zewnętrznej klasycznej łatki w czasie działania.

**Kryterium porażki (też wartościowe)**: douczony model nie generalizuje
poza konfiguracje widziane podczas treningu — sygnał, że albo korpus wciąż
za wąski, albo problem jest głębiej osadzony w sposobie, w jaki model
reprezentuje surowe dane liczbowe (hex), niezależnie od dotrenowania.

---

## 5. Koszt i zakres

Znacząco większy niż dotychczasowe eksperymenty w tej serii:
- Rozbudowa generatora: nowy, nietrywialny kod (dni pracy, nie godziny).
- Fine-tuning: koszt API (OpenAI SFT) lub infrastruktura obliczeniowa
  (self-hosted DeepSeek) + czas treningu.
- Ewaluacja: N=100 jak dotychczas, ale na WIELU konfiguracjach testowych
  (nie tylko naszym oryginalnym 3-ID mini-DBC — inaczej znowu testujemy
  zapamiętywanie, nie generalizację).

To uzasadnia status osobnego eksperymentu (4.3), nie doklejki do bieżącej
pracy nad Eksperymentem 4.1/4.2.

---

## 6. Pytania do przedyskutowania z wykładowcą

1. Czy akceptowalne jest, żeby "nauczycielem" w tym schemacie był nasz
   klasyczny klasyfikator (Kierunek B), a nie sam LLM — czy wykładowcy
   zależało konkretnie na tym, żeby LLM SAM odkrywał wzorce (co byłoby
   osobnym, trudniejszym zadaniem: prosić model o wyindukowanie ogólnej
   reguły z wielu przykładów, zamiast dawać mu gotowy klasyfikator)?
2. Jaki zakres różnorodności syntetycznego korpusu (Etap A) uznać za
   wystarczający, zanim przejdziemy do faktycznego fine-tuningu — ile
   różnych konfiguracji bit-packingu, ile próbek na konfigurację?
3. Czy zaczynamy od GPT-4.1/o4-mini (łatwiejszy dostęp przez API, mniejszy
   próg wejścia) czy od DeepSeek (open-weight, pełna kontrola, ale wymaga
   własnej infrastruktury treningowej)?
4. Czy warto rozważyć wariant pośredni: zamiast pełnego fine-tuningu wag,
   sprawdzić najpierw, czy WIELE (setki) przykładów w kontekście (long-context
   few-shot, nie w wadze modelu) daje podobny efekt — tańszy test przed
   inwestycją w faktyczny fine-tuning.
