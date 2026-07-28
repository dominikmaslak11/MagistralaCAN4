# Eksperyment 4.2 (propozycja) — Dalsza optymalizacja skuteczności LLM w dekodowaniu ramek CAN

Data: 2026-07-28
Status: PROPOZYCJA do przedyskutowania z wykładowcą — nie wdrożone, nie uruchomione.
Punkt wyjścia: Eksperyment 4.1 (N=100 × 4 modele × 4 warianty: zero-shot, few-shot,
entropy-analysis, context-fix — patrz `Eksperyment_4.1_Model_Comparison_Raport_20260727.md`
i raport z testów context-fix).

---

## 1. Czego się nauczyliśmy (i dlaczego to zawęża dalsze kroki)

W Eksperymencie 4.1 przetestowaliśmy **cztery niezależne interwencje**, wszystkie mające
na celu jeden konkretny problem: żaden z 4 modeli (Claude, GPT, DeepSeek, Gemini) nie
dekoduje niezawodnie bajtu zawierającego kilka niezależnych flag bitowych (5 sygnałów
dyskretnych na CAN ID 0x200 w naszym syntetycznym mini-DBC) — model niemal zawsze
interpretuje taki bajt jako pojedynczą wartość skalarną.

| interwencja | mechanizm | wynik na flagach bitowych |
|---|---|---|
| Zero-shot (baseline) | brak dodatkowych wskazówek | Claude 0%, GPT 35,8%, DeepSeek 6,7%, Gemini 3,0% |
| Few-shot | 2 rozwiązane przykłady w promptcie, w tym 1 pokazujący dekompozycję bitową | Claude 0% (bez zmiany) — model zignorował przykład |
| Entropy-analysis | wymuszona, jawna procedura krok-po-kroku analizy każdego bajtu PRZED odpowiedzią | Claude 0% (bez zmiany) — model zignorował procedurę |
| Context-fix | naprawiono błąd zanieczyszczenia kontekstu (współdzielony bufor 3 CAN ID → osobny bufor per ID) | Claude 0% (bez zmiany), **GPT 35,8%→0% (regresja!)**, DeepSeek 6,7%→3,0% (lekka regresja) |

**Kluczowy wniosek:** wszystkie cztery interwencje działały na tym samym poziomie —
próbowały skłonić model, żeby **SAM, w jednym przebiegu, zauważył** wzorzec "mały,
niesekwencyjny zbiór wartości bajtu = niezależne flagi bitowe" patrząc na surowe bajty
hex. Żadna nie zadziałała, a jedna (context-fix) pokazała coś ważniejszego: gdy dajemy
modelowi *czystsze, ale mniej zróżnicowane* dane (bo flagi bitowe przełączają się rzadko
względem cyklu próbkowania), model **rzadziej** w ogóle rozważa hipotezę wielosygnałową —
przypadkowa różnorodność wartości (nawet błędna, z innych ID) zwiększała szansę, że model
"zauważy coś złożonego", a jej brak — zmniejsza.

To sugeruje, że problem nie leży w *motywacji* modelu (czy "chce" szukać flag bitowych —
mamy dowód, że wprost o to proszony, wciąż tego nie robi) ani wyłącznie w *jakości danych*
(sprzątanie kontekstu nie pomogło, a czasem zaszkodziło) — tylko w samej **zdolności do
policzenia i rozpoznania entropii/niesekwencyjności bajtu z surowego zapisu hex w locie**,
bez pomocy. To jest zadanie policzalne (deterministyczne), które LLM wykonuje zawodnie,
a klasyczny kod wykonuje bezbłędnie i praktycznie za darmo obliczeniowo.

---

## 2. Dalsze drogi optymalizacji — przegląd

| # | Kierunek | Mechanizm | Koszt/tradeoff | Priorytet |
|---|---|---|---|---|
| A | **Wstępne statystyki per-bajt wstrzykiwane do promptu** | Zamiast prosić LLM o policzenie entropii/zbioru wartości z surowego hexu, my (w C++) liczymy: liczbę unikalnych wartości, czy bity zmieniają się niezależnie (macierz toggle per bit), czy sekwencja jest monotoniczna — i podajemy te GOTOWE cechy w promptcie jako dane, obok surowych ramek. | Minimalna zmiana kodu (funkcja licząca statystyki + rozszerzenie promptu), model dalej odpowiada w 1 przebiegu | **Wysoki — patrz Eksperyment 4.2 poniżej** |
| B | **Hybrydowa walidacja post-hoc (override klasyczny)** | Jeśli LLM zaproponuje skalar dla bajtu, nasz kod sprawdza deterministycznie: czy dekodowane wartości tworzą mały, nie-monotoniczny zbiór typowy dla flag bitowych. Jeśli tak — programowo wymuszamy dekompozycję zamiast ufać etykiecie LLM. | LLM traci "ostatnie słowo" w klasyfikacji — używamy go tylko do nazywania/skalowania, nie do decyzji strukturalnej | Wysoki, tańszy niż A, ale mniej "czysto LLM-owy" wynik naukowy |
| C | **Dwuetapowe wywołanie (commit-then-decide)** | Zamiast jednego promptu z instrukcją "przeanalizuj krok po kroku" (co model może po cichu pominąć), wymuszamy DWA osobne zapytania: (1) model MUSI zwrócić klasyfikację każdego bajtu (scalar/bitflags/unused) jako osobną, wymaganą odpowiedź, (2) dopiero na jej podstawie generuje finalną listę sygnałów w drugim zapytaniu. | 2× koszt/czas API na próbę, ale wymusza faktyczne zaangażowanie się w krok pośredni (nie da się go pominąć jak w EntropyAnalysis) | Średni — wart testu, jeśli A zawiedzie |
| D | **Ensemble / głosowanie większościowe** | Wielokrotne zapytanie tego samego modelu o tę samą ramkę (np. temperature>0, 3-5×), branie sygnału większościowego. Dane z 4.1 pokazują, że zdolność do poprawnej dekompozycji WYSTĘPUJE sporadycznie (np. GPT 12/33 w wariancie base) — głosowanie może to ustabilizować. | 3-5× koszt API, nie naprawia leżącej u podstaw słabości, tylko ją uśrednia | Niski-średni, dobre jako "szybka łatka produkcyjna", słabe jako wynik naukowy |
| E | **Prawdziwy fine-tuning** | SFT/DPO na GPT-4.1/o4-mini (jedyne modele OpenAI z publicznym fine-tuningiem) lub self-hosted fine-tuning DeepSeek (jedyny open-weight z testowanych) | Wysoki koszt czasu/pieniędzy, wymaga zbioru treningowego (mamy już syntetyczny generator ruchu — mógłby go dostarczyć) | Niski jako pierwszy krok — dopiero gdy A-D zawiodą |
| F | **Zmiana formatu prezentacji ramek w promptcie** | Zamiast listy surowych ramek (frame-po-frame), pokazać dane jako transponowaną tabelę czasową per bajt (bajt 0: [0x03, 0x03, 0x07, 0x03, ...]) — łatwiejsze do "przeczytania" wzorca niż wyodrębnianie kolumny z listy wierszy | Bardzo tani (tylko zmiana formatowania stringa), ale słabszy mechanistycznie niż A | Warto zrobić RAZEM z A, jako część tego samego eksperymentu |

---

## 3. Rekomendowany eksperyment — Eksperyment 4.2: "Structured Byte-Statistics Prompt"

### Hipoteza
Modele LLM zawodzą w dekompozycji flag bitowych nie dlatego, że "nie chcą" lub im tego
nie powiedziano (already tested — few-shot i entropy-analysis to obalają), ale dlatego,
że **zawodnie liczą** entropię/niesekwencyjność bajtu z surowego zapisu hex w jednym
przebiegu wnioskowania. Jeśli dostarczymy te statystyki JUŻ POLICZONE (deterministycznie,
w C++) — model powinien znacząco częściej poprawnie zaproponować dekompozycję bitową,
bo zadanie zmienia się z "policz i zauważ" na "zinterpretuj już gotową obserwację".

### Co dokładnie się zmienia (implementacja)
1. Nowa funkcja w `DecodingAccuracyRunner`: `computeByteStatistics(recentFrames)` —
   dla każdej pozycji bajtu (0..7) liczy:
   - liczbę unikalnych wartości w oknie historii,
   - czy wartości tworzą monotoniczny/gładki ciąg (heurystyka: różnice kolejnych wartości
     w większości tego samego znaku i małe) czy nie,
   - macierz "czy bit N zmienia się niezależnie od innych bitów w tym bajcie"
     (dla każdego z 8 bitów: czy w historii występują oba stany 0 i 1, niezależnie
     od stanu pozostałych bitów).
2. Nowy wariant promptu `buildSystemPromptWithByteStats()`: bazowy prompt +
   sformatowana tabela "PRECOMPUTED BYTE STATISTICS" (per bajt: liczba unikalnych
   wartości, flaga "looks_like_independent_bitflags: true/false", flaga
   "looks_like_scalar: true/false") wstawiona PRZED surowymi ramkami.
3. Nowa wartość `PromptVariant::ByteStats` (spójne z istniejącym wzorcem
   ZeroShot/FewShot/EntropyAnalysis w kodzie).

### Metodologia testu
- Te same 4 modele, N=100, ta sama synteza ruchu (10 sygnałów / 3 CAN ID) —
  bezpośrednio porównywalne z istniejącymi wynikami zero-shot.
- Metryka sukcesu: detection rate na 5 sygnałach dyskretnych (aktualny sufit:
  Claude 0%, GPT 35,8%, DeepSeek 6,7%, Gemini 3,0% w zero-shot) — oczekujemy
  wzrostu, szczególnie u Claude'a (obecnie twardy sufit 0% niezależnie od promptu).
- Kontrola: sygnały ciągłe (RPM, Throttle, itd.) NIE powinny się pogorszyć —
  jeśli spadną, to sygnał że dodatkowe dane w promptcie "rozpraszają" model
  (obserwowaliśmy to już przy few-shot: RPM 100%→91,2%).

### Kryterium sukcesu / porażki
- **Sukces:** wzrost detection rate na flagach bitowych o >15pp względem zero-shot
  dla przynajmniej 2 z 4 modeli, bez spadku >10pp na sygnałach ciągłych.
- **Porażka (też wartościowy wynik):** brak poprawy pomimo dostarczenia gotowych
  statystyk — oznaczałoby to, że model nie tylko nie liczy, ale też nie UFA/nie
  WYKORZYSTUJE zewnętrznie dostarczonych obserwacji tego typu, co byłoby silnym
  argumentem za rozwiązaniem B (hybrydowy override, gdzie LLM w ogóle nie
  podejmuje tej decyzji) jako jedynym praktycznym rozwiązaniem bez fine-tuningu.

### Szacowany koszt
Identyczny jak dotychczasowe warianty (N=100 × 4 modele × ~20-110 min w zależności
od modelu) — infrastruktura (CLI flag, ESP32, generator) już istnieje i wymaga
tylko dodania nowego `PromptVariant`, bez zmian sprzętowych.

---

## 4. Do przedyskutowania z wykładowcą

1. Czy warto najpierw przetestować Eksperyment 4.2 tylko na Claude (najgorszy,
   twardy sufit 0% — najbardziej czytelny test hipotezy) przed pełnym przebiegiem
   4×N=100, żeby oszczędzić czas/koszt API jeśli hipoteza od razu zawiedzie?
2. Czy rozwiązanie B (hybrydowy override) powinno być traktowane jako
   ROZWIĄZANIE PRODUKCYJNE (do wdrożenia niezależnie od wyniku 4.2), czy tylko
   jako plan awaryjny na wypadek porażki 4.2 — to pytanie o cel pracy: czysto
   naukowy (ile potrafi sam LLM) vs inżynierski (jak najlepiej zdekodować ramkę
   w praktyce, nieważne jakim kosztem).
3. Czy zbiór syntetycznych sygnałów (5 flag w jednym bajcie, generowanych z
   losowym przełączaniem co 3-20s) jest reprezentatywny dla realnych magistrali
   CAN, czy warto rozszerzyć go o dodatkowe warianty (np. flagi w różnych
   bajtach jednocześnie, bajty z mieszanką flag i wartości skalarnej) przed
   dalszymi eksperymentami.
