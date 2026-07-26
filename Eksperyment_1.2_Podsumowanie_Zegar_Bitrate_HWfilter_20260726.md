# Eksperyment 1.2 — Hot Execution Latency: wpływ zegara MCP2515, bitrate CAN i filtracji sprzętowej

**Data:** 2026-07-26
**Sprzęt:** ESP32 + MCP2515 (SPI), magistrala CAN sterowana przez PEAK PCAN-USB (SocketCAN, Linux)

## Metodologia

Mierzony jest czas `t_resp` między przerwaniem sprzętowym MCP2515 (pin INT,
sygnalizujący nadejście ramki CAN) a reakcją systemu (zmiana stanu pinu GPIO
po zweryfikowaniu identyfikatora ramki). Pomiar wykonywany jest wewnętrznym
timerem mikrosekundowym ESP32 (`esp_timer_get_time()`), niezależnie
raportowany przez USB Serial po każdej próbie. Dla każdego wariantu zebrano
N=1000 prób.

## Wyniki

| Wariant | mean t_resp [µs] | stdev [µs] | min/max [µs] |
|---|---|---|---|
| Bazowy (kwarc MCP2515 8MHz, bitrate 250kbps) | 109,70 | 1,52 | 108 / 115 |
| Kwarc MCP2515 16MHz, bitrate 250kbps | 109,67 | 1,53 | 108 / 115 |
| Bitrate 125kbps (kwarc 16MHz) | 109,66 | 1,51 | 108 / 115 |
| Bitrate 500kbps (kwarc 16MHz) | 109,72 | 1,56 | 108 / 115 |
| Bitrate 1000kbps (kwarc 16MHz) | 109,66 | 1,47 | 108 / 115 |
| **Filtr sprzętowy MCP2515 + reakcja w ISR** | **1,01** | **0,10** | **1 / 2** |

## Wniosek 1 — zegar MCP2515 (8MHz → 16MHz) nie zmienia t_resp

Różnica (0,03µs) jest znacznie mniejsza niż błąd standardowy średniej
(~0,05µs przy N=1000) — rozkłady są statystycznie nieodróżnialne.

Wyjaśnienie: sterownik (`autowp/MCP2515`) przelicza rejestry generatora
taktowania bitowego (`CNF1`/`CNF2`/`CNF3`) osobno dla każdej kombinacji
(zegar, bitrate) — dla 250kbps: `CNF1=0x00/CNF2=0xB1/CNF3=0x85` przy 8MHz vs
`CNF1=0x41/CNF2=0xF1/CNF3=0x85` przy 16MHz. Zmieniony prescaler (BRP)
kompensuje podwojony zegar tak, by czas trwania jednego bitu na magistrali
pozostał identyczny. Poprawność deklaracji zegara w kodzie (`MCP_16MHZ`
zamiast `MCP_8MHZ`) była konieczna — inaczej rzeczywisty bitrate wyszedłby
2× za wysoki względem reszty magistrali.

## Wniosek 2 — bitrate CAN (125k–1000k, 8× rozpiętość) nie zmienia t_resp

Wszystkie cztery bitraty mieszczą się w przedziale 109,66–109,72µs.
Magistrala przy 1Mbps przeszła bez błędów (`berr-counter tx=0 rx=0`,
stan `ERROR-ACTIVE`).

Wyjaśnienie: `t_resp` liczony jest **od momentu przerwania INT**, czyli już
**po** pełnym odebraniu ramki (arbitraż + dane + CRC + ACK) — ten etap
zależy od bitrate, ale nie wchodzi do mierzonego okna czasowego. Sam pomiar
obejmuje wyłącznie fazę odczytu/reakcji po stronie hosta, całkowicie
odizolowaną od parametrów magistrali CAN.

## Wniosek 3 — struktura opóźnienia: gdzie faktycznie ucieka 109,7µs

Wariant z filtrem sprzętowym MCP2515 (maski/filtry RXM/RXF ustawione tak, by
tylko docelowy identyfikator ramki mógł wywołać przerwanie) pozwolił
przenieść reakcję GPIO **bezpośrednio do ISR**, przed odczytem treści ramki
przez SPI — bo dopasowanie ID jest już zagwarantowane sprzętowo.

Wynik (1,01µs, stdev 0,10µs) pokazuje, że z całkowitych ~109,7µs:

- **~1µs (1%)** to czysty narzut przerwania i operacji GPIO ESP32,
- **~108,7µs (99%)** to koszt transakcji SPI (odczyt ramki z MCP2515) i
  przetwarzania w firmware (weryfikacja identyfikatora).

**Uwaga metodologiczna:** wariant z filtrem sprzętowym mierzy coś innego niż
wariant bazowy — próg sprzętowy przerwania, a nie czas systemu decyzyjnego,
który musi odczytać treść ramki, by podjąć decyzję. Oba pomiary są
wartościowe, ale nieporównywalne wprost.

## Podsumowanie

Hot Execution Latency (~110µs) jest w pełni zdominowane przez ścieżkę
SPI + firmware po stronie ESP32, nie przez parametry sprzętowe kontrolera
CAN (zegar, bitrate). Ewentualna dalsza optymalizacja tego czasu powinna być
kierowana w stronę szybszej transakcji SPI / uproszczenia logiki odczytu
ramki, a nie w stronę magistrali CAN.
