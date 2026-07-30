# Eksperyment 5.1 — wariant ESP-IDF natywny (JTAG/SystemView)

Port 1:1 logiki z `esp_experiment_5_1/esp_experiment_5_1.ino` (Arduino) na
natywny projekt ESP-IDF. Powód: `configGENERATE_RUN_TIME_STATS`/SystemView
przez JTAG wymaga sdkconfig, niedostępnego z poziomu czystego szkicu `.ino`.
Kontekst i historia prób (proxy loop-spin → `vTaskGetRunTimeStats()` zawiódł
→ bezpośredni `micros()`) opisana w komentarzach `esp_experiment_5_1.ino`
oraz w pamięci projektu (Eksperyment 5.1, sesja 2026-07-28/29/30).

## Sprzęt

- ESP32 DevKitC (lub podobny), MCP2515 na SPI: CS=GPIO5, INT=GPIO4,
  SCLK=GPIO18, MOSI=GPIO23, MISO=GPIO19 (domyślne VSPI).
- Debugger: **ESP-Prog-2** (VID:PID `303a:1002` — natywny USB Espressif,
  NIE klasyczny FTDI ESP-Prog `0403:...`).
- JTAG (piny stałe na chipie ESP32, niezależne od debuggera):

  | ESP-Prog (opisane na płytce) | ESP32 |
  |---|---|
  | TMS | GPIO14 |
  | TDI | GPIO12 |
  | TCK | GPIO13 |
  | TDO | GPIO15 |
  | GND | GND |

  Zasilanie ESP32 zostaje przez jego własny kabel USB (CH340) - ESP-Prog
  dostarcza TYLKO sygnały JTAG, nie zasilanie.

## Środowisko

```bash
. ~/esp/esp-idf/export.sh
```

ESP-IDF w `~/esp/esp-idf` wymagał (jednorazowo, przy pierwszym użyciu tego
projektu, 2026-07-30) pełnej inicjalizacji WSZYSTKICH submodułów - część
(`esp_wifi/lib`, `esp_phy/lib`) nie była zainicjowana mimo pozornie
poprawnego stanu `git submodule status`:

```bash
cd ~/esp/esp-idf
git submodule update --init --recursive
# jesli konkretny submodul (np. esp_phy/lib) mimo to zostaje pusty
# (widac tylko plik .git, brak realnych plikow) - wymusic ponownie:
git submodule deinit -f components/esp_phy/lib
git submodule update --init --recursive --force components/esp_phy/lib
```

`sdkconfig.defaults` zawiera `CONFIG_FREERTOS_HZ=1000` - wymagane przez
`arduino-esp32` jako komponent (build inaczej odmawia konfiguracji:
`esp32-arduino requires CONFIG_FREERTOS_HZ=1000`).

## Build i flash

```bash
idf.py set-target esp32   # tylko raz / po fullclean
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor   # /dev/ttyUSB0 = CH340 devkitu, NIE ESP-Prog
```

Jesli `build/` zostaje w niespojnym stanie po przerwanym/nieudanym
`set-target` (blad "doesn't seem to be a CMake build directory"), po prostu
`rm -rf build/` i powtorzyc `set-target`.

## JTAG / OpenOCD

Uwaga: `/dev/ttyACM0` to WLASNY port UART ESP-Prog (osobny od JTAG) -
NIEUZYWANY w tym projekcie, nie mylic z `/dev/ttyUSB0` (devkit).

Wymagana jednorazowa instalacja reguly udev (bez tego: `LIBUSB_ERROR_ACCESS`):

```bash
sudo cp ~/.espressif/tools/openocd-esp32/*/openocd-esp32/share/openocd/contrib/60-openocd.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
# potem fizycznie odlaczyc/podlaczyc ESP-Prog
```

Test polaczenia (**WAZNE**: `interface/esp_usb_bridge.cfg`, NIE
`interface/ftdi/esp_ftdi.cfg`/`esp-prog.cfg` - ESP-Prog-2 uzywa natywnego
USB Espressif VID `303a`, nie klasycznego FTDI VID `0403` starszych rewizji
ESP-Prog):

```bash
openocd -f interface/esp_usb_bridge.cfg -f target/esp32.cfg
```

Zweryfikowane dzialajace (2026-07-30): oba rdzenie wykryte i "Examination
succeed", GDB server na porcie 3333.

## SystemView - konfiguracja i weryfikacja (ZROBIONE, 2026-07-30)

`sdkconfig.defaults` zawiera (symbole zweryfikowane bezposrednio w
`~/esp/esp-idf/components/app_trace/Kconfig`, nie z pamieci):

```
CONFIG_APPTRACE_DEST_JTAG=y
CONFIG_APPTRACE_SV_ENABLE=y
CONFIG_APPTRACE_SV_DEST_JTAG=y
```

Przechwyt trace (OpenOCD musi dzialac jako serwer w tle - patrz sekcja
JTAG/OpenOCD wyzej):

```bash
# w telnet na porcie 4444 (np. `nc localhost 4444` lub `telnet localhost 4444`):
esp32 sysview_mcore start file:///sciezka/do/pliku.svdat
# ... poczekac ...
esp32 sysview_mcore stop
```

Tworzy 3 pliki: `plik.svdat`, `plik.svdat_core0`, `plik.svdat_core1`.
Otwiera sie w SEGGER SystemView GUI (osobna aplikacja, NIE zainstalowana
w tym srodowisku na koniec tej sesji - do pobrania z segger.com,
darmowa dla tego zastosowania).

**Smoke test zweryfikowany (2026-07-30)**: 5s przechwytu, 90328 bajtow,
**0 utraconych bajtow, 0 niekompletnych blokow**, throughput ~21.5 KiB/s.
Caly lancuch (okablowanie -> JTAG -> OpenOCD -> SystemView -> plik)
dziala end-to-end.

## Pomiar IDLE/PARSING z JTAG - WYKONANY (2026-07-30)

`run_experiment_5_1.py` (z `esp_experiment_5_1/`, bez zadnych zmian - ten
sam protokol serial+CAN dziala identycznie na firmware -jtag) uruchomiony
3x z rozna konfiguracja apptrace, N=40 (20 IDLE + 20 PARSING) kazdy przebieg,
zeby zmierzyc "efekt obserwatora" JTAG/SystemView na sam wynik pomiaru CPU:

| Wariant | CPU IDLE | CPU PARSING | Status |
|---|---|---|---|
| Oryginalny `.ino` (Arduino, bez ESP-IDF/JTAG), `Eksperyment_5.1_MemCPU_20260728_140500` | 0,598% | 0,599% | OK 40/40 |
| ESP-IDF + JTAG, SystemView **aktywnie nagrywa** (`sysview_mcore start`), `Eksperyment_5.1_JTAG_Weryfikacja_20260730/` | 2,488% | 2,485% | OK 40/40 |
| ESP-IDF + JTAG podlaczony, SystemView **zatrzymany** (bez streamu), `Eksperyment_5.1_JTAG_BezPrzechwytu_20260730/` | 1,796% | 1,795% | OK 40/40 |
| ESP-IDF, `APPTRACE_DEST_JTAG` **wylaczony w ogole** (czysty ESP-IDF, bez apptrace) | ~0,82% (czesciowe dane) | ~0,82% (czesciowe dane) | **CRASH x2** (task watchdog) |

**Kluczowa obserwacja 1 (efekt obserwatora, potwierdzony CZESCIOWO)**:
aktywne strumieniowanie SystemView dodaje ~0,7pp CPU ponad sam fakt
skompilowania z `APPTRACE_DEST_JTAG` (2,488% vs 1,796%) - to jest realny,
mierzalny narzut samego JTAG-owego kanalu trace. NIE tlumaczy to jednak
calej roznicy wobec oryginalnego `.ino` (0,6%) - reszta (~1,2pp) to
prawdopodobnie architektura ESP-IDF+arduino-as-component vs czysty szkic
Arduino (inny scheduler/tick/uklad zadan), NIE JTAG.

**Kluczowa obserwacja 2 (WAZNIEJSZA, NIEOCZEKIWANA)**: wariant BEZ apptrace
(najblizszy "czystej" architekturze, mial byc kontrolny/najprostszy) okazal
sie NIESTABILNY - **Task Watchdog Trigger, powtarzalny w 2/2 probkach**,
zawsze w oknie ~45-140s dzialania. ANI JEDEN z dwoch przebiegow z
`APPTRACE_DEST_JTAG` wlaczonym (aktywny lub bierny) nie wykazal tego
problemu w pelnych 40-pomiarowych przebiegach. Surowe logi bledow:
`traces/watchdog_bezapptrace_dowod_20260730.txt`. Przyczyna
NIEZDIAGNOZOWANA - odwraca to prostsza hipoteze "JTAG tylko dodaje szum":
mozliwe, ze to WLASNIE wlaczenie apptrace przypadkowo stabilizuje
scheduler (np. przez okresowe "karmienie" watchdoga jako efekt uboczny),
a bez niego ujawnia sie realny, niezalezny problem architektury.
**WNIOSEK PRAKTYCZNY**: wynik "0,82%" z wariantu bez apptrace NIE jest
wiarygodny jako finalna liczba (niedokonczone przebiegi, nieznana
przyczyna niestabilnosci) - NIE uzywac go jako wyniku do pracy bez
dalszej diagnozy.

Dane: `Eksperyment_5.1_JTAG_Weryfikacja_20260730/`,
`Eksperyment_5.1_JTAG_BezPrzechwytu_20260730/` (oba `raw_data.csv` +
`report.txt`). Konfiguracja przywrocona na koniec sesji do wariantu
"JTAG aktywny" (`sdkconfig.defaults` z `APPTRACE_DEST_JTAG=y` - stan
zbudowany i wgrany na koniec tej sesji).

## Nastepne kroki (NIEWYKONANE na koniec sesji 2026-07-30)

1. Zdiagnozowac przyczyne task watchdog w wariancie bez apptrace (wymaga
   pelnego logowania serial do pliku podczas przebiegu, nie tylko
   przechwytu pierwszej linii bledu jak dzisiaj) - PRZED uznaniem
   ktoregokolwiek wyniku CPU z tego wariantu za wiarygodny.
2. Zainstalowac SEGGER SystemView GUI do faktycznej analizy `.svdat`
   (pliki juz sa: `traces/measurement_v2_20260730.svdat*`).
3. Zdecydowac, ktora liczba (0,6% oryginalna, ~2,5% JTAG-aktywny, czy
   ~1,8% JTAG-bierny) ma trafic do finalnej tabeli metodyki Grupy 5 -
   rekomendacja robocza: oryginalny wynik `.ino` jako glowny (najmniej
   inwazyjny pomiar), warianty JTAG jako Dodatek/dyskusja "efektu
   obserwatora" w artykule/raporcie, NIE jako zamiennik.
