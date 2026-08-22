# Eksperyment 4.5 — ciągła obserwacja magistrali (Raspberry Pi Zero W)

## 1. Co badano — wyjaśnienie dla osoby spoza tematu

Eksperymenty 4.3 i 4.4 pokazały słabości klasyfikatora. Postawiono pytanie:
**czy wynikały one z samej metody, czy tylko z tego, że patrzyliśmy zbyt krótko?**

Dotychczas urządzenie obserwowało magistralę **epizodycznie** — kilkanaście
sekund w ramach jednego uruchomienia programu. Flaga, która przełącza się raz na
20 sekund, mogła po prostu nie zdążyć się pokazać w obu stanach.

Pomysł: urządzenie, które **patrzy godzinami**, bez przerw.

## 2. Czym i jak to zrobiono

**Sprzęt:** Raspberry Pi Zero W + MCP2515 (nakładka Waveshare, kwarc 16 MHz).
Magistrala 250 kbit/s, ruch generowany przez PEAK PCAN-USB.

**Kluczowa zmiana techniczna:** statystyki liczone **przyrostowo**. Zamiast
trzymać w pamięci rosnącą listę wszystkich ramek, program aktualizuje kilka
liczników przy każdej ramce — czas i pamięć **stałe niezależnie od długości
obserwacji**. To warunek działania przez godziny na urządzeniu z 512 MB pamięci.

Równoważność matematyczną z wersją nieprzyrostową **zweryfikowano 2000 losowych
testów, 0 rozbieżności**.

**Trzy fazy:**

| Faza | Co robiono |
|---|---|
| 1 | ciągły klasyfikator flag, bez modelu językowego |
| 2 | baza wektorowa Qdrant budowana **na żywo** z tego samego przebiegu |
| 5 | embeddingi neuronowe (model MiniLM) zamiast ręcznych cech |

## 3. Pliki wynikowe

| Plik | Co zawiera | Pochodzenie |
|---|---|---|
| `przebieg_godzinny_snapshoty.csv` | stan klasyfikatora co 120 s przez godzinę | **przeliczone** z `snapshots.jsonl` |
| `faza2_qdrant_ewaluacja.csv` | trafność bazy wektorowej w czasie | **przeliczone** z `warmstart_eval.jsonl` |
| `faza5_embeddingi_neuronowe.csv` | porównanie cech ręcznych z embeddingiem | **przeliczone** z `neural_embedding_results.json` |

### Kolumny `przebieg_godzinny_snapshoty.csv`

| Kolumna | Znaczenie |
|---|---|
| `czas_s` | ile sekund trwała obserwacja |
| `ramek_lacznie` | ile ramek przetworzono od startu |
| `pozycji_sledzonych` | ile pozycji (CAN ID, bajt) jest obserwowanych |
| `flag_wykrytych` | ile pozycji klasyfikator uznał za flagi |

## 4. Przykładowe dane

```
czas_s,ramek_lacznie,pozycji_sledzonych,flag_wykrytych
120.0,54570,160,12
3600.3,1625649,160,12
```

Odczyt: po godzinie przetworzono **1 625 649 ramek**, śledzono 160 pozycji,
klasyfikator wskazał 12 z nich jako flagi.

## 5. Jak sprawdzano poprawność

1. **Równoważność z wersją offline** — funkcje liczące werdykt są w tym programie
   **tymi samymi funkcjami** co w wersji nieprzyrostowej, tylko liczonymi inaczej.
   Sprawdzono 2000 losowych przypadków: **0 rozbieżności**.
2. **Weryfikacja bazy wektorowej 1:1** — wynik ewaluacji na żywo porównano
   z wynikiem skryptu offline na tym samym korpusie: **91,6 % / 91,6 %,
   identyczna macierz pomyłek**.
3. **Ground truth** — jak w pozostałych eksperymentach, ruch generowany
   własnym programem o znanej konfiguracji.

## 6. Wynik końcowy

**Faza 1 — klasyfikator ciągły (po strojeniu progu):**

| Metryka | Wynik |
|---|---|
| Recall | **85,0 %** (17 z 20 flag) |
| Precision | **100 %** (0 fałszywych alarmów) |
| Ramek | 1 625 649 w ciągu godziny |

**Strojenie progu — istotne ustalenie:** domyślna wartość 0,5 ucinała 5 z 20
prawdziwych flag **bez żadnej korzyści w precyzji**. Przyjęto **0,3**.

**Faza 2 — Qdrant na żywo:** 91,6 % / 91,6 % (w obrębie tego samego przebiegu).

**Faza 5 — embeddingi neuronowe:**

| Test | Metoda | Trafność 3-klasowa |
|---|---|---|
| w obrębie korpusu | cechy ręczne (7-wym.) | 89,5 % |
| w obrębie korpusu | embedding neuronowy (384-wym.) | 98,7 % |
| **między korpusami** | cechy ręczne | 92,8 % |
| **między korpusami** | **embedding neuronowy** | **100 %** |

## 7. Wnioski i zastrzeżenia

1. **Ciągła obserwacja realnie pomogła** — Recall wzrósł z 55,9 % (Eksperyment
   4.3) do 85 %, przy zachowaniu 100 % precyzji.
2. **Embeddingi neuronowe rozwiązały problem generalizacji z Eksperymentu 4.4** —
   100 % trafności między korpusami wobec ~70 % dla bazy z ręcznymi cechami.
3. **Zastrzeżenie do Fazy 5:** wykonano ją **wyłącznie offline, na laptopie**.
   Na Raspberry Pi Zero W było to niewykonalne — PyTorch nie ma pakietów dla
   architektury ARMv6. Wdrożenie wymagałoby innego sprzętu.

*To zastrzeżenie zostało zdjęte w Eksperymencie 4.6.*
