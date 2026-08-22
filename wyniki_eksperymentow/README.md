# Wyniki eksperymentów — CAN-Edge-AI

Data zestawienia: 2026-08-20
Zakres: wszystkie eksperymenty projektu, lipiec – sierpień 2026

Ten katalog zawiera **końcowe, najlepsze wersje wyników** każdego eksperymentu
wraz z opisem, co i jak było robione. Każdy podkatalog ma własny `README.md`
i pliki `.csv` z danymi.

**Dokument jest napisany tak, żeby zrozumiała go osoba, która nie zajmowała się
wcześniej magistralą CAN ani modelami językowymi.** Sekcja 1 tłumaczy pojęcia,
sekcja 2 — jak oceniano poprawność wyników.

---

## 1. Co trzeba wiedzieć, żeby zrozumieć te wyniki

### 1.1. Czym jest magistrala CAN

W samochodzie (i w maszynie rolniczej) sterowniki rozmawiają ze sobą po jednej
wspólnej parze przewodów. Ten sposób komunikacji nazywa się **magistralą CAN**.

Po magistrali krążą **ramki** — krótkie paczki danych. Każda ramka ma:
- **identyfikator** (CAN ID), np. `0x100` — mówi, od kogo pochodzi i czego dotyczy,
- **do 8 bajtów danych** — czyli do 8 liczb z zakresu 0–255.

Przykładowa ramka:

```
ID=0x100   dane = [0x1A, 0x2F, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00]
```

### 1.2. Na czym polega problem

Same bajty nic nie znaczą, dopóki nie wiadomo, **jak je czytać**. Producent
pojazdu wie, że np. „w ramce `0x100`, bajt 0 i 1 to obroty silnika pomnożone
przez 0,25" — ale tej wiedzy nie udostępnia. Plik z takim opisem nazywa się
**DBC** i jest zwykle tajemnicą producenta.

**Celem projektu było sprawdzenie, czy model językowy (LLM) potrafi odgadnąć te
reguły, obserwując same ramki.**

### 1.3. Dwa rodzaje sygnałów — kluczowe rozróżnienie

To rozróżnienie przewija się przez wszystkie eksperymenty:

| Rodzaj | Co to | Przykład | Jak wygląda w danych |
|---|---|---|---|
| **Sygnał ciągły** (skalar) | Jedna liczba, zmieniająca się płynnie | temperatura, obroty, prędkość | wartość bajtu rośnie i maleje małymi krokami: 100, 101, 103, 102… |
| **Flaga bitowa** | Pojedynczy bit = jeden stan włącz/wyłącz | kierunkowskaz, drzwi otwarte, światła | wartość bajtu skacze o duże wartości: 0, 4, 0, 68, 64… bo zmienia się jeden bit wyższego rzędu |

**Bajt „mieszany"** to taki, w którym część bitów to flagi, a część tworzy małą
liczbę (np. bity 0–1 to flagi, bity 2–5 to licznik 0–15). Ten przypadek okazał
się najtrudniejszy.

### 1.4. Sprzęt użyty w projekcie

| Element | Rola |
|---|---|
| **ESP32** | mikrokontroler — „komputerek" wielkości pudełka zapałek, bez systemu operacyjnego |
| **MCP2515** | układ scalony tłumaczący sygnały magistrali CAN na coś, co ESP32 rozumie |
| **PEAK PCAN-USB** | profesjonalny adapter USB↔CAN, użyty jako **generator ruchu testowego** i punkt odniesienia |
| **Raspberry Pi Zero W / Orange Pi Zero 3** | małe komputery z Linuksem, użyte tam, gdzie ESP32 nie wystarczał |
| **Analizator stanów logicznych** | przyrząd mierzący napięcia na przewodach — użyty do **niezależnej weryfikacji** pomiaru czasu |

---

## 2. Jak oceniano, czy wynik jest poprawny

To pytanie jest kluczowe dla wiarygodności całego projektu, więc opisujemy je
dokładnie. Kryteria są zaimplementowane w `src/core/DecodingAccuracyRunner.cpp`.

### 2.1. Skąd wiadomo, jaka jest prawidłowa odpowiedź

Ruch CAN w eksperymentach jest **generowany przez nas** (`generate_traffic_diverse.py`).
Skoro sami go tworzymy, **znamy prawdziwe znaczenie każdego sygnału** — nazywa
się to **ground truth** („prawda podstawowa"). Dzięki temu można obiektywnie
sprawdzić, czy model trafił.

*To zarazem główne ograniczenie projektu — patrz sekcja 4.*

### 2.2. Kryterium 1: czy sygnał został w ogóle wykryty (`detectionRate`)

Model odpowiada listą reguł: „w bajcie 3 jest liczba", „w bajcie 0, bit 2, jest
flaga". Sprawdzamy, czy któraś z nich pasuje do prawdziwego sygnału:

**Dla flagi bitowej — warunki są ostre:**
- reguła musi dotyczyć **tego samego bajtu**, oraz
- musi wskazywać **dokładnie ten sam pojedynczy bit**.

Reguła mówiąca „cały bajt to jedna liczba" **nie zalicza się** jako wykrycie
flagi, nawet jeśli bajt jest ten sam. To jest właśnie ten typowy błąd modeli.

**Dla sygnału ciągłego — warunek jest łagodniejszy:**
- reguła dotyczy tego samego bajtu; reguła obejmująca cały bajt pasuje wprost.

`detectionRate` = odsetek prób (ze 100), w których sygnał został wykryty.

### 2.3. Kryterium 2: czy odczytana wartość jest poprawna

Sam fakt wskazania właściwego miejsca nie wystarcza — trzeba sprawdzić, czy
odczytana **wartość** się zgadza. Dla każdej ramki dekodujemy ją dwa razy:
regułą prawdziwą i regułą zaproponowaną przez model.

**Dla flag bitowych (wartość 0 albo 1):**

| Prawda | Model | Zapis |
|---|---|---|
| 1 | 1 | trafienie (TP) |
| 0 | 0 | poprawne odrzucenie (TN) |
| 0 | 1 | fałszywy alarm (FP) |
| 1 | 0 | przeoczenie (FN) |

Z tych czterech liczb liczone są:
- **Recall** (czułość) = ile prawdziwych flag udało się znaleźć
- **Precision** (precyzja) = jaki odsetek wskazań był trafny
- **F1** = miara łącząca obie, użyteczna gdy zależy nam na obu naraz

**Dla sygnałów ciągłych:** liczony jest **RMSE** — pierwiastek ze średniego
kwadratu różnicy między wartością prawdziwą a odczytaną. Im mniejszy, tym lepiej.

### 2.4. Dlaczego progu 0,5 użyto do klasyfikacji

Wartość zdekodowana jest liczbą rzeczywistą; flaga jest 0 albo 1. Przyjęto próg
**0,5** — wartość ≥ 0,5 traktowana jest jako „włączone". To standardowe,
neutralne rozwiązanie, nie strojone pod wynik.

---

## 3. Struktura katalogu

| Podkatalog | Eksperyment | Wynik |
|---|---|---|
| `01_Eksperyment_1.1_ColdStart` | ile trwa odgadnięcie reguły | pozytywny |
| `02_Eksperyment_1.2_HotExecution` | ile trwa reakcja na znaną regułę | pozytywny, zweryfikowany niezależnie |
| `03_Eksperyment_2.1_Przepustowosc` | czy mikrokontroler gubi ramki | pozytywny |
| `04_Eksperyment_2.2_Bufor` | kiedy przepełnia się bufor | pozytywny + znaleziona luka |
| `05_Eksperyment_3.1_Zasieg_radiowy` | zasięg WiFi/BLE | **przygotowany, nie zmierzony** |
| `06_Eksperyment_4.1_DecodingAccuracy` | czy LLM odgadnie reguły | 4 warianty negatywne, 1 pozytywny |
| `07_Eksperyment_4.3_FineTuning` | klasyfikator jako nauczyciel modelu | etapy A–C, D wstrzymany |
| `08_Eksperyment_4.4_Qdrant` | czy podpowiedź z bazy wektorowej pomoże | **negatywny** |
| `09_Eksperyment_4.5_CiaglaObserwacja` | czy dłuższa obserwacja naprawi klasyfikator | pozytywny |
| `10_Eksperyment_4.6_Replikacja_OrangePi` | czy wynik zależy od płytki | pozytywny |
| `11_Eksperyment_4.7-4.10_SiecNeuronowa` | sieć neuronowa kontra reguła | pozytywny + 3 hipotezy odrzucone |
| `12_Eksperyment_4.11-4.12_MaskaBitowa` | które bity są flagami | pozytywny **po korekcie własnego wyniku** |
| `13_Eksperyment_5.1_Profilowanie` | ile zasobów to kosztuje | pozytywny + wynik metodologiczny |

---

## 4. Ograniczenie wspólne dla wszystkich eksperymentów

**Cały ruch CAN jest syntetyczny** — generowany naszym własnym programem, nie
pobrany z prawdziwego pojazdu. Testy na różnych ziarnach losowości ograniczają
ryzyko, że metoda „nauczyła się na pamięć" jednego zestawu danych, ale nie
usuwają go całkowicie: inne ziarno to wciąż ten sam generator.

**Walidacja na magistrali prawdziwego pojazdu pozostaje zadaniem otwartym**
i jest najpoważniejszym ograniczeniem całej pracy. Żadnego z wyników nie należy
przedstawiać jako dowodu, że metoda zadziała w samochodzie.

---

## 5. Format plików

- Wszystkie pliki `.csv` używają **przecinka** jako separatora kolumn i **kropki**
  jako separatora dziesiętnego (standard międzynarodowy, zgodny z pandas/Excel
  przy imporcie z ustawieniem „angielskie").
- Kodowanie: **UTF-8**.
- Pierwszy wiersz każdego pliku to **nagłówki kolumn** opisane po polsku.
- Nazwy kolumn zawierają jednostkę w nazwie, np. `t_resp_us` = mikrosekundy,
  `wykrywalnosc_%` = procenty.
