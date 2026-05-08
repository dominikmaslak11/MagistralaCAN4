# MagistralaCAN4 — CAN XL, CANopen, MQTT

Dokumentacja trzech najnowszych funkcji MagistralaCAN4: obsługi ramek CAN XL (do 2048 bajtów), analizatora protokołu CANopen (CiA 301) oraz mostka MQTT do IoT.

---

## CAN XL

CAN XL to najnowszy standard magistrali CAN, umożliwiający przesyłanie ramek o długości **do 2048 bajtów** (w porównaniu do 8 bajtów dla klasycznego CAN i 64 bajtów dla CAN FD). MagistralaCAN4 w pełni wspiera CAN XL — odczyt, zapis, wyświetlanie i nagrywanie.

### Identyfikacja wizualna

W kolumnie **FD** tabeli "Ruch CAN" ramki CAN XL są oznaczone czerwonym tekstem **`XL`**:

| Typ | Kolor | Etykieta |
|-----|-------|----------|
| CAN Classic | Szary | `CAN` |
| CAN FD | Zielony | `FD` |
| CAN XL | Czerwony | `XL` |

### Wyświetlanie danych

Dla ramek CAN XL tabela pokazuje pierwsze **16 bajtów** danych, a po nich informację o pełnej długości:

```
A1 B2 C3 D4 E5 F6 07 08 09 0A 0B 0C 0D 0E 0F 10 ... (2048 bajtów)
```

Najechanie kursorem na komórkę z danymi pokazuje tooltip z **pierwszymi 256 bajtami**.

### Struktura CanFrame (rozszerzona)

```cpp
struct CanFrame {
    uint32_t id;                         // CAN ID
    bool     fd  = false;                // CAN FD
    bool     xl  = false;                // CAN XL ← NOWE
    uint8_t  sdt = 0;                    // SDU Type (CAN XL) ← NOWE
    uint32_t af  = 0;                    // Acceptance Field ← NOWE
    uint8_t  dlc = 0;                    // 0–8 classic, 0–64 FD, 0–2047 XL
    std::array<uint8_t, 2048> data{};    // max 2048 bajtów ← ZMIENIONE
    uint64_t timestamp;
};
```

### Nagrywanie CAN XL

Format `.mcan` automatycznie obsługuje ramki XL — DLC jest zapisywane jako `uint16`, a flaga `0x10` oznacza ramkę XL.

### Wymagania systemowe

- Linux kernel **6.4+** z obsługą CAN XL
- Magistrala uruchomiona w trybie CAN XL: `ip link set can0 type can xl on`

---

## CANopen (CiA 301)

CANopen to protokół warstwy aplikacyjnej dla automatyki przemysłowej, standardowy w napędach, czujnikach i sterownikach PLC. Działa na 11-bitowych identyfikatorach CAN.

### Zakładka "CANopen" — 7 kolumn

| Kolumna | Opis |
|---------|------|
| Czas (s) | Timestamp ramki |
| CAN ID | Hex ID ramki |
| Węzeł | Node ID (1–127) |
| Funkcja | Function Code (4-bit) |
| Typ | Typ ramki (NMT, SYNC, EMCY, PDO, SDO, Heartbeat) |
| Szczegóły | Zdekodowana komenda/stan |
| Dane | Surowe dane hex |

### Typy ramek CANopen

| Typ | CAN ID | Opis |
|-----|--------|------|
| **NMT** | 0x000 | Network Management — sterowanie stanem węzłów |
| **SYNC** | 0x080 | Synchronizacja cykliczna |
| **EMCY** | 0x081–0x0FF | Emergency — komunikaty awaryjne |
| **PDO1 TX** | 0x181–0x1FF | Process Data Objects — dane procesowe |
| **PDO1 RX** | 0x201–0x27F | PDO odbierane |
| **SDO TX** | 0x581–0x5FF | Service Data Objects — konfiguracja |
| **SDO RX** | 0x601–0x67F | SDO żądania |
| **Heartbeat** | 0x701–0x77F | Sygnał życia węzła |

### Zdekodowane szczegóły

**NMT:** `Start Remote Node`, `Stop Remote Node`, `Enter Pre-Operational`, `Reset Node`, `Reset Communication`

**EMCY:** `Generic Error (0x1000)`, `Device Hardware (0x5000)`, `Communication (0x8100)`, `PDO Length Exceeded (0x8210)`

**SDO:** `Initiate Download`, `Download Segment`, `Initiate Upload`, `Upload Segment`, `Abort Transfer`

**Heartbeat:** `Boot-up`, `Stopped`, `Operational`, `Pre-operational`

### Przykład użycia

```bash
# Generuj ramki CANopen testowe (cangen z odpowiednimi ID)
cangen vcan0 -I 0x701 -L 1 -D 05 -g 100   # Heartbeat: Operational
cangen vcan0 -I 0x000 -L 2 -D 01 00 -g 1000 # NMT: Start
```

W zakładce CANopen zobaczysz zdekodowane ramki z informacją o stanie węzła.

---

## MQTT — mostek IoT

MQTT (Message Queuing Telemetry Transport) to lekki protokół publikacji/subskrypcji dla Internetu Rzeczy. MagistralaCAN4 może publikować zdekodowane sygnały DBC do brokera MQTT, umożliwiając integrację z chmurą.

### Jak to działa

```
MagistralaCAN4 → DbcParser → MqttBridge → mosquitto_pub → Broker MQTT
                                                              │
                                              subskrybenci: Node-RED, Grafana,
                                              aplikacje mobilne, dashboardy web
```

### Format publikacji

Każdy sygnał DBC jest publikowany jako osobny temat:

```
magistrala/EngineSpeed → {"value": 2100.0, "unit": "rpm", "timestamp": 1715123456789}
magistrala/CoolantTemp → {"value": 85.5, "unit": "degC", "timestamp": 1715123457012}
```

### Użycie

1. **Zainstaluj brokera MQTT:**
   ```bash
   sudo apt install mosquitto mosquitto-clients
   ```

2. **W MagistralaCAN4:**
   - Wczytaj plik DBC (`🗄️ Wczytaj DBC`)
   - Kliknij przycisk **📡 MQTT** w toolbarze
   - Uruchom sniffing (▶ Start)

3. **Subskrybuj na innym komputerze:**
   ```bash
   mosquitto_sub -h 192.168.1.10 -t "magistrala/#" -v
   ```

4. **W Node-RED / Grafanie:**
   - Skonfiguruj broker MQTT → `192.168.1.10:1883`
   - Subskrybuj `magistrala/#` — wszystkie sygnały DBC pojawią się jako dane

### Ograniczenie duplikatów

MqttBridge nie publikuje tej samej wartości dwa razy z rzędu (próg 0.001), co oszczędza pasmo przy stałych sygnałach.
