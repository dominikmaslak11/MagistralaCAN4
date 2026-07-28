# Pytania, kwestie i sugestie do przedyskutowania z wykładowcą

Data: 2026-07-28
Kontekst: Eksperyment 4.1 (Decoding Accuracy vs Ground Truth) — runda testów naprawy
zanieczyszczenia kontekstu (`recentFrames`) na 4 modelach LLM. Pełne dane w
`Eksperyment_4.1_Naprawa_Kontekstu_Raport_20260728.md`, propozycja dalszych badań w
`Eksperyment_4.2_Propozycja_Dalszej_Optymalizacji_LLM_20260728.md`.

---

## 1. Wyniki wymagające komentarza / interpretacji

1. **Dlaczego naprawa realnego błędu (zanieczyszczenie kontekstu) pogorszyła wyniki
   GPT tak drastycznie** (67,9%→43,8% ogółem, 35,8%→0% na flagach bitowych)? Mamy
   roboczą hipotezę (czysty, ale mało zróżnicowany kontekst słabiej "podpowiada"
   złożoność niż przypadkowo zanieczyszczony) — czy wykładowca widzi w niej lukę,
   czy uważa ją za wystarczające wyjaśnienie, czy chciałby dodatkowej weryfikacji
   (np. bezpośredni pomiar entropii per-bajt w faktycznie widzianych przez model
   oknach, zamiast wnioskowania pośredniego)?
2. Czy taki wynik (poprawa architektoniczna kodu pogarszająca wynik modelu) jest
   sam w sobie wartościowym wnioskiem naukowym do opisania w pracy — jako przykład,
   że "poprawność kodu" i "skuteczność modelu" to niezależne osie, które mogą się
   rozjeżdżać?
3. Czy 4 kolejne negatywne/neutralne wyniki (zero-shot, few-shot, entropy-analysis,
   context-fix) wobec tego samego problemu (dekompozycja flag bitowych) są
   wystarczającą podstawą, by uznać ten kierunek (interwencje na poziomie
   promptu/kontekstu) za wyczerpany i przejść do innej kategorii rozwiązań
   (patrz punkt 4 niżej)?

---

## 2. Kwestia strategiczna: cel dalszej pracy — naukowy czy inżynierski?

To pytanie determinuje, który z zaproponowanych kierunków (Eksperyment 4.2, punkt A/B)
ma sens jako następny krok:

- **Cel naukowy** ("ile potrafi sam LLM, bez pomocy"): dalsze eksperymenty powinny
  trzymać się czystych interwencji na poziomie promptu/danych wejściowych — np.
  Eksperyment 4.2 (statystyki per-bajt wstrzykiwane do promptu), bez pozwalania
  klasycznemu kodowi nadpisywać decyzji modelu.
- **Cel inżynierski** ("jak najlepiej zdekodować ramkę w praktyce"): najlepszym
  rozwiązaniem jest **hybrydowy override** (deterministyczny test entropii/
  niesekwencyjności bajtu automatycznie nadpisuje błędną klasyfikację LLM) —
  gwarantowana poprawa, niezależna od ograniczeń modelu, możliwa do wdrożenia
  od razu, bez dalszych eksperymentów.

**Pytanie:** czy praca ma dalej testować granice samego LLM (wtedy: Eksperyment 4.2,
bez override'u), czy ma dążyć do najlepszego praktycznego pipeline'u dekodowania
(wtedy: wdrożyć override teraz, niezależnie od dalszych testów promptowych)?
Można też robić oba równolegle — ale wtedy warto jasno rozdzielić w pracy, które
wyniki są "czystym LLM", a które "LLM + wspomaganie klasyczne".

---

## 3. Zakres i priorytety dalszych eksperymentów

1. Czy Eksperyment 4.2 (statystyki per-bajt w promptcie) powinien najpierw zostać
   przetestowany tylko na Claude (najgorszy wynik, twardy sufit 0% — najbardziej
   czytelny test hipotezy), zanim zainwestujemy czas/koszt API w pełny przebieg
   4 modele × N=100?
2. Czy warto rozszerzyć syntetyczny zestaw sygnałów testowych (obecnie: 5 flag
   bitowych upakowanych w JEDNYM bajcie na jednym CAN ID) o dodatkowe warianty —
   np. flagi rozproszone w różnych bajtach, bajt łączący flagę + wartość skalarną —
   żeby sprawdzić, czy problem dekompozycji jest specyficzny dla TEGO układu, czy
   uogólnia się na inne konfiguracje bit-packingu?
3. Czy dwuetapowe wywołanie (Kierunek C — wymuszona klasyfikacja bajtów jako
   osobna odpowiedź przed finalną listą sygnałów) warto przetestować równolegle
   z Eksperymentem 4.2, czy dopiero jeśli 4.2 zawiedzie (żeby nie mnożyć kosztu
   API bez potrzeby)?
4. Czy warto rozważyć prawdziwy fine-tuning (Kierunek E) już teraz, czy dopiero
   po wyczerpaniu tańszych opcji promptowych/architektonicznych (A-D)? DeepSeek
   pozostaje jedynym kandydatem do faktycznego self-hosted fine-tuningu
   (open-weight) spośród testowanych 4 modeli.

---

## 4. Sugestie własne (do zaakceptowania/odrzucenia przez wykładowcę)

1. **Rekomenduję wdrożyć hybrydowy override (Kierunek B) jako trwałą poprawę
   produkcyjną już teraz** — niezależnie od dalszych eksperymentów badawczych,
   bo to czysty zysk bez ryzyka regresji (matematyka nie zależy od nastroju
   modelu). To nie koliduje z dalszymi testami czysto-promptowymi — te mogą
   działać równolegle, jako osobna, jawnie oznaczona ścieżka badawcza.
2. **Rekomenduję Eksperyment 4.2 jako następny krok badawczy**, z zawężeniem do
   Claude'a w pierwszym podejściu (najtańszy, najbardziej czytelny test) przed
   ewentualnym pełnym przebiegiem na 4 modelach.
3. Rekomenduję NIE powtarzać few-shot ani entropy-analysis w innych wariantach
   (więcej przykładów, inne sformułowania) — te dwie ścieżki dały już spójny,
   negatywny wynik i dalsze próby w tym samym duchu prawdopodobnie nie zmienią
   wniosku.
