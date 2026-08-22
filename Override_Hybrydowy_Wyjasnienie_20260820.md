# Hybrydowy override klasyczny — co to jest, po co powstał i co z niego wynikło

Data: 2026-08-20
Cel dokumentu: pełne wyjaśnienie mechanizmu, który jest **głównym pozytywnym
wynikiem projektu** i punktem wyjścia dla całej pracy sierpniowej.

Wszystkie opisy poniżej zweryfikowano względem faktycznego kodu
(`src/core/DecodingAccuracyRunner.cpp`), nie względem dokumentacji.

---

## 1. Problem, który miał rozwiązać

Modele językowe proszone o odgadnięcie reguły dekodowania sygnałów z ramek CAN
radzą sobie **dobrze z sygnałami ciągłymi i fatalnie z flagami bitowymi**:

| Model | Wszystkie sygnały | Flagi bitowe |
|---|---|---|
| GPT-5.6-sol | 67,9 % | 35,8 % |
| Gemini-3.6-flash | 50,9 % | 3,0 % |
| Claude Sonnet 5 | 47,9 % | **0,0 %** |
| DeepSeek-v4-pro | 41,5 % | 6,7 % |

*(N = 100 prób na model; sygnały ciągłe: 79–100 %)*

**Claude nie wykrył poprawnie ani jednej z pięciu flag w stu próbach.**

Cztery niezależne próby naprawy tego promptem — przykłady (few-shot), wymuszona
analiza entropii, naprawa zanieczyszczenia kontekstu — dały **dokładnie ten sam
wynik 0,0 %**. U GPT-5.6-sol naprawa kontekstu spowodowała wręcz regresję
o 35,8 pp.

Wniosek, który to wymusiło: problem nie leży w tym, **co** mówimy modelowi, ani
jak czysty jest kontekst. Modele zawodnie wykonują systematyczną analizę bitową
na surowym zapisie liczbowym, nawet gdy wprost o to poprosić.

---

## 2. Czym jest override — mechanizm

**Definicja w jednym zdaniu:** deterministyczna heurystyka, która ocenia bajt
niezależnie od odpowiedzi modelu i — jeśli model pomylił się w konkretny,
przewidywalny sposób — **odbiera mu tę jedną decyzję**.

### 2.1. Krok pierwszy: które bity w ogóle się przełączają

```cpp
uint8_t independentBitMask(const std::vector<CanFrame> &frames, int byteIdx) {
    uint8_t seen0 = 0, seen1 = 0;
    for (const auto &f : frames) {
        uint8_t v = f.byteAt(byteIdx);
        for (int b = 0; b < 8; ++b) {
            if (v & (1u << b)) seen1 |= (1u << b);
            else               seen0 |= (1u << b);
        }
    }
    return seen0 & seen1;   // bity widziane w OBU stanach
}
```

Maska `seen0 & seen1` to zbiór bitów, które w obserwowanej historii przyjęły
zarówno wartość 0, jak i 1. Bit stale zerowy albo stale jedynkowy odpada.

### 2.2. Krok drugi: czy to wygląda jak flagi

Dwa warunki, **oba muszą być spełnione**:

| Warunek | Próg | Uzasadnienie |
|---|---|---|
| Liczba przełączających się bitów | **2 ≤ n ≤ 6** (z 8) | Prawdziwe pola flag używają *podzbioru* bajtu — reszta bitów jest zarezerwowana lub stała. Bajt, w którym waha się 7–8 bitów, to niemal na pewno skalar wykorzystujący pełny zakres |
| Udział „dużych skoków" | **≥ 50 %** zmian ma \|różnica\| > 3 | Przełączenie bitu wyższego rzędu daje skokową zmianę wartości bajtu. Skalar albo licznik zmienia się płynnie, małymi krokami |

```cpp
bool looksLikeBitFlags(const std::vector<CanFrame> &frames, int byteIdx) {
    if (frames.size() < 2) return false;
    uint8_t mask = independentBitMask(frames, byteIdx);
    int bitCount = /* liczba bitów w masce */;
    if (bitCount < 2 || bitCount > 6) return false;      // warunek 1

    int bigJumps = 0, changedPairs = 0;
    for (size_t i = 1; i < frames.size(); ++i) {
        int a = frames[i-1].byteAt(byteIdx), b = frames[i].byteAt(byteIdx);
        if (a == b) continue;
        changedPairs++;
        if (std::abs(a - b) > 3) bigJumps++;
    }
    if (changedPairs == 0) return false;
    return (double(bigJumps) / double(changedPairs)) >= 0.5;   // warunek 2
}
```

### 2.3. Krok trzeci: podmiana reguły

Override wchodzi **tylko wtedy**, gdy model zaproponował dla tego bajtu
**pojedynczy skalar jednobajtowy bez maski** (`byteLen == 1 && bitMask == 0xFFFFFFFF`).
Wtedy ta jedna reguła zostaje zastąpiona **zestawem reguł per bit** — po jednej
na każdy bit z maski:

```
reguła LLM:        "EngineFlags", bajt 0, skalar 0–255
        ↓ override
reguła 1:          "EngineFlags_bit0_override", bajt 0, maska 0x01
reguła 2:          "EngineFlags_bit2_override", bajt 0, maska 0x04
reguła 3:          "EngineFlags_bit5_override", bajt 0, maska 0x20
```

Wszystko inne — skalary wielobajtowe, reguły już zamaskowane, bajty
niespełniające kryteriów — **przechodzi bez zmian**.

### 2.4. Dwie zasady, które czynią to uczciwym

1. **Heurystyka patrzy wyłącznie na obserwowane dane.** Nigdy na ground truth.
   W kodzie jest to opatrzone komentarzem: *„to musi być uczciwa, samodzielna
   heurystyka, nie podglądanie odpowiedzi"*.
2. **Metryki z override'em raportowane są RÓWNOLEGLE** do metryk surowego LLM,
   nie zamiast nich. Dzięki temu porównanie jest bezpośrednie i nie ma pokusy
   przedstawienia wyniku hybrydy jako wyniku modelu.

---

## 3. Dwa warianty override'a

W kodzie istnieją **dwa** warianty, liczone równolegle:

| Wariant | Źródło danych | Próg skoków | Kiedy powstał |
|---|---|---|---|
| `applyBitFlagOverride()` | **krótkie okno** ramek przekazanych przy danej próbie | 0,5 | Eksperyment 4.1 |
| `applyBitFlagOverrideLongTerm()` | **cała dotychczasowa historia** danego CAN ID | **0,3** | Eksperyment 4.5, Faza 4 |

Wariant długoterminowy używa statystyk akumulowanych **przyrostowo dla każdej
odebranej ramki** (`updateLongTermStats()`), a nie tylko dla ramek wyzwalających
Cold Start. Jest to O(1) pamięci i czasu na ramkę, więc można go bezpiecznie
wołać zawsze.

Niższy próg (0,3 zamiast 0,5) pochodzi ze strojenia opisanego w
`Eksperyment_4.5_Strojenie_Progu_Klasyfikatora_20260808.md` — przy dłuższej
obserwacji można sobie pozwolić na łagodniejsze kryterium bez utraty precyzji.

Komentarz w kodzie odnotowuje, że jest to **1:1 równoważnik**
`ByteStat.update()/verdict()` z `pi_continuous_observer.py` — czyli ta sama
logika istnieje w C++ i w Pythonie i została zweryfikowana jako zgodna.

---

## 4. Wynik

| Sygnał | Surowy LLM | LLM + override |
|---|---|---|
| RPM | 100,0 % | 100,0 % |
| Temperatura płynu | 97,1 % | 97,1 % |
| Przepustnica | 97,1 % | 97,1 % |
| Kąt skrętu | 100,0 % | 100,0 % |
| Prędkość | 97,0 % | 97,0 % |
| Lewy kierunkowskaz | 3,0 % | **97,0 %** |
| Prawy kierunkowskaz | 0,0 % | **97,0 %** |
| Światła | 3,0 % | **93,9 %** |
| Drzwi kierowcy | 0,0 % | **97,0 %** |
| Hamulec ręczny | 3,0 % | **90,9 %** |
| **Średnia (10 sygnałów)** | **50,0 %** | **96,7 %** |

Wszystkie pięć sygnałów dyskretnych: **F1 = 0 → F1 = 1,000**.
**Żaden sygnał ciągły nie uległ pogorszeniu** — override nie dotyka bajtów,
które klasyfikator uznaje za skalarne.

To był **pierwszy pozytywny wynik w serii pięciu testowanych wariantów**
(zero-shot, few-shot, entropy, naprawa kontekstu, override).

---

## 5. Dlaczego to zadziałało, skoro prompt engineering zawiódł

Kluczowa różnica, warta wypowiedzenia wprost:

> Override **nie prosi modelu, żeby zrobił coś, czego nie umie**. Odbiera mu tę
> jedną decyzję i oddaje ją narzędziu, które się do niej nadaje — a model dalej
> robi to, w czym jest dobry.

Prompt engineering próbował zmienić **zachowanie** modelu. Override zmienia
**podział pracy** między modelem a kodem klasycznym. Pierwsze podejście
uderzało w ograniczenie, które okazało się twarde; drugie je obeszło.

---

## 6. Błąd w pierwszej wersji — znaleziony i naprawiony w trakcie badań

**Wersja 1 heurystyki wymagała tylko jednego przełączającego się bitu**, bez
górnego ograniczenia. Skutek: sygnały ciągłe o szerokim zakresie wartości
(temperatura płynu, przepustnica) miały praktycznie każdy bit bajtu wahający
się w obu stanach — i były **fałszywie klasyfikowane jako flagi**.

Skala błędu: **29 z 34 prób** na CAN ID 0x100 w teście N=100.

Naprawa: dodanie warunku `2 ≤ bitCount ≤ 6`. Po poprawce: **0 % fałszywych
trafień** na wąskozakresowym, ustabilizowanym skalarze w całym zakresie testu.

To jest przykład korekty metodologii w trakcie badania — i został opisany
w artykule jako część wyniku, nie jako wstydliwy szczegół.

---

## 7. Na co to wpłynęło — mapa zależności w projekcie

Ta sama heurystyka żyje dziś w **siedmiu plikach**. Override z lipca jest
przodkiem całej pracy sierpniowej:

| Plik | Rola |
|---|---|
| `src/core/DecodingAccuracyRunner.cpp/.h` | oryginał w C++, harness Eksperymentu 4.1; oba warianty override'a |
| `esp_experiment_4_3/etap_b_autolabel.py` | port 1:1 do Pythona — **automatyczne etykietowanie danych treningowych** do fine-tuningu (Etap B) |
| `esp_experiment_4_3/evaluate_diverse_zeroshot.py` | ewaluacja na szerszym, zróżnicowanym korpusie |
| `esp_experiment_4_5_rpi/pi_continuous_observer.py` | wersja **przyrostowa** (O(1)/ramkę) do ciągłej obserwacji na urządzeniu brzegowym |
| `esp_experiment_4_5_rpi/apply_override_offline.py` | zastosowanie override'a do zebranych danych offline |
| `esp_experiment_4_7_nn/pi_observer_nn.py` | demon, w którym **sieć neuronowa działa obok** tej reguły, do porównania na żywo |

### Łańcuch konsekwencji

1. **Override zadziałał** → klasyfikator okazał się wiarygodny.
2. **Skoro potrafi wskazać flagi, może uczyć model** → pomysł na automatyczne
   etykietowanie danych treningowych (Eksperyment 4.3, Etap B).
3. **Ale na szerszym korpusie wypadł słabo** → Recall 55,9 %, trafność maski
   15,8 %. To **zablokowało Etap D** (fine-tuning) — nie ma sensu uczyć modelu
   na danych, w których ~44 % przykładów z flagami jest błędnie oznaczonych.
4. **Zdiagnozowano dwie przyczyny** → za krótkie okno obserwacji i bajty mieszane.
5. **Stąd ciągła obserwacja** (Eksperyment 4.5) i wariant długoterminowy
   override'a z progiem 0,3.
6. **Stąd replikacja na drugiej platformie** (4.6) — sprawdzenie, czy wynik jest
   własnością metody, czy płytki.
7. **Stąd sieć neuronowa** (4.7–4.12) — bo okazało się, że reguła ma **sufit
   konstrukcyjny**: warunek `bitCount ≤ 6` odrzuca bajty z 7–8 flagami
   niezależnie od jakiegokolwiek strojenia.

---

## 8. Co powiedzieć w trzech zdaniach

> „Modele językowe nie widzą flag bitowych — cztery próby naprawy przez prompt
> dały cztery razy zero. Zamiast dalej przekonywać model, dołożyliśmy
> deterministyczny klasyfikator, który patrzy na to, ile bitów bajtu się
> przełącza i czy zmiany są skokowe, i który **nadpisuje decyzję modelu tylko
> dla bajtów spełniających te kryteria**. Średnia skuteczność wzrosła z 50 % do
> 96,7 %, flagi bitowe z F1 = 0 do F1 = 1,000, a żaden sygnał ciągły się nie
> pogorszył."

---

## 9. Powiązane dokumenty

| Dokument | Zawartość |
|---|---|
| `Artykul_Naukowy_LLM_CAN_Bitowe_Flagi.md` | sekcje 3.5 (projekt), 4.5 (wynik), 5.1 (dlaczego działa), 5.3 (korekta błędu v1) |
| `Eksperyment_4.1_Hybrydowy_Override_Infografika_20260728.pdf` | wersja graficzna |
| `Eksperyment_4.5_Strojenie_Progu_Klasyfikatora_20260808.md` | skąd próg 0,3 w wariancie długoterminowym |
| `Eksperyment_4.7-4.10_Siec_Neuronowa_vs_Regula_20260815.md` | sufit konstrukcyjny reguły i sieć jako następca |
| `Eksperyment_4.11-4.12_Maska_Bitowa_20260817.md` | problem maski bitowej, którego override nie rozwiązuje |
