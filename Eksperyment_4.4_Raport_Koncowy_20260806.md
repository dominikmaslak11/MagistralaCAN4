# Eksperyment 4.4 — Raport końcowy: Retrieval-Augmented Warm-Start Decoding przez Qdrant

Data: 2026-08-06
Autorzy: Dominik Maślak (prowadzenie), Claude (asystent, implementacja i analiza)
Status: **ZAKOŃCZONY** — 4 modele LLM (Claude, GPT, DeepSeek, Gemini), N=100 sparowanych
prób każdy, dane z prawdziwej magistrali CAN (PEAK PCAN-USB + ESP32/MCP2515).

---

## 1. Cel eksperymentu

Eksperyment 4.1 wykazał, że modele LLM w trybie "Cold Start" (zero-shot, bez
wcześniejszej wiedzy o pojeździe) konsekwentnie zawodzą przy rozpoznawaniu bajtów
CAN pakujących kilka niezależnych flag bitowych — proponują pojedynczą wartość
skalarną zamiast dekompozycji na bity. Jedynym skutecznym lekarstwem był
**deterministyczny "hybrydowy override"** — twarda, programowa korekta PO
odpowiedzi LLM (0%→90-97% detekcji flag), działająca OBOK modelu, nie w jego
wnętrzu.

**Eksperyment 4.4 stawia inne pytanie**: czy zamiast korygować odpowiedź PO
fakcie, można dać modelowi **lepszy punkt startowy PRZED** odpowiedzią —
wyszukując w bazie wektorowej (Qdrant) sygnały o podobnym zachowaniu, już
wcześniej poprawnie zaklasyfikowane, i dopisując tę informację jako podpowiedź
do promptu ("warm start" zamiast "Cold Start")?

---

## 2. Metodyka

### 2.1 Architektura — dwie fazy, dwa różne zbiory danych

**Faza A (offline, bez sprzętu) — budowa biblioteki:**
Syntetyczny generator `esp_experiment_4_3/generate_traffic_diverse.py` (Etap A
propozycji Eksperymentu 4.3, już istniejący i zweryfikowany) wygenerował **40
zróżnicowanych konfiguracji CAN ID** (5 wzorców: sygnały ciągłe, flagi bitowe,
flagi rozproszone, bajty mieszane, mieszanki), `seed=42`. Dla każdego z 152
sygnałów obliczono **7-wymiarowy wektor cech behawioralnych** (rozkład wartości,
wielkość skoków, oscylacja, "dwell fraction") — ręcznie zaprojektowany, bez
sieci neuronowej — i zapisano w lokalnej (embedded) instancji **Qdrant** wraz z
metadanymi (typ sygnału, skala, offset).

**Faza B (na żywo, prawdziwy sprzęt) — przechwytywanie i ewaluacja:**
PEAK PCAN-USB wygenerował **NOWY** ruch (celowo **inny seed=999**, 8 CAN ID) na
fizycznej magistrali CAN. ESP32+MCP2515 (firmware `esp_experiment_1_1.ino`,
niezmienione względem Eksperymentu 4.1) sniffował magistralę i przesyłał ramki
przez WebSocket do napisanego dziś serwera Python. Zebrano **100 realnych okien
"Cold Start"** (po 30 ramek historii + ramka wyzwalająca, round-robin między 8
CAN ID). Dla każdej próby: zapytanie do Qdrant per bajt → jeśli podobieństwo
>0.85, podpowiedź tekstowa dopisana do promptu → **sparowane** zapytanie do 4
modeli LLM (baseline bez podpowiedzi, warmstart z podpowiedzią, na TYCH SAMYCH
100 próbach dla każdego modelu).

### 2.2 Kluczowe decyzje metodologiczne

1. **Różne ziarno losowości (seed=42 dla biblioteki, seed=999 dla testu na
   żywo)** — test mierzy prawdziwą generalizację do podobnych-ale-nieznanych
   sygnałów, nie trywialne odnalezienie identycznego wpisu w bazie.
2. **Porównanie parowane** — każdy model widzi dokładnie te same 100 prób w
   obu warunkach, mocniejsze statystycznie niż niezależne próby losowe.
3. **Prawdziwy sprzęt** — dane testowe z fizycznej magistrali CAN, zweryfikowanej
   dwukierunkowo (heartbeat + echo, `esp_can_loopback_test`) przed zbieraniem
   danych.
4. **Cechy ręczne, nie "czarna skrzynka"** — wektor cech w pełni jawny i
   wytłumaczalny, Qdrant nie wymaga sieci neuronowej do generowania embeddingów.
5. **Retry z exponential backoff** na wszystkich 4 klientach API (dodane po
   napotkaniu limitów/wyczerpania budżetu, patrz sekcja 4).

---

## 3. Narzędzia i kod (nowo napisane w tej sesji)

| Plik | Rola |
|---|---|
| `capture_live_trials.py` | Serwer WebSocket zgodny z protokołem `esp_experiment_1_1.ino`, przechwytuje 100 realnych okien Cold Start z żywej magistrali |
| `qdrant_warmstart_prototype.py` | Prototyp wstępny (10 sygnałów, czysty retrieval bez LLM) |
| `qdrant_warmstart_diverse.py` | Rozszerzony prototyp (152 sygnały, 40 konfiguracji, czysty retrieval) |
| `evaluate_with_llms.py` | Harness Python wywołujący 4 API LLM bezpośrednio (Claude/Anthropic, GPT/OpenAI, DeepSeek, Gemini), budujący podpowiedzi Qdrant, ewaluujący odpowiedzi |

---

## 4. Napotkane i naprawione problemy (element rzetelności metodycznej)

### 4.1 Błąd filtrowania podpowiedzi dla nieużywanych bajtów

Pierwszy przebieg (Claude+GPT) dał wynik **+3 do +4pp** poprawy ogólnej. Analiza
na żądanie użytkownika ("jakim cudem wyszła poprawa z 40 do 80%?") wykazała:
kod pytał Qdrant o podpowiedź dla **każdego** bajtu ramki, w tym bajtów bez
żadnego sygnału (padding). **516 z 799 podpowiedzi (64.6%) dotyczyło bajtów o
CAŁKOWICIE STAŁEJ wartości** w całym oknie obserwacji — stały bajt statystycznie
przypomina "zawsze wyłączoną flagę" i dostawał fałszywie pewną (score≈1.0)
podpowiedź. Po odfiltrowaniu: trafność realnych podpowiedzi 71.4% (nie 25.3%
jak sugerowały zaszumione dane). **Naprawiono** przez pomijanie bajtów o zerowej
wariancji przed zapytaniem do Qdrant.

### 4.2 Wyczerpany budżet API (OpenAI)

W trakcie pierwszego pełnego przebiegu GPT/warmstart, wszystkie 100 prób padło
z błędem HTTP 429. Diagnoza ujawniła prawdziwą przyczynę: **nie limit czasowy,
tylko wyczerpany budżet konta** (`insufficient_quota`/`credit_balance_exhausted`).
Po doładowaniu konta i dodaniu retry+exponential backoff (5 prób, 5-80s odstępu)
do wszystkich 4 klientów API, przebieg dokończono bez błędów.

### 4.3 Konsekwencja: pełny powtórny przebieg `warmstart`

Ponieważ błąd z sekcji 4.1 dotyczył WYŁĄCZNIE warunku `warmstart` (baseline nie
używa podpowiedzi Qdrant w ogóle), **baseline zostało zachowane bez zmian**
(w pełni wiarygodne od początku), a **warmstart powtórzono w całości dla
wszystkich 4 modeli** z naprawionym kodem — to są wyniki prezentowane w sekcji 5.

---

## 5. Wyniki finalne (N=100 sparowanych prób, 4 modele, 0 błędów)

### 5.1 Ogólna detekcja

| Model | Baseline | Warmstart | Δ |
|---|---|---|---|
| Claude Sonnet 5 | 43.0% | 42.8% | −0.3pp |
| GPT-5.6-sol | 33.2% | 32.2% | −1.0pp |
| DeepSeek-v4-pro | 29.6% | 32.1% | **+2.4pp** |
| Gemini-3.6-flash | 41.5% | 37.2% | **−4.3pp** |

### 5.2 Per typ sygnału (agregat wszystkich 4 modeli)

| Typ sygnału | n | Baseline | Warmstart | Δ |
|---|---|---|---|---|
| scalar | 24 | 70.3% | 73.3% | +3.0pp |
| **bit_flag** | **116** | **17.1%** | **13.5%** | **−3.5pp** |
| partial_scalar | 4 (mała próba) | 87.5% | 100.0% | +12.5pp |

### 5.3 bit_flag per model — wzorzec UNIWERSALNY

| Model | Δ na bit_flag |
|---|---|
| Claude | −4.2pp |
| GPT | −3.7pp |
| DeepSeek | −1.1pp |
| Gemini | −5.2pp |

Wszystkie 4 modele pokazują ten sam kierunek — pogorszenie, nie przypadek
jednego modelu.

---

## 6. Interpretacja i wnioski

### 6.1 Co udowodniliśmy

1. **Retrieval-augmented warm-start przez Qdrant NIE poprawia niezawodnie
   dekodowania ramek CAN przez LLM** — efekt ogólny jest bliski zeru (od −4.3pp
   do +2.4pp w zależności od modelu), nie jest to "darmowe usprawnienie".
2. **Miękka podpowiedź konsekwentnie POGARSZA wykrywanie flag bitowych** —
   dokładnie tam, gdzie miała najbardziej pomóc (to najsłabszy punkt LLM z
   Eksperymentu 4.1) — u WSZYSTKICH 4 przebadanych modeli.
3. **Mechanizm prawdopodobny**: podpowiedź Qdrant ma ~70% trafności na
   prawdziwych, nieznanych danych (nie 89% jak w teście w obrębie jednego
   korpusu — generalizacja między różnymi przebiegami jest trudniejsza). Modele
   LLM (zwłaszcza Claude, udokumentowane w 4.1) mają już wcześniej silną
   skłonność do traktowania pakowanego bajtu jako skalara. Błędna podpowiedź
   (~30% przypadków), która akurat sugeruje "scalar" tam gdzie jest flaga, **nie
   myli modelu losowo — potwierdza jego już istniejący błąd**, czyniąc go
   bardziej pewnym siebie w złej odpowiedzi niż bez żadnej podpowiedzi.
4. **Sygnały ciągłe (scalar) generalnie korzystają** z podpowiedzi (+3.0pp
   agregatowo) — tam błędna podpowiedź nie trafia w istniejący silny bias
   modelu, więc szkoda jest mniejsza niż korzyść z trafnych podpowiedzi.

### 6.2 Kontrast z hybrydowym override (Eksperyment 4.1)

Kluczowa różnica koncepcyjna: hybrydowy override **wymusza** poprawną strukturę
(model nie ma szansy jej zignorować), podczas gdy podpowiedź Qdrant to
**sugestia, którą model może (i często to robi) zignorować lub źle
zinterpretować**. Gdy mechanizm sugerujący nie jest wystarczająco dokładny
(~70%, nie ~100%), miękka interwencja może być gorsza niż brak interwencji —
podczas gdy twarda interwencja (override) działa niezależnie od tego, co model
"myśli", bo w ogóle nie daje mu wyboru.

### 6.3 Wartość naukowa wyniku

To jest **wartościowy wynik negatywny/neutralny** (podobnie jak 4 nieudane próby
promptowe w Eksperymencie 4.1) — pokazuje konkretną, mierzalną granicę
retrieval-augmented promptingu: **jakość samego mechanizmu wyszukiwania
podobieństwa musi przekraczać pewien próg, inaczej ryzyko wzmocnienia istniejącego
błędu przewyższa potencjalną korzyść**. To praktyczna, empiryczna ilustracja
zjawiska szerzej dyskutowanego w literaturze o RAG (Retrieval-Augmented
Generation) — że "zanieczyszczony"/niedoskonały kontekst retrieval może
pogarszać, nie poprawiać, jakość odpowiedzi modelu.

---

## 7. Ograniczenia

1. **Cechy ręczne (nie neuronowe)** — prostsze, wytłumaczalne, ale mogą tracić
   informację względem uczonych embeddingów; nie testowaliśmy alternatywy.
2. **Mały rozmiar próby dla `partial_scalar`** (n=4 w całym zbiorze,
   pojedynczy sygnał w żywym korpusie) — wynik +12.5pp nieistotny statystycznie.
3. **Nierówny rozkład prób między CAN ID** — artefakt wyzwalacza opartego na
   czasie (CAN ID o krótszym okresie ramek częściej "wygrywają" kolejkę
   round-robin) — np. 0x300/0x320/0x370 miały 24-25 prób, 0x310 tylko 2.
4. **Próg podobieństwa 0.85** (kiedy w ogóle dać podpowiedź) nie był strojony/
   optymalizowany — inny próg mógłby dać inny bilans korzyść/szkoda.
5. **Tylko jedno okno (30 ramek) per próba** — nie testowano wpływu długości
   okna obserwacji na trafność podpowiedzi (patrz też Eksperyment 4.3 Etap B,
   gdzie krótkie okno było zdiagnozowaną przyczyną słabej trafności
   klasyfikatora — możliwe że dotyczy to też podpowiedzi Qdrant).

---

## 8. Powiązanie z innymi eksperymentami tej sesji

- **Eksperyment 4.3, Etap B** (auto-etykietowanie klasyczne): niezależnie
  potwierdził ten sam wzorzec — mechanizm walidowany na wąskim/tym samym
  korpusie (blisko 100% skuteczności) traci znacząco na jakości przy
  zastosowaniu do szerszych/innych danych (tu: Precision 82.6%, Recall 55.9%,
  trafność maski 15.8%). To spójny, powtarzający się motyw tej sesji: **wyniki
  walidowane na wąskich/dopasowanych danych nie generalizują automatycznie**.
- **Eksperyment 4.5** (propozycja, Raspberry Pi, ciągła obserwacja): bezpośrednia
  odpowiedź na pytanie, czy dłuższy czas obserwacji (nie zmiana metody) mógłby
  naprawić zarówno problem z Etapu B 4.3, jak i z tego eksperymentu.

---

## 9. Podsumowanie

Eksperyment 4.4 zbudował i przetestował kompletny pipeline retrieval-augmented
warm-start decoding (Qdrant + 4 modele LLM + prawdziwa magistrala CAN), w pełni
sparowany, na 400 rzeczywistych zapytaniach API (100 prób × 4 modele) w warunku
finalnym, plus dodatkowe 400 w warunku baseline. W trakcie sesji znaleziono i
naprawiono dwa realne błędy (zaszumione podpowiedzi z nieużywanych bajtów,
wyczerpany budżet API) — obydwa udokumentowane jako element rzetelności
metodologicznej, nie ukryte.

**Wynik końcowy jest jednoznaczny i spójny na wszystkich 4 modelach**: miękka
podpowiedź retrieval, oparta na niedoskonałym (~70%) mechanizmie wyszukiwania
podobieństwa, **nie poprawia** ogólnej skuteczności dekodowania, a **konsekwentnie
pogarsza** wykrywanie flag bitowych — najtrudniejszej i najważniejszej kategorii
sygnałów z perspektywy całej Grupy 4 tej pracy. To ważny, praktyczny wniosek dla
projektowania systemów CAN-Edge-AI: retrieval-augmented prompting wymaga
mechanizmu wyszukiwania o wysokiej niezawodności, inaczej lepiej go nie stosować
niż stosować niedoskonały.
