# Zakładka „Ruch CAN" — dokumentacja interfejsu

## Lokalizacja w oknie głównym

```
QMainWindow
└── QTabWidget (główny, 4 grupy)
    └── "Przechwytywanie" (QTabWidget = captureTabs)
        ├── "Ruch CAN"                ← ta zakładka
        ├── "Szczegóły ramki"
        ├── "Dashboard CAN"
        ├── "Konfigurowalny Dashboard"
        ├── "Obciążenie magistrali"
        ├── "ID Statistics"
        ├── "Frame DB"
        ├── "Byte Heatmap"
        ├── "Timeline"
        └── "Trend sygnałów"
```

---

## Układ zakładki „Ruch CAN"

```
┌─────────────────────────────────────────────────────────────┐
│  PASEK NARZĘDZI (QToolBar, niemożliwy do przestawienia)     │  ← Warstwa 1
├─────────────────────────────────────────────────────────────┤
│  PANEL STATYSTYK (CanStatsPanel)                            │  ← Warstwa 2
├─────────────────────────────────────────────────────────────┤
│  TABELA RAMEK (QTableView)          │  HEATMAP BAR          │  ← Warstwa 3
│  (rozciąga się poziomo, flex)       │  (HeatmapBar, stała   │
│                                     │   szerokość, prawa    │
│                                     │   krawędź)            │
└─────────────────────────────────────────────────────────────┘
```

Margines i spacing layoutu = 0 (brak odstępów między warstwami).

---

## Warstwa 1 — Pasek narzędzi

### Sekcja A: Interfejs CAN

| Element | Typ | Opis |
|---|---|---|
| ComboBox interfejsu | `QComboBox` (edytowalny, min. 120 px) | Lista wykrytych interfejsów CAN: PCAN (`PCAN_USBBUS1` itp.), SLCAN (`COM3 [SLCAN]`), ESP-MCP2515 (`COM4 [ESP-MCP2515]`), GVRET (`COM5 [GVRET]`). Odświeżana co 5 s gdy nie sniffuje. |
| `↻` | `QPushButton` | Ręczne odświeżenie listy interfejsów. |
| ComboBox baudrate | `QComboBox` (min. 70 px) | Prędkość magistrali: `1M`, `800K`, `500K` (domyślna), `250K`, `125K`, `100K`, `50K`, `20K`, `10K`. |
| ─── | separator | |

### Sekcja B: Sterowanie sniffingiem

| Element | Typ | Opis |
|---|---|---|
| `▶ Start` / `■ Stop` | `QPushButton` | Uruchamia lub zatrzymuje nasłuchiwanie. Po starcie: interfejs/baud locked, timer batch 33 ms aktywny. Po stopie: bufor wyczyszczony (capacity zachowana). |
| `Nadpisywanie` | `QCheckBox` (domyślnie: ✓) | **Tryb nadpisywania** — każdy unikalny CAN ID zajmuje jeden wiersz w tabeli; przychodzące ramki z tym samym ID aktualizują istniejący wiersz (m_idToRow). **Tryb akumulacji** (odznaczone) — każda ramka tworzy nowy wiersz (max 10 000 w buforze cyklicznym). |
| `Nagrywaj candump` | `QCheckBox` (domyślnie: ✓, kolor #00ffaa) | Włącza automatyczny zapis ramek do `C:\candump\candump_YYYYMMDD_HHMMSS.log` w trakcie sniffingu. |
| ─── | separator | |
| etykieta statusu | `QLabel` | `Rozłączony` (kolor #ff4444) / `Nasłuchuje... (Backend, Baud)` (kolor #00ffaa) / `Nasłuchuje... ⚠ brak ramek — sprawdź magistralę/baud` (#ffaa00, po 5 s braku ramek). |
| ─── | separator | |

### Sekcja C: Eksport i nagrywanie

| Element | Opis |
|---|---|
| `🗙 Wyczyść` | Czyści model (`CanFrameModel::clear()`). Stary stan trafia do stosu undo (max 5 stanów). |
| `📥 Eksportuj candump` | Eksport aktualnie widocznych ramek do pliku `.log` (format: `(timestamp) iface ID#DLCdata`). Asynchroniczny (QtConcurrent). |
| `📊 Eksportuj CSV` | Eksport do `.csv` z nagłówkiem: `Index, Timestamp_us, ID(hex), Type, RTR, DLC, Data(hex), FD`. |
| `🦈 Eksportuj PCAP` | Eksport do `.pcap` (libpcap format) przez `PcapExporter`. |
| `⏺ Nagraj` | Zapis do pliku `.mcan` (binarny, własny format). Toggle: start/stop. |
| `📦 Nagraj MDF4` | Zapis do `.mf4` (MDF4, format AUTOSAR). Toggle: start/stop. |
| `🌐 REST API` | Uruchamia/zatrzymuje serwer HTTP REST na porcie 8080. |
| `📡 MQTT` | Włącza/wyłącza bridge MQTT (przesyła ramki na broker). |
| `☀️ Jasny motyw` | Przełącza między ciemnym i jasnym stylem. |

### Sekcja D: Wczytywanie plików

| Element | Opis |
|---|---|
| `📜 Wczytaj skrypt Lua` | `QToolButton` z menu MRU (ostatnio używane pliki `.lua`). Wczytuje skrypt Lua do silnika. |
| `🗄️ Wczytaj DBC` | `QToolButton` z menu MRU (ostatnio używane `.dbc`). Propaguje parser DBC do wszystkich widgetów (dashboard, participant, sygnały itp.). |
| `ARXML` | `QPushButton`. Import sygnałów z pliku AUTOSAR `.arxml`. Scala z istniejącym DBC (DBC wygrywa przy konflikcie ID). |

### Sekcja E: Presety filtrów

| Element | Opis |
|---|---|
| ComboBox `— filtry —` | Lista zapisanych presetów filtrów (format: `0xID — Nazwa`). Wybór automatycznie stosuje filtr. Presety przechowywane w `%USERPROFILE%/.magistrala_can4/filter_presets.txt`. |
| `+` (stały 28 px) | Zapisuje bieżący filtr z CanStatsPanel jako nowy preset (dialog tekstowy). |

### Sekcja F: Odtwarzanie nagrań

| Element | Opis |
|---|---|
| `📂 Wczytaj nagranie` | Otwiera dialog do wczytania pliku `.mcan` lub `.mcan.zst`. Przy wczytaniu zatrzymuje live sniffing. |
| `▶ Odtwórz` | `QPushButton` (90 px). Toggle: start/stop odtwarzania (`CanPlayer`). |
| ComboBox prędkości | `QComboBox` (70 px). Wartości: `0.5x`, `1x` (domyślna), `2x`, `5x`, `10x`, `Max` (nieograniczona). |

---

## Warstwa 2 — Panel statystyk (CanStatsPanel)

Widoczny nad tabelą, poniżej paska narzędzi.

| Kontrolka | Opis |
|---|---|
| FPS | Live licznik ramek/sekundę. |
| Unikalne ID | Liczba unikalnych CAN ID w aktualnej sesji. |
| Filtr ID | Pole tekstowe do filtrowania tabeli po ID (hex, np. `7DF`). Obsługuje wyrażenia logiczne: `can.id == 0x7DF`, `can.id > 0x700 && can.dlc == 8`, `!can.rtr`. |
| Pauza | Wstrzymuje aktualizację tabeli (ramki buforowane w tle, drainowane batch 500 ramek/33 ms po odblokowaniu). |

---

## Warstwa 3A — Tabela ramek (QTableView)

### Model danych

- Klasa: `CanFrameModel` (rozszerza `QAbstractTableModel`)
- Dane widoczne przez `CanFilterProxy` (filtrowanie wierszy po CAN ID lub wyrażeniu)
- Bufor cykliczny: max **10 000** ramek (`m_maxFrames`)
- Odświeżanie: timer 33 ms, batch do 500 ramek na tick

### Właściwości tabeli

| Właściwość | Wartość |
|---|---|
| Nagłówek pionowy (numer wiersza) | ukryty |
| Ostatnia kolumna | rozciąga się do końca |
| Siatka | wyłączona |
| Naprzemienne kolory wierszy | wyłączone |
| Sortowanie | włączone (klik nagłówka kolumny) |
| Scrollowanie | płynne (ScrollPerPixel) |
| Auto-scroll | włączony gdy scrollbar na dole; wyłącza się po ręcznym przewinięciu |

### Kolumny tabeli

| # | Nazwa | Typ danych | Opis |
|---|---|---|---|
| 0 | **ID** | hex uppercase | CAN ID ramki. Standardowy: 3 cyfry hex (np. `7DF`). Rozszerzony EFF: 8 cyfr (np. `18DA00F1`). |
| 1 | **EXT** | `E` / `S` | Typ ramki: `E` = Extended (29-bit ID), `S` = Standard (11-bit ID). |
| 2 | **RTR** | `RTR` / `-` | Ramka zdalna (Remote Transmission Request). |
| 3 | **DLC** | 0–8 (CAN) / 0–64 (CAN FD) | Data Length Code — liczba bajtów danych. |
| 4 | **DATA** | hex, spacje | Bajty danych oddzielone spacjami, np. `02 10 03 00 00 00 00 00`. Zmienione bajty podświetlane przez `DataHighlightDelegate` (kolor tła). |
| 5 | **TIMESTAMP** | µs | Czas od epoki Unix w mikrosekundach. |
| 6 | **FD** | `FD` / `XL` / `-` | Typ ramki FD: `FD` (CAN FD ISO), `XL` (CAN XL), `-` (klasyczny CAN). |
| 7 | **DELTA** | µs | Czas od poprzedniej ramki z tym samym CAN ID (inter-frame gap). Obliczany na bieżąco. |
| 8 | **SIGNAL** | tekst | Zdekodowana wartość sygnału z DBC/J1939/ARXML (jeśli załadowano). Dla J1939: PGN i SPN. |

### Podświetlanie danych (DataHighlightDelegate)

Kolumna DATA używa `DataHighlightDelegate`. Rola `ChangedMaskRole` (Qt::UserRole+1) to 64-bitowa maska — bit `i` = bajt `i` zmienił wartość względem poprzedniej ramki z tym samym ID. Zmienione bajty mają inne tło (kolor zależy od stylu/motywu).

### Tryb nadpisywania a akumulacji

**Nadpisywanie (domyślnie):** `m_idToRow` mapuje CAN ID → numer wiersza. Nowa ramka z istniejącym ID aktualizuje ten wiersz na miejscu — tabela pokazuje *aktualny stan* każdego węzła sieci. Liczba wierszy = liczba unikalnych ID.

**Akumulacja:** każda ramka → nowy wiersz. Chronologiczna historia wszystkich ramek do 10 000 pozycji. Po przekroczeniu: bufor cykliczny nadpisuje najstarsze.

---

## Warstwa 3B — HeatmapBar

Pionowy pasek po prawej stronie tabeli, wyrównany do jej scrollbara.

- Klasa: `HeatmapBar`
- Wizualizuje całą magistralę (wszystkie wiersze) proporcjonalnie do wysokości paska
- Komórki kolorowane według `BurstRole` (Qt::UserRole+2) — wykrywanie nagłego wzrostu częstotliwości ramki
- Umożliwia szybkie skakanie po tabeli kliknięciem w pasek (jak minimap)

---

## Menu kontekstowe (prawy klik w tabeli)

Menu pojawia się po kliknięciu prawym przyciskiem w komórce tabeli.

| Pozycja | Warunek | Opis |
|---|---|---|
| `Kopiuj CAN ID (0xXXX)` | ramka zaznaczona | Kopiuje hex ID do schowka. |
| `Kopiuj dane (hex)` | ramka zaznaczona | Kopiuje bajty danych jako `HH HH HH...` do schowka. |
| ─── | separator | |
| `Filtruj po ID 0xXXX` | ramka zaznaczona | Ustawia filtr CanStatsPanel na ten ID. |
| `Wyczyść filtr` | filtr aktywny | Usuwa bieżący filtr ID/wyrażenie. |
| ─── | separator | |
| `Kopiuj zaznaczone (TSV)` | zawsze | Kopiuje zaznaczone wiersze jako TSV z nagłówkiem (jak Ctrl+C). |

---

## Skróty klawiszowe

| Skrót | Zakres | Akcja |
|---|---|---|
| `Ctrl+C` | fokus na tabeli | Kopiuje zaznaczone wiersze do schowka (TSV z nagłówkiem). |
| `Ctrl+Z` | okno główne | Cofnij ostatnią operację niszczącą (clear, zmiana trybu). |
| `Ctrl+Y` | okno główne | Przywróć cofniętą operację (redo). |
| `Ctrl+Shift+E` | okno główne | Oznacz zdarzenie w uczeniu asocjacyjnym. |
| `Ctrl+Shift+D` | okno główne | Oznacz nie-zdarzenie w uczeniu asocjacyjnym. |

---

## Pipeline przetwarzania ramek

```
CanSniffer::newFrame
        │
        ▼
MainWindow::onNewFrame()
    ├── licznik m_totalFrames++
    ├── m_frameBuffer.append()
    ├── zapis do candump (jeśli włączone)
    └── CanStatsPanel::onNewFrame() (FPS, unique IDs)

Timer 33ms → updateTableBatch()
    ├── sniffer.drainAndEmit() (drenaż ring buffera sterownika)
    ├── pauza? → buforuj w tle
    └── batch (max 500 ramek/tick):
        ├── emit frameProcessed(frame)        → AssociativeLearner, LuaEngine,
        │                                        CanRecorder, MDF4, MQTT, Gateway,
        │                                        UdsSequence, IdStats, BusHealth,
        │                                        Trigger, AlertEngine, ModuleProfiler,
        │                                        ProtoExporter
        ├── emit frameProcessedThrottled(frame) → Dashboard, CustomDashboard, Forensics,
        │                                         SignalStats, J1939, SignalPlotter,
        │                                         BusLoad, SignalMonitor, Plugins,
        │                                         Timeline, Heatmap, SignalTrend, ICSim
        └── CanFrameModel::processIncomingFrames() → aktualizacja tabeli
```

---

## Dane candump (C:\candump\)

Katalog tworzony automatycznie przy pierwszym użyciu. Plik: `candump_YYYYMMDD_HHMMSS.log`.

Format wiersza:
```
(timestamp) interfejs ID#DLCdane
# Przykład klasyczny:
(1747080000.123456) PCAN_USBBUS1 7DF#8020600000000000
# Przykład CAN FD:
(1747080000.234567) PCAN_USBBUS1 18DA00F1##812345678
```

### Pliki na dysku (stan 2026-05-16)

| Plik | Rozmiar | Data |
|---|---|---|
| `candump_20260511_161633.log` | 13,7 KB | 11.05.2026 16:16 |
| `candump_20260511_165247.log` | 107 KB | 11.05.2026 16:52 |
| `candump_20260511_170445.log` | 23,6 KB | 11.05.2026 17:04 |
| `candump_20260511_172605.log` | 141 KB | 11.05.2026 17:26 |
| `candump_20260512_183113.log` | 1,59 MB | 12.05.2026 18:34 |
| `candump_20260512_184049.log` | 5,16 MB | 12.05.2026 18:52 |
| `candump_20260515_205850.log` | 198 B | 15.05.2026 21:03 |
| `candump_20260516_202703.log` | 844 B | 16.05.2026 20:27 |
| `candump_20260516_202837.log` | 3,4 KB | 16.05.2026 20:29 |
| `candump_20260516_193149.log` | 1,2 KB | 16.05.2026 20:32 |
| *(pozostałe ~10 plików)* | 0 B | — sesje bez ramek |

Największy plik to `candump_20260512_184049.log` (5,16 MB) z 12 maja.
