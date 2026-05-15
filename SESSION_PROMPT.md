# MagistralaCAN4 — Session Context

## Projekt
Zaawansowany analizator magistrali CAN z GUI Qt6, uczeniem maszynowym i obsługą 11 protokołów automotive.
- **Wersja**: 2.2.0 | **Stack**: C++17, Qt 6.10, CMake 4.x, GoogleTest, Lua 5.4, OpenCL, SQLite
- **Platformy**: Windows (MSYS2/UCRT64, statyczne Qt6) + Linux (Kali, GCC 15, dynamiczne Qt6)
- **Repozytorium**: https://github.com/dominikmaslak11/MagistralaCAN4

## Stan projektu (2026-05-15)
- **Testy**: 1202 razem (part 17-18), 10 pre-existing RemoteCan failures (wymagają Python)
- **Protokoły**: CAN classic, CAN FD (BRS/ESI), LIN, J1939, UDS (ISO 14229), KWP2000 (ISO 14230), XCP, SOME/IP, DoIP (ISO 13400), CANopen, OBD-II
- **Formaty sygnałów**: DBC (Vector), ARXML (AUTOSAR 4.x)
- **Eksport**: candump, CSV, MDF4, PCAP (Wireshark), Python/Lua/Arduino prototype code

## Kluczowe moduły
| Klasa | Rola |
|-------|------|
| `ICanDriver` | Abstrakcja sterownika (SocketCanDriver / SlCanDriver / PcanDriver) |
| `CanSniffer` | Przechwytywanie ramek, throttling, dispatch do widgetów |
| `LearningEngine` | 37 algorytmów ML — 6 plików: core / filters / correlations / models / clustering / persistence |
| `AssociativeLearner` | Cienka warstwa UI dla LearningEngine |
| `CanObservationDb` | SQLite-backed frame storage — WAL, batched inserts (500 ramek / transakcja) |
| `CanAlertEngine` | Rule-based alerts — 5 typów: NewCanId, DlcChange, ByteValue, RateAnomaly, IsoForestScore |
| `HttpRestServer` | REST API: GET /status /frames /ids /alerts + POST /send /start /stop |
| `CanModuleProfiler` | Detekcja heartbeat modułu CAN, uczenie różnicowe 2-fazowe (A \ B) |
| `CanPrototypeExporter` | Generowanie kodu Python (python-can) / Lua / Arduino z ruchu CAN |
| `BusLoadAnalyzer` | Sliding-window % wykorzystania pasma, top loaders per ID |
| `CanProtocolTimelineWidget` | Qt6 Charts scatter — oś czasu zdarzeń protokołów |
| `CanByteHeatmapWidget` | Custom paintEvent — heatmapa wartości bajtów per ID w czasie |
| `IcSimDecoder` | Decoder/encoder ramek ICSim (Open Garages) — speed/doors/signals |
| `IcSimWidget` | Panel sterowania symulatorem ICSim — dashboard, slider, przyciski drzwi/sygnałów |
| `CanDashboardConfig` | Dane: `GaugeConfig` + `DashboardLayout` — JSON serialization, addGauge/removeGauge/clear |
| `CanCustomDashboard` | Konfigurowalny dashboard — wybór sygnałów DBC, tryb edycji, persist QSettings/JSON |
| `CanForensicsWidget` | Panel reverse-engineering: bit profiler, interval stats, payload pattern search |
| `CanTriggerWidget` | GUI wyzwalacza: arm/disarm, warunek (ID/bajt/błąd), bufor pre/post, eksport candump |
| `CanSignalStatisticsWidget` | Statystyki DBC: min/max/mean/σ/CV%/histogram, filtr, CSV export, auto-refresh 2s |
| `CanBusHealthWidget` | Monitor błędów magistrali (10 klas SocketCAN, BUS-OFF alert, rate) + Walidator liczników AUTOSAR |

## Ostatnia sesja — 2026-05-15: GvretDriver integracja z MagistralaCAN4
- **`GvretDriver.h/cpp`**: nowy backend ICanDriver — binarny protokół GVRET (EVTV), 115200 baud
  - Auto-detekcja: sonduje `{F1 08}`, szuka odpowiedzi z `F1 08` → etykieta `"COM3 [GVRET-ESP32]"`
  - `readFrame()`: parsuje pakiety `F1 00 [ts:4LE] [id:4LE, bit31=ext] [(bus<<4)|dlc] [data]`
  - `writeFrame()`: buduje `F1 01 [id:4LE, bit31=ext] [dlc] [data]`
- **MainWindow**: `m_gvretDriver` dodany; `refreshInterfaces()` sonduje GVRET; `toggleSniffing()` wybiera po `[GVRET`; gateway widget dodany
- **CMakeLists.txt**: `GvretDriver.cpp` w SOURCES, `GvretDriver.h` w HEADERS
- ESP32 aktualnie ma firmware GVRET (`esp_gvret.ino`); dla MagistralaCAN4-SLCAN flashuj `esp_slcan.ino`
- MagistralaCAN4.exe odbudowany → `build_native/MagistralaCAN4.exe` (2026-05-15 11:30)

## Poprzednia sesja — 2026-05-15: SLCAN bugfixy + GVRET firmware (SavvyCAN)
- **3 bugfixy `SlCanDriver.cpp`**: `open()` (`isEmpty` → `contains('\a')`), `parseIncoming()` (`dlcLen=2`→`1`), `writeFrame()` (DLC 2-cyfrowy → 1-cyfrowy dziesiętny) → rebuild MagistralaCAN4.exe
- **`esp_gvret/esp_gvret.ino`**: binarny protokół GVRET dla SavvyCAN (`Connection → Serial Connection → COM3`)
- ESP32 aktualnie ma firmware GVRET; dla MagistralaCAN4 flashuj `esp_slcan.ino`

## Poprzednia sesja — 2026-05-15: ESP MCP firmware + CAN → relay logika + SIM
- Subprojekt `esp_mcp/` — ESP32 + MCP2515 CAN bridge (bez zmian w głównym projekcie Qt6)
- Diagnoza: stary firmware nie odpowiadał na komendy seryjne; pobrano `arduino-cli.exe` dla Windows
- Skompilowano i wgrano `esp_mcp.ino` na ESP32-D0WD-V3 rev3.1 (COM3, FQBN `esp32:esp32:esp32`)
- Sterowanie 6 przekaźnikami przez serial 115200 baud: `RELAY <0-5> <ON|OFF>`
- Modyfikacja `handleCanRx()`: warunek `dlc==8` → `dlc>=2`; ramka EXT `0x1421003F` `data[1]=1/0` → relay 0 ON/OFF
- Dodana komenda `SIM EXT|STD <ID> <DLC> <B0..BN>` — wstrzykuje ramkę do `handleCanRx()` bez fizycznej magistrali
- Test potwierdzony: `SIM EXT 1421003F 2 00 01` → `STATUS relays: 100000`; `SIM EXT 1421003F 2 00 00` → `000000`

## Poprzednia sesja — część 14: CanBusHealthWidget (commit ea043f3)
- `CanBusHealthWidget.h/cpp` — panel zdrowia magistrali CAN z 2 zakładkami:
  - **Błędy magistrali**: alert BUS-OFF, liczniki 10 klas błędów SocketCAN (TxTimeout→Unknown), częstotliwość błędów (okno 5s), tabela zdarzeń błędów (czas/klasa/raw ID/opis), limit 500 wpisów
  - **Walidator liczników**: dodawanie/usuwanie reguł `CanCounterValidator::Config` (CAN ID, byteIndex, upper nibble, modulus), tabela statystyk live (OK, błędy, razem, % błędów), kolorowanie wierszy (zielony/żółty/czerwony)
- Integracja MainWindow: `frameProcessed` → `processFrame` (każda ramka, nie throttlowana), nowa zakładka "Zdrowie magistrali" w grupie Analiza
- Testy: 1190 (bez zmian — `CanBusErrorAnalyzer` i `CanCounterValidator` już testowane w poprzednich sesjach)

## Poprzednia sesja — część 13: CanTriggerWidget + CanSignalStatisticsWidget (commit 694392b)
- `CanTriggerWidget.h/cpp` — GUI dla CanTriggerRecorder: tryby (dowolna ramka z ID / bajt==wartość / ramka błędu), pre/post spinboxy, tabela przechwyconych ramek (PRE/TRIGGER/POST z kolorowaniem), eksport do candump; tab "Wyzwalacz" w Narzędzia
- `CanSignalStatisticsWidget.h/cpp` — statystyki sygnałów DBC: min/max/mean/stdDev/CV%/histogram UTF-8, auto-odświeżanie 2s, filtr nazwy, eksport CSV; tab "Statystyki sygnałów" w Analiza

## Poprzednia sesja — część 12: CanForensicsWidget (commit a1ff8e2)
- `CanForensicsWidget.h/cpp` — panel forensics z 3 zakładkami:
  - **Profil bitów**: wizualizacja CanBitAnalyzer (alwaysZero/alwaysOne/varying per bajt per ID), kolory zielony→czerwony, tooltip binarny, "Kopiuj raport"
  - **Interwały**: tabela CanIntervalAnalyzer (mean/stddev/min/max w ms, przerwy, typ cykliczny/sporadyczny)
  - **Szukaj wzorców**: CanPayloadSearch (hex pattern + maska + ID filter, rolling buffer 200k ramek, highlighting [match] w danych)
- Integracja MainWindow: nowa zakładka "Forensics" w grupie Analiza, frameProcessedThrottled

## Poprzednia sesja — część 11: CanCustomDashboard (commit fe18887)
- `CanDashboardConfig.h/cpp` — model danych: `GaugeConfig` (signalName, canId, style, useDbcRange, rangeMin/Max, unit) + `DashboardLayout` (columns, QVector<GaugeConfig>), JSON serialization
- `CanCustomDashboard.h/cpp` — Qt6 widget: siatka CanGaugeWidget, tryb edycji (przycisk ✕ per gauge), dialog dodawania (wybór sygnału DBC, styl, zakres), zapis/odczyt JSON + QSettings
- +21 testów (GaugeConfig, DashboardLayout round-trip, edge cases)
- Integracja z MainWindow: nowa zakładka "Konfigurowalny Dashboard" w grupie Przechwytywanie
- setDbcParser podpięte we wszystkich miejscach ładowania DBC/ARXML

## Poprzednia sesja — część 10: ICSim integration (commit `4bfb9d4`)
- `IcSimDecoder.h/cpp` — pure C++ decoder ramek ICSim: speed (0x244), doors (0x19B), signals (0x188)
- `IcSimWidget.h/cpp` — Qt6 widget z custom paintEvent (speedometr 0-90 mph, 270°, drzwi, strzałki)
- Obsługa seeded ICSim (`icsim -s <seed>`) — konfigurowalne CAN ID przez QSpinBox (hex)
- Wysyłanie ramek sterujących przez CanSniffer::writeFrame()
- Nowa zakładka "ICSim" w grupie Narzędzia → toolsTabs
- CMakeLists fix: CanExporter.cpp wyjęty z bloku HAS_XCB (pre-existing Windows linker bug)
- +28 testów (IcSimDecoder), wszystkie zielone

## Poprzednia sesja — Linux build fix (commit `093d414`)
Fix kompilacji GCC 15 / Qt 6.10 na Kali Linux:
- `SocketCanDriver.cpp:90` — `std::min(uint8_t, int)` → `std::min((int)frame.dlc, 64)` (GCC 15 ścisłe typy)
- `CanObservationDb.cpp` — `int64_t → qlonglong` w `addBindValue` (na Linux `int64_t = long`; Qt nie ma `QVariant(long)`)
- `CMakeLists.txt` — `pkg_check_modules(ZSTD)` przeniesione poza blok `WIN32`
- Wymagane pakiety Linux: `qt6-serialport-dev liblua5.4-dev libzstd-dev libgl-dev`
- Build: `cmake -B build_linux -S . -DCMAKE_BUILD_TYPE=Release && cmake --build build_linux --parallel $(nproc)`

## Poprzednia sesja — część 9: CanPrototypeExporter (commit `3bc6c62`)
- `CanPrototypeExporter.h/cpp` — generuje kod Python/Lua/Arduino z aktywnego ruchu CAN
- Detekcja liczników auto-increment (≥60% par z delta=1 mod 256)
- Źródła: CanFrameModel + DbcParser + ModuleProfile.periodicIds
- Nowa zakładka "Eksport kodu" w MainWindow
- +16 testów → 435/435

## Aktualny roadmap (priorytety malejące)
1. ~~**CanGaugeWidget integracja z DBC**~~ — DONE: `CanCustomDashboard` (część 11)
2. ~~**LogComparatorWidget**~~ — DONE (part 17): tryb per-ID + podświetlenie bajtów + bugfix parsera
3. ~~**MqttBridge**~~ — DONE (part 18): natywny klient TCP MQTT 3.1.1, bez mosquitto_pub
4. ~~**CanOpenWidget heartbeat**~~ — DONE (part 18): monitor węzłów, SDO decode, filtr węzła
5. **ObdWidget rozszerzenia** — DTC decode (Mode 3/7/0A), multi-frame ISO-TP reassembly, PID gauge
6. **CanNodeSimulator testy integracyjne** — skomplikowany komponent, słabo pokryty testami
7. **GvretDriver testy** — unit testy parsera pakietów + writeFrame

## Uwagi architektoniczne (pułapki)
- `ArxmlParser`: zmienna `signals` → `sigDefs` (Qt `#define signals = public` powoduje conflict)
- `CanAlertEngine::submitExternalAlert()` — routing alertów spoza systemu reguł (używa np. CanModuleProfiler)
- `LuaScriptEngine`: callbacki `onFrame(id, data, ts)` + `onAlert(rule, id, desc, data, ts)`
- `QSortFilterProxyModel::invalidateFilter()` deprecated w Qt 6.10 — tylko warning, nie błąd
- `QApplication` musi być tworzone przed widgetami w testach — patrz `test_cangaugewidget.cpp` (SetUpTestSuite)
- CI/CD: `.github/workflows/build.yml` + `ci.yml` — Windows MSYS2 + Linux, lcov coverage, Codecov

## Pełna historia sesji
Szczegółowe logi wszystkich sesji: `CHANGELOG_SESSIONS.md`
