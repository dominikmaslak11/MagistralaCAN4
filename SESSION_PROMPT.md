# MagistralaCAN4 — Session Context

## Projekt
Zaawansowany analizator magistrali CAN z GUI Qt6, uczeniem maszynowym i obsługą 11 protokołów automotive.
- **Wersja**: 2.2.0 | **Stack**: C++17, Qt 6.10, CMake 4.x, GoogleTest, Lua 5.4, OpenCL, SQLite
- **Platformy**: Windows (MSYS2/UCRT64, statyczne Qt6) + Linux (Kali, GCC 15, dynamiczne Qt6)
- **Repozytorium**: https://github.com/dominikmaslak11/MagistralaCAN4

## Stan projektu (2026-05-14)
- **Testy**: +21 (CanDashboardConfig) → 1190 razem, 10 pre-existing RemoteCan failures (wymagają Python)
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

## Ostatnia sesja — część 11: CanCustomDashboard (commit TBD)
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
2. **LogComparatorWidget** — porównanie dwóch plików candump (diff timeline)
3. **CANopen/OBD-II parsery** — PDO/SDO decode + OBD-II PID decode z tabelą Mode/PID
4. **MQTT bridge testy** — testy jednostkowe dla `MqttBridge`

## Uwagi architektoniczne (pułapki)
- `ArxmlParser`: zmienna `signals` → `sigDefs` (Qt `#define signals = public` powoduje conflict)
- `CanAlertEngine::submitExternalAlert()` — routing alertów spoza systemu reguł (używa np. CanModuleProfiler)
- `LuaScriptEngine`: callbacki `onFrame(id, data, ts)` + `onAlert(rule, id, desc, data, ts)`
- `QSortFilterProxyModel::invalidateFilter()` deprecated w Qt 6.10 — tylko warning, nie błąd
- `QApplication` musi być tworzone przed widgetami w testach — patrz `test_cangaugewidget.cpp` (SetUpTestSuite)
- CI/CD: `.github/workflows/build.yml` + `ci.yml` — Windows MSYS2 + Linux, lcov coverage, Codecov

## Pełna historia sesji
Szczegółowe logi wszystkich sesji: `CHANGELOG_SESSIONS.md`
