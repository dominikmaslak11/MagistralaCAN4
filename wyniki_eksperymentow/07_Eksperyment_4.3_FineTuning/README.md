# Eksperyment 4.3 — Bootstrapped Fine-Tuning (klasyfikator jako nauczyciel modelu)

## 1. Co badano — wyjaśnienie dla osoby spoza tematu

Z Eksperymentu 4.1 wiemy, że **klasyczny klasyfikator** potrafi wskazać flagi
bitowe tam, gdzie model językowy zawodzi. Nasunęło to pytanie:

**Skoro klasyfikator umie coś, czego model nie umie — czy może go tego nauczyć?**

Pomysł: klasyfikator automatycznie oznacza tysiące przykładów („tu jest flaga,
tu jest liczba"), a następnie model uczy się na tych przykładach. W literaturze
nazywa się to **destylacją wiedzy**; tutaj nauczycielem jest algorytm klasyczny,
a nie inna sieć neuronowa — stąd **destylacja symboliczno-neuronowa**.

## 2. Czym i jak to zrobiono

Eksperyment podzielono na cztery etapy:

| Etap | Co obejmuje | Narzędzie |
|---|---|---|
| **A** | rozbudowa generatora ruchu — wiele różnych „mini-DBC" zamiast jednego | `generate_traffic_diverse.py` |
| **B** | automatyczne oznaczanie korpusu klasyfikatorem | `etap_b_autolabel.py` |
| **C** | przygotowanie danych w formacie treningowym | `etap_c_format_training_data.py` |
| **D** | właściwe douczenie modelu | — |

**Dlaczego rozbudowano generator (Etap A):** uczenie na wąskim zestawie
(3 identyfikatory, 1 przypadek flag) nauczyłoby model rozpoznawać **te konkretne
ramki**, a nie ogólną zasadę. Potrzebny był korpus zróżnicowany, żeby odróżnić
uogólnienie od zapamiętywania.

Nowy generator tworzy m.in.: flagi w różnych pozycjach bajtu, różną ich liczbę,
flagi rozproszone po wielu bajtach, **bajty mieszane** (część bitów to flagi,
część to mała liczba) oraz sygnały o różnej dynamice zmian.

## 3. Pliki wynikowe

| Plik | Co zawiera | Pochodzenie |
|---|---|---|
| `status_etapow.csv` | stan każdego z czterech etapów | przepisane z raportów |
| `wyniki_etapu_B.csv` | jakość klasyfikatora na wąskim i szerokim korpusie | przepisane z `Pytania_Do_Wykladowcy_Eksperyment_4.3_20260806.md` |

## 4. Przykładowe dane

**Wejście** — fragment wygenerowanej konfiguracji (ground truth):

```json
{"can_id": 768, "dlc": 8, "signals": [
   {"name": "flag_0_bit0", "kind": "bit_flag", "byte_idx": 7, "bit_idx": 0},
   {"name": "scalar_0", "kind": "scalar", "byte_idx": 0, "byte_len": 2, "scale": 0.1}
]}
```

**Wyjście Etapu B** — etykieta nadana automatycznie przez klasyfikator dla
każdej pozycji (CAN ID, bajt): „zawiera flagi" albo „nie zawiera".

## 5. Jak sprawdzano poprawność

Etykiety nadane przez klasyfikator porównywano z **ground truth** — czyli
z prawdziwą konfiguracją, którą sami wygenerowaliśmy. Liczono:

- **Precision** — jaki odsetek pozycji oznaczonych jako „flagi" faktycznie nimi był,
- **Recall** — jaki odsetek prawdziwych flag klasyfikator znalazł,
- **trafność maski** — czy wskazał **właściwe bity**, a nie tylko właściwy bajt.

Ostatnia metryka okazała się decydująca.

## 6. Wynik końcowy

| Korpus | Precision | Recall | F1 | Trafność maski |
|---|---|---|---|---|
| wąski mini-DBC (Eksp. 4.1) | — | ~97–100 % | — | nie mierzona |
| **szeroki korpus (Etap A)** | **82,6 %** | **55,9 %** | **66,7 %** | **15,8 %** |

## 7. Wniosek i decyzja o wstrzymaniu Etapu D

Klasyfikator, który na wąskim zestawie wyglądał niemal bezbłędnie, na szerszym
korpusie **znajdował tylko połowę flag**, a właściwe bity wskazywał
w **15,8 % przypadków**.

**Konsekwencja:** przy takiej jakości nauczyciela około **44 % przykładów
z flagami w zbiorze treningowym byłoby błędnie oznaczonych**. Uczenie modelu na
takich danych nauczyłoby go powielać błędy klasyfikatora.

**Etap D został świadomie wstrzymany** i skierowano pytanie do prowadzącego
(6 sierpnia): naprawiać najpierw klasyfikator, czy uczyć mimo szumu i sprawdzić
empirycznie, czy model to zniesie?

Zdiagnozowano dwie przyczyny słabości:
- **(a)** za krótkie okno obserwacji — część flag nie zdążyła przełączyć się
  w obu stanach,
- **(b)** **bajty mieszane** — klasyfikator nie odróżnia zmienności pochodzącej
  od flagi od zmienności pochodzącej od liczby w tym samym bajcie.

*Obie przyczyny zostały naprawione w eksperymentach 4.5–4.12. Etap D jest dziś
odblokowany.*
