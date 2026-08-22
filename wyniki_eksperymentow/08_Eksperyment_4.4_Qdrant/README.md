# Eksperyment 4.4 — Qdrant retrieval warm-start (podpowiedź z bazy wektorowej)

## WYNIK NEGATYWNY — metoda nie zadziałała tam, gdzie miała pomóc

## 1. Co badano — wyjaśnienie dla osoby spoza tematu

Skoro model językowy słabo radzi sobie z flagami, może pomoże mu **podpowiedź
z doświadczenia**? Pomysł: zapamiętać sygnały już rozpoznane, a przy nowym
sygnale wyszukać najbardziej podobne i pokazać modelowi: *„to wygląda jak
sygnały, które wcześniej okazały się flagami"*.

Do wyszukiwania podobieństwa służy **baza wektorowa** — w tym wypadku
**Qdrant**. Każdy sygnał opisywany jest zestawem liczb (wektorem cech), a baza
znajduje wektory najbliższe zadanemu.

**Warm-start** oznacza „start z podpowiedzią", w odróżnieniu od **baseline** —
pytania bez żadnej pomocy.

## 2. Czym i jak to zrobiono

**Opis sygnału:** 7-wymiarowy wektor cech zachowania — m.in. udział wartości
unikalnych, częstość zmian, wielkość skoków, oscylacyjność.

**Biblioteka odniesienia:** zbudowana z **innego przebiegu** ruchu (inne ziarno
losowości) niż dane testowe. To celowe — sprawdzamy, czy wiedza z jednej sesji
przenosi się na inną.

**Dane testowe:** 100 rzeczywistych okien Cold Start przechwyconych z żywej
magistrali.

**Ewaluacja:** 4 modele językowe × 2 warianty (baseline i warm-start).

## 3. Pliki wynikowe

| Plik | Co zawiera | Pochodzenie |
|---|---|---|
| `porownanie_modeli.csv` | wynik dla każdego z 4 modeli, przed i po podpowiedzi | **przeliczone** z `final_summary_4models.json` |
| `wynik_per_typ_sygnalu.csv` | rozbicie na typy sygnałów | przepisane z `Eksperyment_4.4_Raport_Koncowy_20260806.md` |

### Kolumny `porownanie_modeli.csv`

| Kolumna | Znaczenie |
|---|---|
| `model` | model językowy |
| `baseline_%` | trafność bez podpowiedzi |
| `warmstart_%` | trafność z podpowiedzią z bazy |
| `zmiana_pp` | różnica w punktach procentowych |
| `ocena` | poprawa / pogorszenie / bez zmian |

## 4. Przykładowe dane

**Wektor cech** opisujący jeden sygnał (7 liczb, znormalizowanych):

```
[0.031, 0.88, 0.42, 0.15, 0.67, 0.09, 0.55]
 ^unikalne ^dwell ^oscylacje ^delta_sr ...
```

**Podpowiedź wstawiona do pytania** (schemat):

```
Podobne sygnały z wcześniejszych obserwacji:
 - sygnal_A: bit_flag (podobienstwo 0.94)
 - sygnal_B: bit_flag (podobienstwo 0.91)
```

## 5. Jak sprawdzano poprawność

Zastosowano **ocenę parowaną** — ten sam zestaw 100 okien testowych puszczono
dwukrotnie przez każdy model: raz bez podpowiedzi, raz z podpowiedzią.
Porównanie dotyczy więc **tych samych danych**, co eliminuje wpływ losowego
doboru próby.

Trafność oceniano względem **ground truth** tak samo jak w Eksperymencie 4.1.

Dodatkowo sprawdzono **jakość samych podpowiedzi** — czy wskazany „podobny
sygnał" faktycznie był tego samego typu.

## 6. Wynik końcowy

**Per model:**

| Model | Baseline | Warm-start | Zmiana |
|---|---|---|---|
| Claude Sonnet 5 | 43,0 % | 42,8 % | −0,3 pp |
| GPT-5.6-sol | 33,2 % | 32,2 % | −1,0 pp |
| DeepSeek-v4-pro | 29,6 % | 32,1 % | **+2,4 pp** |
| Gemini-3.6-flash | 41,5 % | 37,2 % | **−4,3 pp** |

**Per typ sygnału — to jest istota wyniku:**

| Typ | n | Baseline | Warm-start | Zmiana |
|---|---|---|---|---|
| liczba (scalar) | 24 | 70,3 % | 73,3 % | +3,0 pp |
| **flaga bitowa** | **116** | **17,1 %** | **13,5 %** | **−3,5 pp** |
| liczba częściowa | 4 (mała próba) | 87,5 % | 100,0 % | +12,5 pp |

## 7. Wnioski

1. **Podpowiedź pomagała tam, gdzie pomoc nie była potrzebna** (liczby, +3 pp)
   i **szkodziła tam, gdzie była potrzebna** (flagi, −3,5 pp).
2. **Trafność samych podpowiedzi wynosiła ~70 %**, gdy budowano je z innego
   przebiegu. Błędna podpowiedź potrafiła **utwierdzić model w istniejącym
   błędzie** — regres sięgał −5,2 pp.
3. Ustalenie techniczne: **516 z 799 podpowiedzi (64,6 %)** dotyczyło bajtów
   niezawierających żadnego sygnału (wypełnienie). Po ich odfiltrowaniu trafność
   realnych podpowiedzi wyniosła 71,4 %.

**Znaczenie:** to wynik negatywny, ale wartościowy — pokazuje, że problem flag
bitowych nie jest problemem „braku wiedzy" modelu, który dałoby się uzupełnić
podpowiedzią. Model nie potrafi wykonać samej analizy, a podpowiedź tego nie
zastępuje.
