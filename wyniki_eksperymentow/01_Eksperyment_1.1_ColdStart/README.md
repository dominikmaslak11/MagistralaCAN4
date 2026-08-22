# Eksperyment 1.1 — Cold Start Latency (czas „zimnego startu")

## 1. Co badano — wyjaśnienie dla osoby spoza tematu

Gdy urządzenie zobaczy na magistrali ramkę, której **jeszcze nie rozumie**, musi
przejść całą drogę: wykryć, że to coś nowego, wysłać dane do modelu językowego,
poczekać na odpowiedź, przetworzyć ją i wgrać nową regułę.

Ta droga nazywa się **Cold Start** („zimny start"). Pytanie eksperymentu:
**ile to trwa i która część zajmuje najwięcej czasu?**

Odpowiedź decyduje o tym, gdzie warto optymalizować cały system.

## 2. Czym i jak to zrobiono

**Sprzęt:** ESP32 + MCP2515 (czyta magistralę) → WiFi/WebSocket → komputer
z aplikacją MagistralaCAN4 (C++/Qt6). Ruch CAN generowany przez PEAK PCAN-USB.

**Oprogramowanie:** realne komponenty aplikacji, nie symulacja —
`ColdStartDetector` (wykrywa nieznaną ramkę), `LlmQueryClient` (wysyła zapytanie
do modelu), `LatencyProfiler` (mierzy czas każdego etapu), `ExperimentRunner`.

**Przebieg:** dla każdego z 4 modeli językowych wykonano **30 niezależnych prób**.
W każdej próbie mierzono osobno czas pięciu etapów.

## 3. Pliki wynikowe

| Plik | Co zawiera |
|---|---|
| `input_data.csv` | **dane wejściowe** — co dokładnie wysłano do modelu i co odpowiedział |
| `raw_data.csv` | **surowe pomiary** — czas każdego etapu w każdej próbie |
| `statistics.csv` | **wynik końcowy** — średnie i odchylenia na model |

### Kolumny `raw_data.csv`

| Kolumna | Znaczenie |
|---|---|
| `Model` | który model językowy odpytywano |
| `Trial` | numer próby (0–29) |
| `CAN_ID` | identyfikator ramki, która wywołała zimny start |
| `t_det_ms` | czas wykrycia nieznanej ramki (milisekundy) |
| `t_tx_up_ms` | czas przesłania danych z ESP32 przez WiFi |
| `t_llm_ms` | **czas oczekiwania na odpowiedź modelu** |
| `t_comp_ms` | czas przetworzenia odpowiedzi |
| `t_ota_ms` | czas wgrania nowej reguły z powrotem na urządzenie |
| `t_total_ms` | suma całości |
| `success` | czy próba zakończyła się powodzeniem |

## 4. Przykładowe dane

**Wejście** (fragment `input_data.csv`) — surowe bajty ramki wysłane do modelu:

```
Model: gpt-5.6-sol | CAN_ID: 0x26e | Frame_Data_Hex: 2e4f82ab8479ff16
```

**Wyjście modelu** (skrócone) — model opisuje, czym jego zdaniem są bajty:

```
"Byte0 (0x2E): possible counter or low byte of a 16-bit signal;
 byte1 (0x4F): possible high byte/independent sensor value;
 byte6 (0xFF): likely unavailable/invalid-value sentinel..."
```

**Pomiar tej samej próby** (`raw_data.csv`):

```
t_det_ms=0.031  t_tx_up_ms=472.71  t_llm_ms=6058.0  t_ota_ms=699.034  t_total_ms=7229.775
```

## 5. Jak sprawdzano poprawność

W tym eksperymencie **nie oceniano trafności** odpowiedzi modelu — to przedmiot
Eksperymentu 4.1. Tutaj weryfikowano wyłącznie **poprawność pomiaru czasu**:

- próba liczy się tylko przy `success = True` (model odpowiedział, odpowiedź
  dała się sparsować),
- czasy etapów muszą sumować się do `t_total_ms`,
- pomiar prowadzony jest **zegarem jednego urządzenia** (komputera), więc nie
  wymaga synchronizacji zegarów między ESP32 a hostem.

## 6. Wynik końcowy (najlepsza, ostateczna wersja)

| Model | t_llm [ms] | T_total [ms] | N |
|---|---|---|---|
| Claude Sonnet 5 | **4741 ± 1305** | 5772 ± 1726 | 30 |
| GPT-5.6-sol | 4889 ± 1111 | 5866 ± 1251 | 30 |
| Gemini-3.6-flash | 8522 ± 3321 | 9488 ± 3216 | 30 |
| DeepSeek-v4-pro | 18642 ± 1352 | 19550 ± 1502 | 30 |

## 7. Wniosek

**Czas oczekiwania na model językowy stanowi ponad 50 % całości u każdego
modelu** — od ~4,7 s (najszybszy) do ~18,6 s (najwolniejszy). Wszystkie
pozostałe etapy razem to ułamek tego czasu.

**Konsekwencja dla projektu:** optymalizowanie sprzętu nie ma sensu, dopóki
wąskim gardłem są sekundy oczekiwania na model. Dlatego dalsze prace skupiły się
na **trafności** modelu, a nie na wydajności urządzenia.

**Zastrzeżenie:** czasy odpowiedzi modeli zależą od obciążenia serwerów
dostawcy i mogą się zmieniać w czasie. Pomiar opisuje stan z lipca 2026.
