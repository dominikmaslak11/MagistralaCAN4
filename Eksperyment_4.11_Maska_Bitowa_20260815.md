# Eksperyment 4.11 — klasyfikacja maski bitowej: które bity są flagami

Data: 2026-08-15
Autorzy: Dominik Maślak (prowadzenie), Claude (asystent, implementacja i analiza)
Platforma: Orange Pi Zero 3 + MCP2515, magistrala wspólna z PEAK PCAN-USB
Status: **ZAKOŃCZONY**

---

## 1. Problem

Eksperymenty 4.5-4.10 poprawiały wyłącznie odpowiedź na pytanie **„który bajt
zawiera flagi"** — od 55,9 % (4.3 Etap B) przez 85 % (4.5) do 100 % Recall
(4.7-4.10, sieć). Ani jeden z nich nie tknął pytania **„które konkretnie bity
są flagami"**, czyli trafności maski bitowej, zmierzonej w Eksperymencie 4.3
Etap B na **15,8 %**.

Obecna maska to `seen0 & seen1` — „każdy bit, który kiedykolwiek się zmienił".
Ta reguła **z definicji** nie odróżnia flagi od bitu należącego do skalara,
bo oba się zmieniają. Żadne strojenie progu tego nie naprawi, bo progi działają
na poziomie **bajtu**, a problem jest na poziomie **bitu**.

---

## 2. Doprecyzowanie problemu — gdzie on właściwie jest

Pomiar odniesienia (korpus seed=999, 20 CAN ID) ujawnił, że słabość maski nie
jest równomierna:

| Typ bajtu | Liczba | Trafność maski `seen0 & seen1` |
|---|---|---|
| same flagi | 17 | **100 %** — trywialne |
| **mieszane (flagi + skalar częściowy)** | 3 | **0 %** — nigdy nie trafia |
| same skalary | 26 | — |

**Maska jest idealna dla bajtów czystych i całkowicie bezradna dla mieszanych.**
Bajty mieszane stanowią ~22 % bajtów zawierających flagi (zmierzone na 12
ziarnach × 30 konfiguracji: średnio 6,8 mieszanych na 24,1 czystych).

To wyjaśnia rozbieżność wobec 15,8 % z 4.3 Etap B — tamta wartość pochodzi
z innego korpusu i (prawdopodobnie) innego sposobu liczenia; przy naszym
korpusie odniesienie wypada na 78-82 % ogółem. **Istotna jest nie średnia,
lecz zero na przypadkach trudnych** — i tak zaprojektowano ewaluację.

---

## 3. Hipoteza i cechy

Bity skalara są **statystycznie sprzężone**, flagi **niezależne**:

- w liczniku bit 0 przełącza się najczęściej, bit 7 najrzadziej — powstaje
  monotoniczny gradient częstości,
- zmiany propagują się przez **przeniesienia**: bit *i* zmienia się głównie
  wtedy, gdy bity 0…*i*−1 są w stanie 1,
- flaga nie wykazuje korelacji z sąsiadami.

11 cech per bit, **wszystkie liczalne przyrostowo, O(8) na ramkę** (tak samo jak
dotychczasowe statystyki per-bajt): częstość przełączeń, jej ranga w bajcie,
stosunek do maksimum, wypełnienie, sygnatura przeniesień
(`P(zmiana | niższe bity same jedynki)` minus tło), współzmienność z sąsiadem
niższym i wyższym, liczba zmieniających się bitów w bajcie, monotoniczność
gradientu, pozycja bitu, kontekst bajtu.

Protokół: uczenie ziarna 1-30 (vcan), walidacja 31-40 (vcan), **test ziarna
100-105 na prawdziwym MCP2515**. Razem 57 280 bitów uczących (3937 flag).

---

## 4. Wynik

Sieć: MLP **11 → 32 → 16 → 1**, 1105 parametrów, uczenie **68 s** na płytce.

### Test na prawdziwym sprzęcie (6 zbiórek, 8528 bitów)

| Metoda | Recall | Precision | F1 |
|---|---|---|---|
| maska `seen0 & seen1` | 100 % | 24,1 % | 38,8 % |
| **sieć per bit** | 100 % | **99,8 %** | **99,9 %** |

Trafność **maski dokładnej** (przewidziana maska = prawdziwa maska flag):

| Metoda | bajty czyste | **bajty MIESZANE** | razem |
|---|---|---|---|
| maska `seen0 & seen1` | 137/137 = 100 % | **0/30 = 0 %** | 137/167 = 82 % |
| **sieć per bit** | 137/137 = 100 % | **30/30 = 100 %** | **167/167 = 100 %** |

Walidacja (vcan, ziarna 31-40) potwierdza: bajty mieszane **69/69 = 100 %**
wobec **0/69 = 0 %** odniesienia. Jeden fałszywy alarm na ~1800 bitów nie-flag.

### 4.1. Czy sieć jest tu naprawdę potrzebna?

W Eksperymencie 4.7 okazało się, że zmiana jednej stałej w regule daje prawie
tyle, co sieć — więc to pytanie zadano ponownie, tym samym protokołem (próg
dobierany na danych uczących, oceniany na sprzęcie):

| Podejście | F1 | maska w bajtach mieszanych |
|---|---|---|
| najlepszy pojedynczy próg (`carry_lift ≤ 0,5`) | 57,8 % | 3/30 = 10 % |
| najlepsza koniunkcja dwóch cech (`duty ≥ 0,308 AND toggle_rate ≤ 0,024`) | 81,9 % | 17/30 = 57 % |
| **sieć (11 cech)** | **99,9 %** | **30/30 = 100 %** |

**Tym razem odpowiedź jest przeciwna niż w 4.7: żadna prosta reguła nie zbliża
się do sieci.** Granica decyzyjna wymaga nieliniowego połączenia wielu cech —
najsilniejsza pojedyncza cecha ma AUC 0,966, ale to wciąż daleko od
rozstrzygnięcia. To pierwszy przypadek w tym projekcie, w którym uczenie
maszynowe jest **konieczne**, a nie tylko wygodne.

---

## 5. Znaczenie dla projektu

Domknięty zostaje łańcuch rozpoznania:

| Pytanie | Metoda | Skuteczność |
|---|---|---|
| Czy ten bajt zawiera flagi? | sieć per bajt (4.7) | Recall 100 %, F1 98,6 % |
| **Które bity są flagami?** | **sieć per bit (4.11)** | **maska 100 %** |
| Co te sygnały znaczą? | LLM (4.1) | 3-24 % — **nadal otwarte** |

Poprawa dotyczy wyłącznie **struktury** sygnału. Semantyka — co dany sygnał
oznacza fizycznie — pozostaje domeną LLM i nie została tknięta.

---

## 6. Zastrzeżenia

1. **Cały ruch jest syntetyczny.** W generatorze skalary częściowe zajmują
   **ciągły** zakres bitów (`bit_lo`…`bit_hi`), a flagi są pojedyncze — struktura
   jest bardzo regularna. Prawdziwe DBC bywają mniej uporządkowane. Wynik 100 %
   należy czytać jako „metoda działa na tak zdefiniowanym problemie", nie jako
   gwarancję na magistrali pojazdu.
2. **Mało przypadków trudnych**: 30 bajtów mieszanych w teście sprzętowym i 69
   w walidacji. 100 % na 30 przypadkach to mocna przesłanka, nie tysiące prób.
3. Sieć zakłada, że bajt **został już wskazany** jako zawierający flagi
   (przez regułę albo sieć z 4.7) — to drugi stopień kaskady, nie samodzielny
   detektor.

---

## 7. Pliki

- `esp_experiment_4_11_maska/collect_bits.py` — cechy per bit z SocketCAN
- `esp_experiment_4_11_maska/train_bits.py` — uczenie i ewaluacja z rozbiciem
  na bajty czyste/mieszane
- Dane: `/root/proj/bits_data/` (40 ziaren vcan), `/root/proj/data/bits_hw_*.json`
  (6 zbiórek sprzętowych), model `/root/proj/data/model_bits.pt`

---

## 8. Zadania otwarte

1. **Wdrożenie kaskady** w demonie: sieć per bajt (4.7) → sieć per bit (4.11),
   z eksportem wag do JSON i inferencją bez PyTorcha, tak jak zrobiono
   w `pi_observer_nn.py`.
2. **Walidacja na ruchu z prawdziwego pojazdu** — jedyne zastrzeżenie, którego
   żaden eksperyment tej sesji nie zdjął.
3. Test na korpusie z **nieciągłymi** skalarami częściowymi (rozproszone bity),
   żeby sprawdzić, czy sieć nie opiera się na regularności generatora.
