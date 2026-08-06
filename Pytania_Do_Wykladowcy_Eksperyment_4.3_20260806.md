# Pytania, kwestie i sugestie do przedyskutowania z wykładowcą

Data: 2026-08-06
Kontekst: Eksperyment 4.3 (Bootstrapped Fine-Tuning — klasyfikator jako automatyczny
nauczyciel LLM). Konsoliduje i AKTUALIZUJE pytania z dwóch wcześniejszych dokumentów
(`Eksperyment_4.3_Propozycja_Bootstrapped_FineTuning_20260728.md` i
`Eksperyment_4.3_Uzasadnienie_i_Pytania_Do_Wykladowcy_20260728.md`) w świetle
faktycznie wykonanej dziś pracy: **Etap A** (rozbudowa generatora) był już gotowy;
**Etap B** (auto-etykietowanie) i **Etap C** (format treningowy) zostały dziś
faktycznie uruchomione i dały konkretne, empiryczne wyniki — poniższe pytania
odzwierciedlają to, czego się z nich dowiedzieliśmy. **Etap D (fine-tuning) pozostaje
świadomie wstrzymany** do czasu tej dyskusji.

---

## 1. Nowość: realna jakość klasyfikatora-nauczyciela na szerszym korpusie

Oryginalna propozycja (28.07) opierała się na wyniku klasyfikatora Kierunku B
zmierzonym na WĄSKIM mini-DBC Eksperymentu 4.1 (3 CAN ID, 1 przypadek flag) — tam
osiągał ~97-100% skuteczności. Dziś uruchomiliśmy dokładnie ten sam klasyfikator
(port 1:1 z C++) na dużo szerszym, zróżnicowanym korpusie z Etapu A (40 CAN ID,
248 pozycji bajtowych, 5 wzorców sygnałów):

| Metryka | Wynik na wąskim korpusie (4.1) | Wynik na szerokim korpusie (dziś) |
|---|---|---|
| Skuteczność ogólna | ~97-100% | Precision 82.6%, Recall 55.9%, F1 66.7% |
| Trafność maski (które bity) | nie mierzone osobno | **15.8%**, gdy w ogóle wykryje flagi |

Zdiagnozowaliśmy dwie przyczyny spadku: (a) niektóre flagi nie zdążyły przełączyć
się w obu stanach w dostępnym oknie próbek (zbyt krótkie okno vs wolne interwały
przełączania), (b) bajty MIESZANE (flaga + częściowy skalar w tym samym bajcie)
mylą klasyfikator, który nie rozróżnia tych dwóch źródeł zmienności bitu.

**Pytanie:** czy przed jakimkolwiek fine-tuningiem (Etap D) należy najpierw
**poprawić sam klasyfikator** (dłuższe okno obserwacji, jawna obsługa bajtów
mieszanych), czy zaakceptować, że ~44% przykładów z flagami w zbiorze treningowym
będzie błędnie etykietowanych, i sprawdzić empirycznie, czy fine-tuning mimo to
działa (może model uśredni/zignoruje szum, jeśli reszta sygnału jest wystarczająco
silna)?

---

## 2. Czy "nauczycielem" ma być klasyczny klasyfikator, czy sam LLM?

*(pytanie z propozycji 28.07, wciąż aktualne, teraz z twardszymi danymi w tle)*

Nasza korekta pierwotnej sugestii wykładowcy — użycie klasycznego, deterministycznego
kodu zamiast LLM jako "wynajdywacza wzorców" — miała sens, gdy klasyfikator wydawał
się niemal bezbłędny. Przy realnej skuteczności 56-83% w zależności od metryki,
warto zapytać wprost: **czy to nadal akceptowalne podejście, czy zależało Panu/Pani
konkretnie na tym, żeby LLM SAM odkrywał wzorce** (obserwując wiele przykładów, nie
otrzymując gotowej etykiety)?

---

## 3. Zakres i jakość korpusu treningowego (Etap A) — czy wystarczający?

Etap A dał 40 konfiguracji / 248 pozycji bajtowych — więcej niż oryginalne 3 CAN ID,
ale wciąż ograniczone (tylko 19 prawdziwych pozytywnych przypadków flag bitowych
w całym korpusie, z czego tylko 3 z poprawnie odgadniętą maską). **Czy to
wystarczająca różnorodność/liczność przed inwestycją w faktyczny fine-tuning, czy
należy rozbudować generator (więcej konfiguracji, dłuższe okna próbkowania per
sygnał) zanim przejdziemy dalej?**

---

## 4. Wybór modelu do faktycznego fine-tuningu (Etap D)

*(pytanie z propozycji 28.07, wciąż otwarte)*

Zaczynamy od GPT-4.1/o4-mini (łatwiejszy dostęp przez API, ale koszt i czarna
skrzynka wagowa) czy DeepSeek (open-weight, pełna kontrola, ale wymaga własnej
infrastruktury treningowej, której obecnie nie mamy)? Warto też rozważyć wariant
pośredni: sprawdzić najpierw, czy WIELE (setki) przykładów w kontekście
(long-context few-shot, bez zmiany wag) daje podobny efekt — tańszy test przed
inwestycją w faktyczny fine-tuning.

---

## 5. Ambicja pracy: rozdział wewnętrzny czy materiał do publikacji zewnętrznej?

*(pytanie z uzasadnienia 28.07, wciąż aktualne)*

Czy wyniki JUŻ POSIADANE (4 negatywne próby promptowe z Eksperymentu 4.1 + pozytywny
hybrydowy override, ~97%) są wystarczające jako samodzielny rozdział/wynik, czy
Eksperyment 4.3 (w tym teraz już wykonane Etapy A-C i ewentualny Etap D) jest
uznawany za NIEZBĘDNY do domknięcia tezy pracy? Odpowiedź wpływa na to, czy warto
inwestować dalszy czas/koszt w Etap D, czy zatrzymać się na obecnym, już wartościowym
wyniku (w tym nowym wyniku negatywnym z Etapu B: "klasyfikator-nauczyciel jest
znacznie mniej niezawodny niż zakładano na szerszych danych").

---

## 6. Budżet czasowy i koszt Etapu D

*(pytanie z uzasadnienia 28.07, do zaktualizowania po ew. poprawie klasyfikatora)*

Wcześniejszy bilans kosztów (`Eksperyment_4.3_Bilans_Koszty_Korzysci_Infografika_20260728.pdf`)
zakładał czystszy korpus treningowy niż ten, który faktycznie mamy dziś. Czy przy
uwzględnieniu potrzeby ewentualnej poprawy klasyfikatora (pkt 1) budżet czasowy pracy
wciąż pozwala na realizację Etapu D w pełnym zakresie (4 modele), czy lepiej
ograniczyć się do pilotażu na jednym modelu?

---

## 7. Sugestie własne (do zaakceptowania/odrzucenia przez wykładowcę)

1. **Rekomendujemy nie ruszać Etapu D, dopóki nie poprawimy klasyfikatora**
   (pkt 1) — inwestowanie czasu/kosztu fine-tuningu w zbiór z ~44% błędnie
   oznaczonych przykładów flag bitowych ryzykuje niejednoznaczny wynik: jeśli
   fine-tuning się nie uda, nie będzie wiadomo, czy to wina samej metody, czy
   jakości danych treningowych.
2. **Rekomendujemy pilotaż na DeepSeek jako pierwszy krok**, jeśli Etap D zostanie
   zaakceptowany — jedyny open-weight model wśród testowanych, więc jedyny z
   realną możliwością iteracji bez powtarzalnego kosztu API za każdą próbę.
3. **Rekomendujemy krótki, tani test few-shot z setkami przykładów w kontekście**
   (bez zmiany wag) jako tani sondaż PRZED inwestycją w pełny fine-tuning — jeśli
   nawet to nie pomoże, silny sygnał, że problem nie jest kwestią ilości
   przykładów, tylko głębszej reprezentacji danych liczbowych przez model.
