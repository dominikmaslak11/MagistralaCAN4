# Eksperymenty 4.11 i 4.12 — klasyfikacja maski bitowej

## UWAGA: pierwszy wynik tego eksperymentu został OBALONY własnym testem.
## Liczby obowiązujące znajdują się w sekcji 6.2.

## 1. Co badano — wyjaśnienie dla osoby spoza tematu

Dotychczasowe eksperymenty odpowiadały na pytanie **„który bajt zawiera flagi"**.
Ten odpowiada na trudniejsze: **„które konkretnie bity nimi są"**.

Obecna metoda jest prosta: bit uznaje się za flagę, jeśli kiedykolwiek widziano
go w stanie 0 **i** w stanie 1. Zapisuje się to jako `seen0 & seen1`.

**Ta zasada z definicji nie może zadziałać** w bajcie, gdzie flagi sąsiadują
z liczbą — bo bity liczby też się zmieniają. Trafność maski zmierzona
w Eksperymencie 4.3 wyniosła **15,8 %**.

## 2. Doprecyzowanie problemu — najważniejsze ustalenie

Pomiar odniesienia pokazał, że słabość **nie jest równomierna**:

| Typ bajtu | Udział | Trafność maski `seen0 & seen1` |
|---|---|---|
| zawiera **same flagi** | ~78 % | **100 %** — trywialne |
| **mieszany** (flagi + liczba) | ~22 % | **0 %** — nigdy nie trafia |

Metoda jest **idealna w łatwym przypadku i całkowicie bezradna w trudnym**.

**Dlatego ewaluacja raportuje oba typy osobno.** Gdyby podać samą średnią,
wyszłoby ~82 % i wyglądałoby to na drobną niedoskonałość, zamiast na zero
w co piątym przypadku.

## 3. Czym i jak to zrobiono

**Hipoteza:** bity liczby są **statystycznie powiązane**, a flagi **niezależne**:
- w liczniku bit 0 przełącza się najczęściej, bit 7 najrzadziej — powstaje
  malejący gradient częstości,
- zmiany propagują się przez **przeniesienia**: bit *i* zmienia się głównie
  wtedy, gdy bity poniżej są same jedynkami,
- flaga nie wykazuje związku z sąsiadami.

**Sieć:** 11 cech → 32 → 16 → 1, **1105 parametrów**. Wszystkie cechy liczone
przyrostowo.

**Protokół:** uczenie i walidacja na magistralach wirtualnych (57 280 bitów),
**test wyłącznie na prawdziwym MCP2515**.

## 4. Pliki wynikowe

| Plik | Co zawiera | Pochodzenie |
|---|---|---|
| `odniesienie_4.11_bity_ciagle.csv` | wynik metody obecnej, korpus ciągły | **przeliczone** z surowych danych |
| `odniesienie_4.12_bity_rozproszone.csv` | wynik metody obecnej, korpus rozproszony | **przeliczone** z surowych danych |
| `wynik_koncowy_porownanie.csv` | zestawienie wszystkich wariantów | wiersze odniesienia przeliczone; **wiersze sieci przepisane z wyjścia programu** |

**Zastrzeżenie do trzeciego pliku:** wyniki samej sieci nie zostały przeliczone
od nowa, bo model znajduje się na wyłączonej płytce. Pochodzą z wyjścia
programu z 17 sierpnia. Ponowna weryfikacja wymaga uruchomienia urządzenia.

## 5. Przykładowe dane

**Wejście** — 11 cech opisujących **jeden bit** (nie bajt):

```
czestosc=0.31  ranga_w_bajcie=0.86  stosunek_do_max=0.42  wypelnienie=0.55
sygnatura_przeniesienia=0.12  wspolzmiennosc_nizszy=0.03  ...
```

**Ground truth dla bajtu mieszanego** — przykład z korpusu rozproszonego:

```
bajt 5:  flagi = bity {1, 6}      liczba = bity {0, 2, 4, 5}
```

Bity flag i liczby są **przeplecione** — to właśnie przypadek, w którym metoda
`seen0 & seen1` zwraca wszystkie sześć bitów jako flagi, czyli maskę błędną.

## 6. Wynik końcowy

### 6.1. Pierwszy wynik — i dlaczego był zawyżony

Na pierwotnym korpusie sieć osiągnęła w bajtach mieszanych **30/30 = 100 %**.

Wynik wzbudził podejrzenie, bo w tym korpusie flagi **zawsze** siedziały na
najniższych bitach, a liczba tworzyła **ciągły** zakres tuż nad nimi. Sieć
mogła nauczyć się układu pozycji zamiast zasady.

Sprawdzono to: rozproszono bity (flagi na losowych pozycjach, bity liczby
przeplecione) i powtórzono pomiar **na tym samym sprzęcie**.

### 6.2. WYNIK OBOWIĄZUJĄCY — korpus rozproszony, prawdziwy sprzęt

| Metoda | Recall | Precision | F1 | Maska: bajty czyste | **Maska: bajty MIESZANE** |
|---|---|---|---|---|---|
| `seen0 & seen1` (odniesienie) | 100 % | 26,8 % | 42,3 % | 147/147 = 100 % | **0/37 = 0 %** |
| sieć uczona na korpusie ciągłym | 97,2 % | 92,4 % | 94,8 % | 147/147 = 100 % | **5/37 = 14 %** |
| **sieć uczona na danych reprezentatywnych** | 100 % | **96,5 %** | **98,2 %** | 147/147 = 100 % | **22/37 = 59 %** |

## 7. Wnioski — trzy, wszystkie istotne

1. **Model z pierwszego podejścia nie generalizuje.** Trafność maski w bajtach
   mieszanych spadła ze **100 % do 14 %** po usunięciu regularności korpusu.
   Wynik 100 % **był w znacznej mierze artefaktem konstrukcji danych**.
2. **Zadanie jest trudniejsze, niż się wydawało, ale metoda działa.** Model
   uczony na danych reprezentatywnych osiąga **59 %** wobec **0 %** metody
   obecnej. To duża poprawa, ale **nie jest to problem rozwiązany**.
3. **Klasyfikacja pojedynczych bitów pozostaje mocna** — F1 98,2 % wobec 42,3 %.
   Trudność leży w trafieniu **całej maski naraz**: wystarczy jeden błędny bit
   z ośmiu, żeby maska się nie zgadzała.

**Uwaga metodologiczna:** zastrzeżenie „generator daje ciągłe zakresy bitów,
sieć mogła nauczyć się tej regularności" zapisano przy publikacji pierwszego
wyniku jako ryzyko teoretyczne. **Sprawdzenie zajęło jedno popołudnie i obaliło
główną liczbę tamtego eksperymentu.** Zastrzeżenia trzeba sprawdzać, nie tylko
odnotowywać.
