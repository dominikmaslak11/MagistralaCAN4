# Eksperyment 1.2 — stan sesji na 2026-07-25 wieczorem (do kontynuacji jutro)

## Skrót sytuacji

Cel: zmierzyć realnie (oscyloskop/analizator) Hot Execution Latency zgodnie z metodyką
`Pomiary dla CAN-Edge AI.md` (Eksperyment 1.2, N=1000, Kanał 1 = zdarzenie nadejścia
ramki CAN, Kanał 2 = reakcja GPIO ESP32).

Dane zapasowe (już gotowe, ważne, można ich użyć jako finalny wynik jeśli sprzęt
zawiedzie): `Eksperyment_1.2_Hot_Execution_20260725_163438/` — metoda wewnętrznego
timera ESP32 (`esp_timer_get_time()`), N=1000, mean=109.7us, stdev=1.52us, max=115us.

## Wątek 1: ATK-Logic (Alientek DL16 clone, USB 1a86:ffcc) — ZAMKNIĘTY, sprzęt niestabilny

- Sterownik `esp_experiment_1_2/atk_logic_driver.py` NAPRAWIONY i zweryfikowany
  bajt-w-bajt względem realnej działającej sesji oficjalnej appki (usbmon/tshark):
  - brakujący terminator `0x0B` w komendach `_send_to_mcu` (GetMCUVersion, SetResetState)
  - błędny format payloadu `SimpleTrigger` (dodano `arm_trigger_instant_all()`)
  - `close()` teraz usypia FPGA (`sleep_fpga()`) przed zwolnieniem interfejsu — inaczej
    dioda LED zostawała zielona na zawsze (potwierdzone fizycznie z użytkownikiem)
- MIMO poprawnego protokołu urządzenie samo w sobie odpowiada **sporadycznie i losowo**
  (potwierdzone też na oficjalnej aplikacji producenta — ten sam objaw, ~1 udana sesja
  na kilka prób). To niestabilność firmware/FPGA po aktualizacji firmware przez
  użytkownika, nie da się tego naprawić po stronie hosta. Przetestowano wyczerpująco:
  świeży replug + natychmiastowa próba, ciągłe próbowanie 20s, pojedynczy i pulowany (6x)
  odczyt zakolejkowany przed zapisem — zawsze albo cisza, albo (rzadko) sukces.
- Rekomendacja: kontakt z supportem Alientek, albo traktować jako martwy trop.

## Wątek 2: Hantek 1008C (USB 0783:5725) — W TOKU, prawdopodobnie do naprawienia szybko

- To PRAWDZIWY Hantek 1008C (VID:PID zweryfikowany na sigrok.org/wiki/Hantek_1008C,
  `lsusb` pokazuje mylącą nazwę "C3PO YDJ-2088" — to tylko kolizja w bazie usb.ids,
  nie błąd/inne urządzenie).
- Użyto gotowego open-source sterownika: https://github.com/mfg92/hantek1008py
  (Apache-2.0) — skopiowany i lekko załatany do
  `esp_experiment_1_2/hantek1008_driver/hantek1008.py`:
  - 4 asercje w `_init3()` sprawdzające zahardkodowane bajty kalibracji ADC z
    KONKRETNEGO egzemplarza urządzenia autora zamieniono na `log.debug` (naturalnie
    różnią się między sztukami sprzętu, autor sam to zaznaczył w README).
- Sterownik działa STABILNIE i przewidywalnie (w przeciwieństwie do ATK-Logic) —
  za każdym razem odpowiada, żadnych losowych zawieszeń.
- Tryb "burst" + wyzwalacz sprzętowy: przy `ns_per_div=5000` (5us/div), 2 kanały
  aktywne → **2000 próbek/kanał = 400us okno, ~200ns/próbkę rozdzielczości**
  (zweryfikowane empirycznie: total_samples=2000 STAŁE niezależnie od ns_per_div,
  więc 80 divs total, sample_period_ns = ns_per_div/25). To z dużym zapasem
  wystarczy na zmierzony wcześniej t_resp ~110us (max 115us).
- WAŻNE: klasa wysokopoziomowa `Hantek1008` (z konwersją na wolty) NIE przyjmuje
  `trigger_channel/trigger_slope/trigger_level` w konstruktorze (nie przekazuje ich
  dalej do `Hantek1008Raw.__init__`) — do wyzwalacza trzeba używać **`Hantek1008Raw`**
  bezpośrednio (surowe wartości ADC 12-bit, nie wolty — ale do detekcji zbocza
  progiem względnym to bez znaczenia).
- Kanały 0-indeksowane w API: `channel=0` → opisane "CH1" na obudowie, `channel=1`
  → "CH2".

### Podłączenie fizyczne (uzgodnione, wg firmware `esp_experiment_1_2.ino`)
- Hantek CH1 (index 0) → ESP32 **GPIO4** (`MCP_INT_PIN`, przerwanie MCP2515,
  aktywne niskie/FALLING = moment nadejścia ramki CAN)
- Hantek CH2 (index 1) → ESP32 **GPIO13** (`REACTION_PIN`, HIGH = reakcja wykryta)
- Wspólna masa GND

### Firmware ESP32
- `esp_experiment_1_2.ino` skompilowany (FQBN `esp32:esp32:esp32`, 23% flash) i
  wgrany na `/dev/ttyUSB0` — **świeży, aktualny stan**.
- MCP2515 bitrate: `CAN_250KBPS` (`mcp2515.setBitrate(CAN_250KBPS, MCP_16MHZ)`).

### CAN bus (generator ramek: PEAK PCAN-USB)
- `can0` podniesione: `bitrate 250000`, stan `ERROR-ACTIVE` (widzi ruch).
- **Po restarcie komputera trzeba podnieść ponownie:**
  ```
  sudo ip link set can0 type can bitrate 250000
  sudo ip link set can0 up
  ```
  (albo `pkexec sh -c '...'` jeśli wolisz bez interaktywnego sudo)

### STAN NIEROZWIĄZANY — do zrobienia jutro jako pierwsze
`calibrate_hantek.py` (w `esp_experiment_1_2/`) uruchomiony z realnym podłączeniem —
**wynik: oba kanały pokazują tylko szum ADC (~5-6 zliczeń wokół ~2020-2026 z 4096),
NIE prawdziwy skok logiczny 0→3.3V.** To wygląda na problem z fizycznym kontaktem
sond albo brakiem wspólnej masy, NIE na błąd w kodzie. Do sprawdzenia jutro:
1. Czy sondy Hantek faktycznie stykają się z GPIO4 i GPIO13 (dobry kontakt, nie
   "prawie dotyka")?
2. Czy masa (GND) sondy Hantek jest podłączona do GND ESP32?
3. Czy to na pewno pierwsze dwa wejścia (CH1/CH2) na obudowie Hanteka?

Po potwierdzeniu połączenia: uruchomić ponownie
`cd esp_experiment_1_2 && ./.venv/bin/python3 calibrate_hantek.py` — powinno pokazać
wyraźne zbocza (zakres ADC bliski 0-4095 zamiast wąskiego szumu) i sensowną deltę
rzędu ~100us między CH1(falling) a CH2(rising). Jeśli zadziała, następny krok to
napisanie `run_experiment_1_2_hantek.py` (pętla N=1000: wyślij ramkę CAN przez
`cansend can0 123#DEADBEEF11223344`, przechwyć burst, znajdź zbocza, zapisz
`raw_data.csv`/`statistics.csv`/`histogram.png`/`report.txt` — dokładnie jak
`run_experiment_1_2.py` dla wersji ESP32-timer).

## Nowe/zmienione pliki w tej sesji (NIC nie zacommitowane do git)
- `esp_experiment_1_2/atk_logic_driver.py` — naprawiony (patrz Wątek 1)
- `esp_experiment_1_2/calibrate.py` — zaktualizowany pod nowe API atk_logic_driver
- `esp_experiment_1_2/calibrate_hantek.py` — nowy, test kalibracyjny Hantek
- `esp_experiment_1_2/hantek1008_driver/` — nowy, skopiowany+załatany driver (Apache-2.0)
- `esp_experiment_1_2/.venv/` — nowe środowisko (pyusb, pyserial, matplotlib, numpy, overrides)
- `/etc/udev/rules.d/99-hantek1008.rules` — reguła 0666 dla 0783:5725 (system, poza repo)

## Uwaga bezpieczeństwa
Hasło sudo użytkownika pojawiło się w tej rozmowie w plaintext (użytkownik wkleił
je bezpośrednio). Nigdy nie zostało użyte w żadnej komendzie powłoki (unikano tego
celowo, używając `pkexec` do operacji wymagających roota) ani zapisane w żadnym
pliku/pamięci. Mimo to warto rozważyć rotację tego hasła, skoro trafiło do
transkryptu rozmowy.
