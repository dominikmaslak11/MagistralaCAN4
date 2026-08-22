# Eksperyment 4.1 — Decoding Accuracy (trafność dekodowania przez model językowy)

**To jest trzon całego projektu.**

## 1. Co badano — wyjaśnienie dla osoby spoza tematu

Model językowy dostaje surowe bajty z magistrali i ma **odgadnąć, co one
znaczą** — gdzie jest temperatura, gdzie obroty, a gdzie pojedyncze flagi
(kierunkowskaz, drzwi).

Pytanie: **czy model to potrafi, a jeśli nie — czego dokładnie nie potrafi?**

## 2. Czym i jak to zrobiono

**Narzędzie:** `DecodingAccuracyRunner` (C++/Qt6) — moduł aplikacji MagistralaCAN4.
Ruch CAN generowany własnym programem, więc **znamy prawdziwe znaczenie każdego
sygnału** (ground truth).

**Przebieg jednej próby:**
1. program pokazuje modelowi historię ramek jednego CAN ID,
2. model odpowiada listą reguł („bajt 3 to liczba ze skalą 0,5"),
3. program **dekoduje 10 kolejnych ramek** regułą modelu i regułą prawdziwą,
4. porównuje wyniki.

**Wykonano 100 prób na każdy wariant.** Testowano 5 wariantów:

| Wariant | Na czym polega |
|---|---|
| 1. zero-shot | zwykłe pytanie, bez podpowiedzi |
| 2. few-shot | w pytaniu dodano przykłady poprawnych odpowiedzi |
| 3. entropy-analysis | wymuszono w pytaniu procedurę analizy zmienności bajtu |
| 4. naprawa kontekstu | poprawiono błąd: historia ramek zawierała wcześniej ramki obcych CAN ID |
| 5. **override hybrydowy** | dołożono klasyczny klasyfikator nadpisujący decyzję modelu |

## 3. Pliki wynikowe

| Plik | Co zawiera |
|---|---|
| `porownanie_wariantow.csv` | **wynik główny** — wszystkie 5 wariantów i 4 modele w jednej tabeli |
| `wyniki_per_sygnal.csv` | wynik końcowy (override) w rozbiciu na 10 pojedynczych sygnałów |

### Kolumny `porownanie_wariantow.csv`

| Kolumna | Znaczenie |
|---|---|
| `wariant` | który z pięciu wariantów |
| `model` | model językowy |
| `prob_N` | liczba prób (zawsze 100) |
| `wykrywalnosc_srednia_%` | średnia ze wszystkich 10 sygnałów |
| `wykrywalnosc_flagi_bitowe_%` | **tylko 5 sygnałów typu flaga** |
| `wykrywalnosc_sygnaly_ciagle_%` | tylko 5 sygnałów typu liczba |
| `wykrywalnosc_z_override_%` | wynik po zastosowaniu override'a |
| `katalog_zrodlowy` | skąd pochodzą dane |

## 4. Przykładowe dane

**Wejście dla modelu** — historia ramek jednego CAN ID (fragment):

```
ID=0x200  [0x00, 0x2E, 0x4F, ...]
ID=0x200  [0x04, 0x2E, 0x50, ...]
ID=0x200  [0x00, 0x2F, 0x50, ...]
ID=0x200  [0x44, 0x2F, 0x51, ...]
```

Bajt 0 skacze: 0 → 4 → 0 → 68. To są **flagi bitowe** (zmieniają się
pojedyncze bity). Bajt 1 rośnie płynnie: 46 → 46 → 47 → 47 — to **liczba**.

**Typowa błędna odpowiedź modelu:**

```json
{"name": "EngineStatus", "byteIdx": 0, "byteLen": 1, "scale": 1.0}
```

Model uznał bajt 0 za **jedną liczbę 0–255**, zamiast rozbić go na osobne bity.
To jest dokładnie ten błąd, który powtarzał się we wszystkich wariantach.

**Odpowiedź poprawna** (i to, co produkuje override):

```json
{"name": "LeftTurn",  "byteIdx": 0, "bitMask": 4}
{"name": "Lights",    "byteIdx": 0, "bitMask": 64}
```

## 5. Jak sprawdzano poprawność — kryteria walidacji

### Kryterium 1: czy sygnał został wykryty

**Dla flagi bitowej — warunki ostre:** reguła musi dotyczyć **tego samego bajtu**
i wskazywać **dokładnie ten sam pojedynczy bit**. Reguła „cały bajt to jedna
liczba" **nie zalicza się**, nawet przy właściwym bajcie.

**Dla sygnału ciągłego — łagodniej:** wystarczy reguła na tym samym bajcie.

### Kryterium 2: czy odczytana wartość się zgadza

Każda z 10 ramek dekodowana jest dwa razy — regułą prawdziwą i regułą modelu.

- **Flagi:** wartość ≥ 0,5 traktowana jako „włączone". Zliczane są trafienia,
  fałszywe alarmy i przeoczenia → z tego **Precision, Recall, F1**.
- **Sygnały ciągłe:** liczony **RMSE** (średni błąd kwadratowy) — im mniejszy,
  tym lepiej.

Próg 0,5 jest wartością neutralną, nie był dobierany pod wynik.

## 6. Wynik końcowy (najlepsza, ostateczna wersja)

### 6.1. Punkt wyjścia — cztery modele

| Model | Wszystkie sygnały | **Flagi bitowe** |
|---|---|---|
| GPT-5.6-sol | 67,9 % | 35,8 % |
| Gemini-3.6-flash | 50,9 % | 3,0 % |
| Claude Sonnet 5 | 47,9 % | **0,0 %** |
| DeepSeek-v4-pro | 41,5 % | 6,7 % |

Sygnały ciągłe: 79–100 % u wszystkich. **Claude nie wykrył ani jednej flagi
w stu próbach.**

### 6.2. Cztery próby naprawy przez pytanie — wszystkie nieudane

| Wariant | Średnia | Flagi |
|---|---|---|
| zero-shot (odniesienie) | 47,9 % | **0,0 %** |
| few-shot | 46,2 % | **0,0 %** |
| entropy-analysis | 46,7 % | **0,0 %** |
| naprawa kontekstu | 47,4 % | **0,0 %** |

### 6.3. WYNIK KOŃCOWY — override hybrydowy

| | Surowy model | **Model + override** |
|---|---|---|
| Średnia (10 sygnałów) | 50,0 % | **96,7 %** |
| Flagi bitowe (5 sygnałów) | F1 = 0 | **F1 = 1,000** |
| Sygnały ciągłe | — | **bez pogorszenia** |

## 7. Wnioski

1. **Modele dobrze radzą sobie z liczbami, a nie radzą sobie z flagami.**
   Cztery niezależne interwencje w treści pytania nie zmieniły tego ani o punkt.
2. **Problem nie leży w tym, co mówimy modelowi.** Nawet poprawa jakości danych
   wejściowych nie pomogła — a u GPT-5.6-sol wręcz zaszkodziła (spadek o 24 pp).
3. **Rozwiązaniem okazała się zmiana podziału pracy**, nie zmiana pytania:
   klasyczny klasyfikator przejmuje tę jedną decyzję, model robi resztę.

**Ważne zastrzeżenie — błąd znaleziony w trakcie badań:** pierwsza wersja
override'a fałszywie klasyfikowała szerokozakresowe liczby jako flagi —
**29 z 34 prób** na jednym CAN ID. Naprawiono, dodając warunek „od 2 do 6 bitów".
Wyniki w tym katalogu pochodzą z wersji poprawionej.

Szczegóły mechanizmu: `../../Override_Hybrydowy_Wyjasnienie_20260820.md`.
