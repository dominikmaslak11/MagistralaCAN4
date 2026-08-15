# Eksperyment 4.5, Faza 4 — Hybrydowy override zasilany ciągłą obserwacją

Data: 2026-08-08
Autorzy: Dominik Maślak (prowadzenie), Claude (asystent, implementacja i analiza)
Status: **ZAKOŃCZONY** — analiza offline na już zebranych danych (Faza 1 i
Faza 3 Eksperymentu 4.5), **zero nowych, płatnych wywołań API**.

To najsilniejszy, najbardziej jednoznaczny wynik całej serii eksperymentów
4.1–4.5.

---

## 1. Skąd ten pomysł

Eksperyment 4.1 pokazał, że **twardy, deterministyczny override** (post-hoc
podmiana błędnej reguły LLM na strukturę wykrytą klasycznym klasyfikatorem)
daje 0%→90-97% detekcji flag bitowych. Eksperymenty 4.4 i 4.5 (Fazy 2/3)
pokazały, że **miękka podpowiedź** (tekst w promptcie, oparta na Qdrant)
konsekwentnie SZKODZI tej samej kategorii sygnałów, u wszystkich 4 modeli.
Faza 4 stawia pytanie wprost: co się stanie, jeśli **ten sam mechanizm
override z 4.1** zasilimy nie jednorazowym oknem, tylko **ciągłym,
godzinnym klasyfikatorem z Fazy 1 tego eksperymentu** (przestrojonym,
100% precyzji przy pełnej akumulacji danych)?

---

## 2. Metodyka

### 2.1 Ponowne wykorzystanie już zebranych danych — zero nowego kosztu

Wykorzystano bez zmian:
- **400 zapisanych odpowiedzi baseline** (Faza 3: Claude/GPT/DeepSeek/Gemini
  × 100 prób, `rawText` każdej odpowiedzi zachowany w `results_llm/*.json`).
- **`observer_state.json`** (Faza 1: pełny, surowy stan po 1h/1,6mln ramek —
  `seen0`, `seen1`, `changed_pairs`, `big_jumps` per 160 pozycji bajtowych).

Nowy kod (`apply_override_offline.py`) parsuje zapisane odpowiedzi JSON,
identyfikuje propozycje "pojedynczy skalar, pełny bajt" (`byteLen=1`,
`bitMask=null`) i **podmienia je twardo** na zestaw reguł per-bit, jeśli
klasyfikator uzna dany bajt za flagi bitowe — **1:1 port**
`DecodingAccuracyRunner::applyBitFlagOverride` (C++, Eksperyment 4.1),
nie nowy mechanizm.

### 2.2 Dwa warianty źródła klasyfikatora — to jest sedno tej fazy

| Wariant | Źródło werdyktu | Rozmiar próbki |
|---|---|---|
| **A — trial-window** | klasyfikator liczony NA NOWO z 30 ramek TEJ próby (jak w oryginalnym mechanizmie 4.1) | ~30 próbek/bajt |
| **B — Faza 1** | gotowy werdykt z ciągłej, godzinnej obserwacji tego samego CAN ID | 17k–165k próbek/bajt |

---

## 3. Wyniki

### 3.1 Wariant A (trial-window, 30 klatek) — wynik mieszany

| Model | Baseline | Override (A) | Δ | bit_flag baseline | bit_flag override (A) | Δ bf |
|---|---|---|---|---|---|---|
| Claude | 43.2% | 41.8% | −1.4pp | 25.4% | 27.0% | +1.6pp |
| GPT | 36.6% | 35.5% | −1.1pp | 29.1% | 31.5% | +2.3pp |
| DeepSeek | 31.7% | 32.0% | +0.3pp | 11.5% | 26.4% | +14.8pp |
| Gemini | 43.2% | 42.9% | −0.3pp | 28.8% | 35.8% | +7.0pp |

Kierunek na bit_flag jest już pozytywny u wszystkich 4 modeli (w
przeciwieństwie do Qdrant warmstart!), ale ogólna detekcja lekko SPADA u 3/4
modeli. Diagnoza — **precyzja samego override'u na tym oknie to tylko
47.2%** (17 trafionych / 19 fałszywych na 36 uruchomień), bo 30 próbek to za
mało, żeby heurystyka `looks_like_bit_flags` działała niezawodnie (patrz
`Eksperyment_4.5_Strojenie_Progu_Klasyfikatora`, gdzie 100% precyzji
wymagało tysięcy-setek tysięcy próbek).

### 3.2 Wariant B (Faza 1, ciągła obserwacja) — wynik jednoznaczny

| Model | Baseline | Override (B) | Δ | bit_flag baseline | bit_flag override (B) | Δ bf |
|---|---|---|---|---|---|---|
| Claude | 43.2% | **61.2%** | **+18.0pp** | 25.4% | **53.1%** | **+27.7pp** |
| GPT | 36.6% | **39.1%** | **+2.5pp** | 29.1% | 30.4% | +1.3pp |
| DeepSeek | 31.7% | **44.8%** | **+13.1pp** | 11.5% | **43.9%** | **+32.4pp** |
| Gemini | 43.2% | **60.4%** | **+17.2pp** | 28.8% | **52.2%** | **+23.4pp** |

**Precyzja override'u w tym wariancie: 100.0%** — 49 uruchomień, 49
trafionych, 0 fałszywych (zweryfikowane bezpośrednio wobec ground truth,
pełne pokrycie 20/20 CAN ID między Fazą 1 a Fazą 3).

Per typ sygnału (agregat 4 modeli), baseline vs override (B):

| Typ | n | Baseline | Override (B) | Δ |
|---|---|---|---|---|
| scalar | 508 | 75.2% | 74.6% | −0.6pp (szum) |
| partial_scalar | 52 | 73.1% | 73.1% | 0.0pp |
| **bit_flag** | **904** | **16.2%** | **37.1%** | **+20.9pp** |

Interwencja jest **chirurgicznie precyzyjna**: poprawia wyłącznie bit_flag,
nie rusza (w granicach szumu) pozostałych kategorii.

---

## 4. Interpretacja i wnioski

### 4.1 Główny wniosek

**Ciągła obserwacja nie pomaga tylko podpowiedziom retrieval (Faza 2/3) — pomaga
przede wszystkim mechanizmowi override, i to znacznie mocniej.** Ten sam
klasyfikator (`independent_bit_mask`/`looks_like_bit_flags`), zastosowany w
tym samym miejscu kodu (`applyBitFlagOverride`), daje jakościowo różny wynik
w zależności WYŁĄCZNIE od tego, ile danych miał do dyspozycji: 47.2%
precyzji przy 30 próbkach vs 100.0% przy dziesiątkach/setkach tysięcy.
Mechanizm override sam w sobie nie ma żadnej "pamięci" ani uczenia — cała
różnica pochodzi z jakości wejściowego werdyktu klasyfikatora.

### 4.2 Dlaczego to działa, skoro miękkie podpowiedzi (4.4/4.5 Faza 2-3) nie działały

To bezpośrednia demonstracja różnicy między **twardą korektą** a **miękką
sugestią**, opisanej już teoretycznie w raporcie 4.4 (sekcja 6.2), teraz
zmierzonej na tych samych, żywych danych z tego samego przebiegu:
- Podpowiedź Qdrant (nawet przy 93.5% trafności samego mechanizmu
  wyszukiwania — Faza 2) to tylko TEKST w promptcie, który model może
  zignorować albo źle zinterpretować — i konsekwentnie robił to gorzej,
  nie lepiej (Faza 3: bit_flag agregat 16.2%→14.5%, **−1.7pp**).
- Override to **podmiana struktury po fakcie** — model w ogóle nie ma
  szansy jej zignorować, bo decyzja zapada poza nim, na podstawie
  niezależnego, wysoko wiarygodnego źródła.

Innymi słowy: problem z miękkimi podpowiedziami nigdy nie leżał w jakości
biblioteki (to naprawiła już Faza 2 — 93.5% zamiast ~70%) — leżał w samym
**mechanizmie miękkiej sugestii jako takim**. Override obchodzi ten problem
całkowicie, nie próbując go rozwiązać.

### 4.3 To, co mnie najbardziej przekonuje w tym wyniku

Kontrast wariantu A vs B na TYCH SAMYCH 400 odpowiedziach LLM, zmieniając
WYŁĄCZNIE źródło danych klasyfikatora, izoluje zmienną w sposób, jakiego nie
dawał żaden wcześniejszy eksperyment tej sesji. To nie jest "innych 100 prób
dało lepszy wynik" — to dokładnie te same propozycje LLM, ocenione dwa razy,
z jedyną różnicą będącą przedmiotem badania. Trudno o czystszy dowód, że to
**czas obserwacji**, nie coś innego, jest czynnikiem sprawczym.

### 4.4 Nierówny efekt u GPT — warta odnotowania asymetria

GPT zyskuje najmniej (+2.5pp ogółem, +1.3pp na bit_flag) — wyraźnie mniej niż
pozostałe 3 modele (+13 do +18pp). Nie mam pewnego wyjaśnienia; hipoteza: GPT
w baseline rzadziej niż inne modele proponuje "czysty pojedynczy skalar"
(`byteLen=1, bitMask=null`) dla bajtów będących flagami — może już częściej
zgadywać jakąś formę maski samodzielnie, co ogranicza liczbę okazji, w
których override w ogóle ma się do czego podłączyć. Wymagałoby to osobnej
analizy rozkładu propozycji GPT, nie zrobionej tutaj.

### 4.5 Wartość merytoryczna i dodana — szczerze

To pierwszy wynik w całej sesji 4.1–4.5, który **jednocześnie**: (a) poprawia
najsłabszą, najważniejszą kategorię sygnałów (bit_flag), (b) u wszystkich 4
modeli, (c) bez mierzalnego kosztu gdziekolwiek indziej, (d) przy 100%
zweryfikowanej precyzji samej interwencji, (e) zero dodatkowego kosztu API
(w pełni policzone na już opłaconych danych). Poprzednie eksperymenty tej
sesji (4.3 Etap B, 4.4, Faza 2-3 tego eksperymentu) miały charakter
diagnostyczny/negatywny — pokazywały GDZIE i DLACZEGO coś nie działa. Faza 4
jest pierwszym wynikiem o charakterze **rozwiązania, nie tylko diagnozy**.

### 4.6 Sugestia na następny krok

Naturalną konsekwencją jest pytanie, czy ten mechanizm da się przenieść z
tego offline eksperymentu do rzeczywistego pipeline'u: `DecodingAccuracyRunner.cpp`
(C++, firmware/aplikacja) już ma `applyBitFlagOverride`, tylko liczący
klasyfikator z krótkiego okna historii ramek w pamięci, nie z trwałej,
ciągłej akumulacji. Zasilenie go statystyką w stylu `pi_continuous_observer.py`
(Raspberry Pi jako stały węzeł CAN, budujący długoterminową pamięć per-pojazd)
zamiast krótkiego bufora w RAM aplikacji, to bezpośrednia, uzasadniona tym
wynikiem ścieżka do wdrożenia produkcyjnego — nie kolejny eksperyment, tylko
inżynieria.

---

## 5. Ograniczenia

1. **Override zadziałał tylko 49 razy na 400 prób** (~12%) — większość
   propozycji LLM nie była "czystym pojedynczym skalarem" kwalifikującym się
   do podmiany. Wynik jest realny, ale dotyczy ograniczonego podzbioru
   sytuacji.
2. **Ten sam jeden seed (999)** dla Fazy 1 i Fazy 3 co cała reszta tej sesji —
   nie testowano generalizacji do innego rozkładu sygnałów.
3. **Faza 1 i Faza 3 to dwa oddzielne uruchomienia generatora** (ten sam
   seed/schemat, ale inny przebieg w czasie rzeczywistym) — statystycznie
   równoważne, nie identyczne co do pojedynczej klatki. To w praktyce
   dokładnie odpowiada realnemu scenariuszowi (pojazd obserwowany godzinę,
   potem nowe zapytanie), nie jest to więc słabością metodologiczną, ale
   warto to jawnie zaznaczyć.
4. **−0.6pp na scalar** (w granicach szumu, n=508) nie zostało w pełni
   wyjaśnione (możliwe współdzielenie byte_idx między sygnałami różnego
   rodzaju w niektórych konfiguracjach) — nie wpływa na główny wniosek, ale
   warto by to doprecyzować przy powtórzeniu na innym seedzie.

---

## 6. Powiązanie z innymi eksperymentami tej sesji

- **Eksperyment 4.1**: mechanizm `applyBitFlagOverride` (C++) użyty tu bez
  zmian — Faza 4 to nie nowy pomysł, tylko test starego mechanizmu z nowym
  (lepszym) źródłem danych.
- **Eksperyment 4.4 / Eksperyment 4.5 Faza 2-3**: bezpośredni kontrast —
  ten sam problem (bit_flag), ten sam typ interwencji (skorygowanie
  odpowiedzi LLM), przeciwny skutek w zależności od tego, czy korekta jest
  twarda (tu: +20.9pp agregatowo) czy miękka (tam: −1.7pp do −3.5pp).
- **Eksperyment 4.5, Strojenie Progu**: przestrojony próg (0.3, nie 0.5)
  użyty tutaj bez zmian — ten wynik jest częściowo zasługą tamtej wcześniejszej
  poprawki (wyższy recall klasyfikatora = więcej okazji do trafnego override).

---

## 7. Podsumowanie

Zasilenie sprawdzonego mechanizmu twardego override'u (Eksperyment 4.1) danymi
z ciągłej, godzinnej obserwacji Raspberry Pi Zero W (Eksperyment 4.5, Faza 1)
zamiast krótkiego, jednorazowego okna daje **+2.5 do +18.0pp ogólnej detekcji**
i **+1.3 do +32.4pp detekcji flag bitowych** u wszystkich 4 przebadanych modeli
LLM, przy **100% zweryfikowanej precyzji** samej interwencji i **zerowym
koszcie API** (analiza w całości na już zebranych danych). To pierwszy wynik
tej wieloczęściowej sesji badawczej (4.1–4.5), który nie tylko diagnozuje
problem, ale go **rozwiązuje** — i wskazuje konkretną, uzasadnioną ścieżkę do
wdrożenia w rzeczywistym pipeline C++ (`DecodingAccuracyRunner`).
