# Eksperyment 5.1 — profilowanie zasobów, JTAG i efekt obserwatora

## 1. Co badano — wyjaśnienie dla osoby spoza tematu

Pytanie proste: **ile mocy obliczeniowej zużywa parsowanie ramek CAN na ESP32?**

Odpowiedź okazała się mniej oczywista, niż zakładano — bo **samo mierzenie
zmienia wynik pomiaru**.

**JTAG** to interfejs diagnostyczny wbudowany w procesor, pozwalający podglądać,
co się w nim dzieje. **SystemView** to narzędzie, które przez ten interfejs
rejestruje pracę systemu operacyjnego czasu rzeczywistego.

## 2. Czym i jak to zrobiono

Zmierzono zużycie procesora w **czterech różnych konfiguracjach**, żeby
rozdzielić koszt samego zadania od kosztu jego mierzenia:

| Konfiguracja | Co obejmuje |
|---|---|
| firmware Arduino | wyjściowy program, pomiar najprostszym sposobem |
| port na ESP-IDF | ten sam program, inne środowisko programistyczne |
| gotowość kanału trace | kanał diagnostyczny **włączony, ale nie zbiera danych** |
| aktywne śledzenie JTAG | pełny pomiar przez SystemView |

**Napotkany problem techniczny:** początkowo zadanie bezczynności rdzenia CPU0
było „głodzone" przez zadanie główne, które nigdy nie oddawało sterowania.
Rozwiązano to, przenosząc pracę na dedykowane zadanie na drugim rdzeniu.

## 3. Pliki wynikowe

| Plik | Co zawiera | Pochodzenie |
|---|---|---|
| `zuzycie_cpu_efekt_obserwatora.csv` | zużycie procesora w czterech konfiguracjach | przepisane z `Artykul_Naukowy_LLM_CAN_Bitowe_Flagi.md`, Dodatek B.2 |

### Kolumny

| Kolumna | Znaczenie |
|---|---|
| `konfiguracja` | wariant pomiaru |
| `zuzycie_CPU_%` | zmierzone obciążenie procesora |
| `przyrost_pp` | o ile wzrosło względem poprzedniego wariantu |
| `opis` | co dany wariant oznacza |

## 4. Przykładowe dane

```
konfiguracja,zuzycie_CPU_%,przyrost_pp,opis
firmware Arduino (wyjsciowy),0.60,,wartosc odniesienia - pomiar najmniej inwazyjny
gotowosc kanalu trace,1.79,0.97,sam kanal wlaczony, ale NIE zbiera danych
```

## 5. Jak sprawdzano poprawność

**Rozdzielenie kosztu na niezależne składowe** — zamiast jednego pomiaru „ile
zużywa program", wykonano cztery pomiary różniące się **jedną zmienną naraz**.
Dzięki temu można przypisać przyrost konkretnej przyczynie:

- 0,60 → 0,82 % = koszt zmiany środowiska programistycznego (nie dotyczy JTAG),
- 0,82 → 1,79 % = koszt **samej gotowości** kanału diagnostycznego,
- 1,79 → 2,49 % = koszt faktycznego zbierania danych.

## 6. Wynik końcowy

| Konfiguracja | Zużycie CPU | Przyrost |
|---|---|---|
| **firmware Arduino (odniesienie)** | **0,60 %** | — |
| port na ESP-IDF | 0,82 % | +0,22 pp |
| gotowość kanału trace | 1,79 % | **+0,97 pp** |
| aktywne śledzenie JTAG | 2,49 % | +0,70 pp |

**Jako wartość odniesienia dla projektu przyjęto 0,6 %** — pomiar najmniej
inwazyjny.

## 7. Wnioski

1. **Parsowanie ramek jest tanie** — 0,6 % mocy jednego rdzenia. Zasoby ESP32
   nie są ograniczeniem dla tego zastosowania.
2. **Wynik metodologicznie ciekawy:** samo **przygotowanie** kanału pomiarowego —
   jeszcze zanim zaczęto cokolwiek mierzyć — kosztowało **0,97 punktu
   procentowego**, czyli **więcej niż całe mierzone zadanie**.

   To klasyczny problem „obserwator wpływa na obserwowane zjawisko". Gdyby
   przyjąć wynik z aktywnego śledzenia (2,49 %) jako zużycie programu,
   przeszacowano by je **czterokrotnie**.

3. **Zrealizowano też trzeci stan metodyki** — aktualizację oprogramowania przez
   sieć (OTA).

**Zastrzeżenie:** pomiar dotyczy samego parsowania ramek, bez jednoczesnej
komunikacji sieciowej i bez zapytań do modelu językowego.
