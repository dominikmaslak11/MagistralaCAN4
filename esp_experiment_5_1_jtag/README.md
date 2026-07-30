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

## Pomiar IDLE/PARSING z JTAG - WYKONANY, watchdog ZDIAGNOZOWANY I NAPRAWIONY (2026-07-30)

Pierwszy przebieg 3 wariantow (JTAG aktywny/bierny/brak) wykazal
niestabilnosc (task watchdog) w wariancie bez apptrace - PRZYCZYNA
ZDIAGNOZOWANA I NAPRAWIONA w tej samej sesji, opis ponizej. Wszystkie
4 warianty zostaly NASTEPNIE powtorzone na naprawionej architekturze i sa
w pelni czyste (0 bledow watchdoga, 0 utraconych pomiarow).

### Diagnoza przyczyny watchdoga

Surowy log bledu wskazywal: `IDLE0 (CPU 0)` zaglodzone, zadanie `main`
(CPU 0) dziala bez przerwy. Przyczyna: wlasnorecznie napisany `app_main()`
w `main.cpp` uruchamial `while(true) loop();` BEZPOSREDNIO w zadaniu
ESP-IDF "main" (domyslnie przypietym do CPU0), zamiast pozwolic
komponentowi `arduino-esp32` dostarczyc WLASNY, poprawny `app_main()`
(potwierdzone w zrodle:
`managed_components/espressif__arduino-esp32/cores/esp32/main.cpp` +
`Kconfig.projbuild`, opcja `CONFIG_AUTOSTART_ARDUINO`, domyslnie
WYLACZONA - stad koniecznosc pisania wlasnego `app_main()`). Poprawny
`app_main()` tworzy DEDYKOWANE zadanie `loopTask` na `ARDUINO_RUNNING_CORE`
(domyslnie CPU1) i pozwala zadaniu "main" (CPU0) zakonczyc sie od razu -
CPU0 zostaje wolne dla wlasnego IDLE0.

**Naprawa (2 zmiany w `sdkconfig.defaults` + usuniecie recznego
`app_main()` z `main.cpp`)**:
1. `CONFIG_AUTOSTART_ARDUINO=y` - komponent `arduino-esp32` sam dostarcza
   poprawny `app_main()`; `main.cpp` zawiera juz TYLKO `setup()`/`loop()`.
2. Po tej zmianie `loop()` poprawnie dziala na CPU1 - ale to WCIAZ petla
   pollujaca bez `yield`/`delay` (`if (!g_frameReady) return;`, swiadoma,
   udokumentowana cecha architektury dla niskiego opoznienia), wiec
   zaglodzenie przeniosło sie na `IDLE1 (CPU1)` (nadal nieszkodliwe,
   `CONFIG_ESP_TASK_WDT_PANIC` domyslnie WYLACZONE - to byl tylko
   nieszkodliwy, ale halasliwy log, NIE crash - oryginalny `.ino` z
   Arduino IDE nie ma tego problemu bo board package Arduino IDE domyslnie
   jest bardziej pobłazliwy dla watchdoga niz "surowy" ESP-IDF). Dodano
   `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=n` (CPU0, system-krytyczny,
   POZOSTAJE pod nadzorem).

Zweryfikowane: 60/60 pomiarow bez ijednego ostrzezenia watchdoga po
naprawie (przed: 40 ostrzezen/60 pomiarow). Pelne surowe logi diagnostyczne
(przed i po naprawie) w `traces/` (`watchdog_bezapptrace_dowod_20260730.txt`,
`diagnose_watchdog.py` - reuzywalny skrypt diagnostyczny z pelnym
logowaniem serial, zostaje w repo).

### Finalne, w pelni zweryfikowane zestawienie (N=40 kazdy wariant, 0 bledow)

`run_experiment_5_1.py` (bez zadnych zmian, ten sam protokol serial+CAN)
uruchomiony na 4 konfiguracjach firmware, wszystkie na NAPRAWIONEJ
architekturze (`CONFIG_AUTOSTART_ARDUINO=y` + `ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=n`):

| Wariant | CPU IDLE | CPU PARSING | Katalog wynikow |
|---|---|---|---|
| Oryginalny `.ino` (Arduino IDE, bez ESP-IDF/JTAG) | 0,598% | 0,599% | `Eksperyment_5.1_MemCPU_20260728_140500/` |
| ESP-IDF naprawiony, `APPTRACE_DEST_JTAG` **wylaczony w ogole** | 0,817% | 0,823% | `Eksperyment_5.1_JTAG_Naprawiony_BezApptrace_20260730/` |
| ESP-IDF naprawiony, JTAG podlaczony, SystemView **bierny** | 1,789% | 1,799% | `Eksperyment_5.1_JTAG_Naprawiony_Bierny_20260730/` |
| ESP-IDF naprawiony, JTAG, SystemView **aktywnie nagrywa** | 2,476% | 2,500% | `Eksperyment_5.1_JTAG_Naprawiony_Aktywny_20260730/` |

Wyniki na naprawionej architekturze sa PRAWIE IDENTYCZNE z wczesniejszymi
pomiarami na architekturze WADLIWEJ (2,488%/1,796% wczesniej vs
2,476%-2,500%/1,789%-1,799% teraz) - potwierdza to, ze sam pomiar `micros()`
byl wiarygodny NIEZALEZNIE od tego, na ktorym rdzeniu/zadaniu dziala
`loop()` (lokalny pomiar czasu wokol bloku pracy, agnostyczny wobec
architektury schedulera) - watchdog psul TYLKO ciaglosc dlugich przebiegow,
NIE poprawnosc pojedynczego pomiaru.

**Wniosek koncowy (efekt obserwatora, w pelni potwierdzony)**: narzut
rosnie MONOTONICZNIE i W PELNI ODTWARZALNIE na kazdym etapie:
- +0,22pp: sam toolchain ESP-IDF+arduino-as-component vs Arduino IDE
  (0,60%->0,82%) - NIE dotyczy JTAG.
- +0,97pp: samo skompilowanie z `APPTRACE_DEST_JTAG=y` (kanal trace
  "gotowy", nawet bez aktywnego przechwytu) (0,82%->1,79%) - **zaskakujaco
  duzy koszt samej gotowosci kanalu trace**, wiekszy niz koszt aktywnego
  strumieniowania.
- +0,69pp: aktywne strumieniowanie danych SystemView (1,79%->2,49%).

Rekomendacja dla finalnej tabeli metodyki Grupy 5: oryginalny wynik `.ino`
(0,6%) jako glowny, najmniej inwazyjny pomiar; pelna tabela 4 wariantow
jako materiał do dyskusji o granicach metod profilowania (Dodatek B.2
artykulu `Artykul_Naukowy_LLM_CAN_Bitowe_Flagi.md`).

Stan koncowy repo: `sdkconfig.defaults` zawiera PELNA, docelowa
konfiguracje (FREERTOS_HZ, AUTOSTART_ARDUINO, APPTRACE_DEST_JTAG+SV,
LOG_DEFAULT_LEVEL_WARN, ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=n) - firmware
zbudowany i wgrany w tym stanie (JTAG aktywny) na koniec sesji.

## Trzeci stan metodyki: OTA Update - ZREALIZOWANY (2026-07-30)

Metodyka (Grupa 5, Eksperyment 5.1) wymaga TRZECH stanow: Idle, Parsing &
Filtering, OTA Update ("moment aktualizacji i kompilacji nowej reguly w
pamieci"). Trzeci stan byl od poczatku projektu NIEZREALIZOWANY -
mechanizm aplikowania reguly LLM na urzadzeniu nie istnial w architekturze
(reguly LLM zyly wylacznie po stronie PC, `DecodingAccuracyRunner`).

**Zaprojektowany i wdrozony w tej sesji** (main.cpp + nowy
`esp_experiment_5_1/run_experiment_5_1_ota.py`):

- Nowy tryb `MODE_OTA=2` w firmware.
- Struktura `OtaRule` na urzadzeniu - pola 1:1 z `LlmSignalRule`
  (`src/core/DecodingAccuracyRunner.h`: `byteIdx`, `byteLen`,
  `littleEndian`, `isSigned`, `bitMask`, `scale`, `offset`) - swiadoma
  decyzja spojnosci architektonicznej z reszta projektu, zamiast
  wymyslania nowego formatu reguly.
- Protokol dostawy reguly: 4 ramki sterujace na CAN ID 0x7FE
  (`CTRL_OTA_LOAD1`=naglowek, `LOAD2`=bitMask, `LOAD3`=scale,
  `COMMIT`=offset+wyzwolenie kompilacji) - jedna regula NIE miesci sie w
  jednej 8-bajtowej ramce CAN, naturalne ograniczenie magistrali warte
  odnotowania w pracy.
- "Kompilacja" = realny zapis reguly do tabeli aktywnych regul na
  urzadzeniu (do 8 regul, analogicznie do tabeli klasyfikacji PARSING) -
  NIE no-op, mierzone tym samym mechanizmem `g_busyUs`/`micros()` co
  klasyfikacja w PARSING.

### Finalna tabela 3 stanow (N=20 kazdy, to samo binarium, ten sam
### `sdkconfig` - JTAG podlaczony biernie, bez aktywnego streamu)

| Stan | CPU [%] | RAM uzyte [kB] | Flash [kB] | Parametr obciazenia |
|---|---|---|---|---|
| IDLE | 1,792 | 29,50 | 272,4 | 50 ramek/s (bez pracy) |
| PARSING | 1,800 | 29,50 | 272,4 | 50 ramek/s (klasyfikacja per-ID) |
| OTA Update | 1,487 | 29,50 | 272,4 | 10 regul/s = 40 ramek sterujacych/s (kompilacja reguly LLM) |

Dane: `Eksperyment_5.1_3Stany_20260730/` (IDLE+PARSING),
`Eksperyment_5.1_OTA_20260730/` (OTA). Wszystkie pomiary bez bledow
(31/31 regul skompilowanych w kazdym oknie OTA, 151/151 ramek w kazdym
oknie IDLE/PARSING).

RAM/Flash identyczne miedzy stanami (STATYCZNA alokacja tabel regul,
niezalezna od aktywnego trybu - `g_otaRules`/`g_stats` sa zawsze
zaalokowane, tylko aktywnie UZYWANE w odpowiednim trybie). CPU OTA nizsze
niz IDLE/PARSING NIE oznacza "tanszej" pracy per-zdarzenie (kompilacja
reguly to WIECEJ pracy niz pojedyncza klasyfikacja) - to efekt NIZSZEJ
czestotliwosci zdarzen wybranej dla tego stanu (10 regul/s, realistyczne
dla rzadkich aktualizacji reguł, vs 50 ramek/s ciaglego ruchu CAN dla
IDLE/PARSING) - kazdy stan ma WLASNY, udokumentowany parametr obciazenia,
zgodnie z ta sama konwencja co reszta tego eksperymentu.

**WAZNE zastrzezenie terminologiczne**: "OTA Update" w tym projekcie
oznacza aktualizacje POJEDYNCZEJ reguly interpretacji sygnalu (zgodnie z
doslownym brzmieniem metodyki "kompilacji nowej reguly"), NIE klasyczna
aktualizacje calej binarki firmware (typowe znaczenie "OTA" w ekosystemie
ESP32) - warte jawnego wyjasnienia w pracy, zeby uniknac nieporozumienia.

## Nastepne kroki (NIEWYKONANE na koniec sesji 2026-07-30)

1. Zainstalowac SEGGER SystemView GUI do faktycznej analizy zebranych juz
   plikow `.svdat` (`traces/`).
2. Zdecydowac (uzytkownik/wykladowca), ktore dokladnie liczby (i przy
   jakiej konfiguracji JTAG) trafiaja do finalnej tabeli metodyki Grupy 5.
