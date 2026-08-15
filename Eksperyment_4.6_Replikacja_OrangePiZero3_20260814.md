# Eksperyment 4.6 — Replikacja Eksperymentu 4.5 (Faza 1) na Orange Pi Zero 3

Data: 2026-08-14
Autorzy: Dominik Maślak (prowadzenie), Claude (asystent, implementacja i analiza)
Status: **ZAKOŃCZONY** — Faza 1 (klasyfikator ciągły) odtworzona na drugiej,
niezależnej platformie sprzętowej z identycznym wynikiem; Faza 5 (embeddingi
neuronowe) po raz pierwszy uruchomiona na sprzęcie docelowym, z wynikiem
zgodnym 1:1 z przebiegiem na laptopie.

---

## 1. Cel

`Analiza_ESP32_vs_RaspberryPi_OrangePi_20260727.txt` (sekcja 6) i
`Eksperyment_4.5_Uzasadnienie_Zakupu_RaspberryPi_20260806.md` (sekcja 3)
stawiały Orange Pi jako **odrzuconą alternatywę** dla Raspberry Pi — z powodu
gorszego wsparcia peryferiów i mniejszej odtwarzalności (Armbian utrzymywany
społecznościowo zamiast oficjalnego Raspberry Pi OS).

Ten eksperyment weryfikuje dwie rzeczy naraz:

1. **Czy wniosek Eksperymentu 4.5 jest własnością METODY, czy konkretnej płytki?**
   Jeśli ten sam klasyfikator na innym SoC (Allwinner H618 zamiast Broadcom),
   innej dystrybucji (Armbian/Debian 13 zamiast Raspberry Pi OS) i innej
   architekturze (aarch64/ARMv8 zamiast ARMv6) daje **ten sam wynik liczbowy**,
   to mocny argument, że mierzymy metodę, a nie sprzęt.
2. **Ile realnie kosztuje to gorsze wsparcie peryferiów** — czy obawa z analizy
   z 27.07 była uzasadniona, i jak konkretnie się objawiła.

---

## 2. Platforma

| | Raspberry Pi Zero W (Eksp. 4.5) | Orange Pi Zero 3 (ten eksperyment) |
|---|---|---|
| SoC | Broadcom BCM2835, 1× ARM11 | Allwinner H618, 4× Cortex-A53 |
| Architektura | ARMv6 (32-bit) | ARMv8 / aarch64 (64-bit) |
| RAM | 512 MB | 2 GB |
| System | Raspberry Pi OS Bookworm Legacy 32-bit | Armbian, Debian 13 Trixie, minimal/CLI |
| Jądro | (wg 4.5) | 6.18.43-current-sunxi64 |
| Kontroler CAN | MCP2515, kwarc 16 MHz (HAT Waveshare) | **ten sam HAT, przełożony** |
| Magistrala SPI | SPI0 (`spi0.0`), overlay gotowy w systemie | **SPI1 (`spi1.1`), overlay napisany od zera** |

Ruch CAN generowany z laptopa przez **PEAK PCAN-USB**, wspólna magistrala
250 kbit/s, ten sam generator (`esp_experiment_4_3/generate_traffic_diverse.py`),
to samo ziarno **seed=999**, `--n-configs 20`.

---

## 3. Koszt integracji sprzętowej — obawa z analizy 27.07 potwierdzona

Na Raspberry Pi uruchomienie MCP2515 to **jedna linijka** w `/boot/config.txt`
(`dtoverlay=mcp2515-can0,oscillator=16000000,interrupt=25`). Na Orange Pi Zero 3
trzeba było **napisać własny overlay device-tree**. Trzy konkretne pułapki:

1. **Na złączu 26-pin wyprowadzony jest CS1, nie CS0.** Płytka ma `PH5` jako CS0,
   ale ten pin siedzi na pozycji 3 jako linia I2C. Na złącze trafia wyłącznie
   `PH9` = CS1 (pin 24). W overlayu musi więc być `reg = <1>`, a urządzenie
   pojawia się jako `spidev1.1`. Wpisanie `reg = <0>` daje obraz "wszystko
   wygląda dobrze, a CAN nie wstaje".
2. **Standardowe overlaye Armbiana są niekompletne.** Bazowy węzeł
   `spi@5011000` w `sun50i-h618-orangepi-zero3.dtb` **nie ma żadnego `pinctrl`**,
   a dostarczone `sun50i-h616-spidev*.dtbo` też go nie dodają — włączają
   kontroler, ale nie multipleksują pinów. Własny overlay musi jawnie podać
   `pinctrl-0 = <&spi1_pins>, <&spi1_cs1_pin>`.
3. **SPI0 jest zajęty przez wbudowaną pamięć flash** (`jedec,spi-nor`) i nie ma
   go na złączu — odwrotnie niż na Raspberry Pi, gdzie HAT-y siedzą na SPI0.

Do tego dochodzi sprzeczność w dokumentacji publicznej: tabele pinoutu w sieci
podają **różne** pozycje fizyczne dla SPI (dokumentacja MainsailOS wskazuje
piny 21-26, źródła "RPi-compatible" — piny 19-24). Rozstrzygnięto oficjalnym
manualem producenta (*OrangePi Zero3 H618 User Manual v1.1*, str. 123) i
weryfikacją krzyżową z device tree działającej płytki. **Rację miał układ
zgodny z Raspberry Pi.**

Mapowanie ostateczne (zweryfikowane dwoma źródłami):

| Pin | GPIO | Funkcja | Linia `gpiochip1` |
|---|---|---|---|
| 19 | PH7 | SPI1_MOSI | 231 |
| 21 | PH8 | SPI1_MISO | 232 |
| 22 | PC7 | INT (przerwanie) | 71 |
| 23 | PH6 | SPI1_CLK | 230 |
| 24 | PH9 | SPI1_CS (**CS1**) | 233 |

Linia **INT jest obowiązkowa** — sterownik `mcp251x` nie ma trybu odpytywania.

**Wniosek**: obawa z `Analiza_ESP32_vs_RaspberryPi_OrangePi_20260727.txt` była
**uzasadniona co do istoty** (wsparcie peryferiów jest realnie gorsze, HAT-y
projektowane pod Raspberry Pi nie są plug-and-play), ale **koszt okazał się
jednorazowy i ograniczony** — jeden plik `.dts` (~50 linii) rozwiązuje problem
na stałe. Nie jest to bariera uniemożliwiająca użycie tej platformy.

---

## 4. Weryfikacja warstwy sprzętowej przed pomiarem

Zanim uruchomiono sterownik CAN, napisano **skaner SPI**
(`/root/can-setup/scan_mcp2515.py` na płytce) sprawdzający sam układ zgodnie
z dokumentacją Microchip (rozdz. 12, SPI Instruction Set):

- reset (0xC0) → odczyt CANSTAT (0x0E) = **0x80** (tryb Configuration) ✅
- odczyt CANCTRL (0x0F) = **0x87** (wartość domyślna po resecie) ✅
- zapis/odczyt wzorców 0x55 i 0xAA na CNF1 (0x2A) — bezbłędny ✅

To rozdziela dwie klasy błędów, które inaczej wyglądają identycznie: złe
okablowanie SPI od złej konfiguracji sterownika.

**Kwarc 16 MHz potwierdzony pośrednio**: sterownik ustawił `tq = 250 ns` przy
`brp = 2`, co przy zależności TQ = 2·BRP/F_osc wychodzi wyłącznie dla 16 MHz.
Przez SPI częstotliwości kwarcu odczytać się nie da.

**Test dwukierunkowy z PEAK PCAN-USB**: 6 ramek w obie strony, 0 błędów,
`berr-counter tx 0 rx 0`, stan ERROR-ACTIVE po obu stronach.

---

## 5. Wynik Fazy 1 — klasyfikator ciągły

Przebieg: **240 s, 111 587 ramek**, 160 śledzonych pozycji (CAN ID, bajt),
**0 ramek zgubionych** (`dropped 0, missed 0`), obciążenie CPU ~13% jednego
rdzenia. Skrypt `pi_continuous_observer.py` przeniesiony **bez zmiany ani jednej
linii** z Eksperymentu 4.5.

Ground truth (seed=999, n=20): **20 bajtów** faktycznie zawierających flagi bitowe.

| Metryka | Orange Pi Zero 3 | Raspberry Pi Zero W (Eksp. 4.5) |
|---|---|---|
| Recall | **85,0 %** (17/20) | 85 % (17/20) |
| Precision | **100 %** (0 FP) | 100 % (0 FP) |
| F1 | 91,9 % | — |

**Wynik identyczny co do liczby.** Przeoczone bajty: `0x350:5`, `0x390:1`, `0x3C0:2`.

### 5.1. Weryfikacja głębsza — ten sam zbiór, nie tylko ta sama liczba

Zapisany snapshot z Raspberry Pi (`esp_experiment_4_5_rpi/snapshots.jsonl`)
pochodzi **sprzed strojenia progu** i wykrywa 12 bajtów. Przeliczono surowy
stan z Orange Pi przy tym samym progu 0,5:

- Orange Pi przy progu 0,5: **również 12 bajtów**
- Zbiory pokrywają się w **11 z 12** pozycji (różnica: RPi `0x430:0`,
  OPi `0x420:1` — pojedynczy przypadek graniczny)

Oba przebiegi używały **identycznego korpusu** (te same 20 CAN ID, zgodne
z ground truth) — sprawdzone programowo.

### 5.2. Uczciwe zastrzeżenie — próg 0,46 NIE odtworzył się dokładnie

`Eksperyment_4.5_Strojenie_Progu_Klasyfikatora_20260808.md` lokalizuje "urwisko"
dokładnie między 0,46 a 0,47 (przy ≤0,46 recall 85 %). Na danych z Orange Pi:

| Próg | Wykryte | Recall | Precision |
|---|---|---|---|
| 0,50 | 12 | 60,0 % | 100 % |
| 0,47 | 13 | 65,0 % | 100 % |
| 0,46 | 14 | 70,0 % | 100 % |
| 0,40 | 17 | **85,0 %** | 100 % |
| 0,30 | 17 | 85,0 % | 100 % |
| 0,20 | 17 | 85,0 % | 100 % |

Pełne 85 % osiągane jest dopiero przy **≤0,40**, nie przy ≤0,46. Najbardziej
prawdopodobne wyjaśnienie: przebieg trwał 240 s (111 tys. ramek) wobec 1 h
(1,63 mln ramek) w oryginale — estymaty `big_jumps/changed_pairs` dla bajtów
granicznych nie zdążyły się jeszcze ustabilizować.

**To nie podważa decyzji o progu, tylko ją wzmacnia**: wybrana wartość **0,3**
leży bezpiecznie poniżej urwiska na OBU platformach i przy OBU długościach
przebiegu. Margines bezpieczeństwa, o który chodziło przy strojeniu, okazał się
potrzebny w praktyce, nie tylko teoretycznie.

Weryfikację dokładnej pozycji urwiska przy pełnej, godzinnej próbie należy
uznać za **zadanie otwarte**.

---

## 5b. Faza 5 (embeddingi neuronowe) — PIERWSZE uruchomienie na sprzęcie docelowym

`Eksperyment_4.5_Faza5_Embeddingi_Neuronowe_20260808.md` zawierał zastrzeżenie:
eksperyment jest **wyłącznie badawczy/offline (laptop), NIE jest wdrażalny na
Raspberry Pi Zero W** — PyTorch/ONNX Runtime/TFLite nie mają żadnych pakietów
dla ARMv6, nawet przez piwheels. Wymagałoby to sprzętu ARMv7/aarch64.

**Orange Pi Zero 3 to zastrzeżenie usuwa.** `neural_embedding_warmstart.py`
uruchomiono bez zmian na płytce (`torch 2.13.0+cpu`, `sentence-transformers
5.7.0`, model `all-MiniLM-L6-v2`, 384 wymiary).

### Wynik — zgodność 1:1 z przebiegiem na laptopie

Ta sama biblioteka (`esp_experiment_4_4_qdrant/corpus_diverse.json`, 40 CAN ID,
152 instancje) i ten sam korpus testowy (seed=999, 83 instancje):

| Test | Metryka | Orange Pi Zero 3 | Laptop x86 (2026-08-08) |
|---|---|---|---|
| A (within, N=152) | cechy ręczne 7-dim | 89,5 % / 91,4 % | 89,5 % / 91,4 % |
| A (within, N=152) | embedding 384-dim | 98,7 % / **100 %** | 98,7 % / 100 % |
| B (cross, N=83) | cechy ręczne 7-dim | 92,8 % / 94,0 % | 92,8 % / 94,0 % |
| B (cross, N=83) | embedding 384-dim | **100 % / 100 %** | 100 % / 100 % |

(format: trafność 3-way / trafność 2-way)

**Wyniki identyczne co do ostatniej cyfry.** Zmiana architektury z x86-64 na
aarch64 nie wpłynęła na wynik numeryczny.

### Wydajność na sprzęcie docelowym

| Etap | Czas |
|---|---|
| załadowanie modelu MiniLM | ~51 s (pierwsze uruchomienie, z pobraniem) |
| embedowanie 235 instancji | ~101 s |
| **całość** | **2 min 26 s** |

Czas CPU 6 min 47 s przy 2 min 26 s czasu rzeczywistego — obliczenia realnie
rozłożyły się na cztery rdzenie. Zużycie pamięci ~5 % z 2 GB. Na tej płytce
jest to więc zadanie **wykonalne rutynowo**, nie graniczne.

### Pułapka instalacyjna (istotna przy odtwarzaniu)

Domyślny wheel `torch` dla aarch64 z PyPI **ciągnie zależności CUDA**
(`nvidia_cudnn_cu13`, ~445 MB) — ekosystem zakłada, że aarch64 + Linux to Jetson
lub serwer z GPU NVIDIA. Na tej płytce są bezużyteczne, a pobieranie potrafi
przerwać instalację. Konieczne jest jawne wskazanie repozytorium CPU:

```
pip install torch --index-url https://download.pytorch.org/whl/cpu
```

Potwierdza to wcześniejsze ustalenie, że **GPU Mali w H618 nie ma żadnego
zastosowania** w tym projekcie: inferencja idzie w całości przez CPU
(`torch.cuda.is_available() == False`), a mimo to jest w pełni wydajna.

---

## 5c. Przebieg godzinny — domknięcie pytania o próg i NOWE ustalenie

Powtórzono przebieg na pełnej, godzinnej próbie (dla porównania 1:1 z 4.5),
z **Fazą 1 i Fazą 2 działającymi równolegle** — tak jak w oryginale.

Wynik surowy: **1 685 554 ramek w 3600 s** (~468 ramek/s), **0 zgubionych,
0 pominiętych**. Recall 85,0 % (17/20), Precision 100 % — bez zmian wobec
przebiegu 240-sekundowego.

### 5c.1. Gdzie naprawdę leży „urwisko" progu

Zamiast zgadywać z siatki progów, policzono **dokładne wartości współczynnika
`big_jumps/changed_pairs`** dla wszystkich 20 prawdziwych flag. Najniższa
wartość wśród bajtów akceptowalnych przez regułę liczby bitów to **0,4595**
(CAN ID 0x400, bajt 5). Urwisko leży więc dokładnie tam — między 0,45 a 0,46.

| Przebieg | Położenie urwiska |
|---|---|
| RPi Zero W, 1 h (dokument strojenia) | 0,46 → 0,47 |
| Orange Pi, 240 s | ~0,40 |
| **Orange Pi, 1 h** | **0,4595** |

Przy 15× dłuższej próbie urwisko przesunęło się z ~0,40 na 0,4595 — czyli
**w stronę wartości udokumentowanej i o jeden krok siatki od niej**. To
potwierdza hipotezę postawioną przy krótkim przebiegu: rozbieżność wynikała
z **niedokonwergowanych estymat**, nie z platformy sprzętowej. Pozostała
różnica (0,4595 vs ~0,46-0,47) to wahanie pojedynczego, granicznego sygnału
między realizacjami losowymi — nie efekt systematyczny.

Wybrany próg **0,3** pozostaje bezpieczny z zapasem na obu platformach.

### 5c.2. USTALENIE KLUCZOWE — to nie próg ogranicza recall, tylko reguła liczby bitów

Analiza współczynników ujawniła rzecz, której nie widać z samych metryk:
**3 przeoczone flagi nie są przeoczone z powodu progu.** Ich współczynniki
wynoszą 0,9676 / 0,8421 / 0,8160 — przeszłyby *każdy* próg. Odrzuca je
warunek `2 <= bit_count <= 6`, bo mają **7-8** niezależnie przełączających
się bitów:

| CAN ID | bajt | bitów | współczynnik | powód odrzucenia |
|---|---|---|---|---|
| 0x350 | 5 | 8 | 0,9676 | liczba bitów > 6 |
| 0x3C0 | 2 | 7 | 0,8421 | liczba bitów > 6 |
| 0x390 | 1 | 7 | 0,8160 | liczba bitów > 6 |

Innymi słowy: **recall jest zaklinowany na 85 % przez konstrukcję
klasyfikatora, nie przez strojenie.** Obniżanie progu do zera niczego nie da.

### 5c.3. Konsekwencja praktyczna — górny limit 7 bitów jest lepszy niż 6

Sprawdzono wpływ samego górnego limitu liczby bitów (próg stały 0,3):

| max bitów | wykryte | TP | FP | Recall | Precision | F1 |
|---|---|---|---|---|---|---|
| **6** (obecne) | 17 | 17 | 0 | 85,0 % | 100 % | 91,9 % |
| **7** | 20 | 19 | 1 | **95,0 %** | 95,0 % | **95,0 %** |
| 8 | 39 | 20 | 19 | 100 % | 51,3 % | 67,8 % |

Podniesienie limitu do **7 bitów** kupuje +10 pp recall kosztem 5 pp precyzji —
netto **+3,1 pp F1**. Limit 8 rujnuje precyzję (51,3 %), i jest to zachowanie
**fizycznie oczekiwane**: bajt, w którym wszystkie osiem bitów przełącza się
niezależnie, jest nieodróżnialny od szybko zmieniającego się skalara. Pierwotne
ograniczenie było więc słuszne co do idei, ale ustawione o jeden krok za ciasno.

**Rekomendacja do rozważenia**: zmiana górnego limitu z 6 na 7 w
`etap_b_autolabel.py`, `DecodingAccuracyRunner.cpp` i `pi_continuous_observer.py`.
Wymaga potwierdzenia na drugim, niezależnym korpusie (inny seed) przed wdrożeniem —
tutaj zmierzono to na jednym.

### 5c.4. Faza 2 — embedded Qdrant na żywo, równolegle z Fazą 1

Demon `pi_qdrant_warmstart_live.py` działał przez całą godzinę równolegle
z klasyfikatorem, budując bibliotekę wektorów z tego samego, żywego ruchu.
11 ewaluacji leave-one-out w czasie, wynik końcowy:

| Metryka | Orange Pi Zero 3 (1 h) | RPi Zero W (Eksp. 4.5) |
|---|---|---|
| trafność 3-way | **94,0 %** | 91,6 % |
| trafność 2-way | **96,4 %** | 91,6 % |
| N | 83 | 83 |

Wynik nieco **lepszy** niż na Raspberry Pi. Nie należy tego nadinterpretować —
to pojedynczy przebieg, a różnica mieści się w wahaniach między realizacjami
(w trakcie godziny wartość 3-way oscylowała między 91,6 % a 95,2 %). Istotne
jest co innego: **obie fazy działały jednocześnie, a płytka nie zgubiła ani
jednej ramki** — czego jednordzeniowy Pi Zero W nie miał zapasu udźwignąć
z takim marginesem.

### 5c.5. Uczciwa uwaga o czystości przebiegu

W trakcie przebiegu licznik `can0` zarejestrował **1 błąd RX** (przy 1,83 mln
ramek; `dropped 0`, `missed 0`). Przyczyna jest znana i leży po stronie
prowadzącego pomiar: w trakcie trwającego eksperymentu wykonano
`tailscale up --reset`, co chwilowo zresetowało konfigurację sieciową płytki.
Sama instalacja pakietu (z obniżonym priorytetem CPU/IO) przeszła bez śladu —
licznik przed i po wynosił 0. Błąd nie narastał przez kolejne dwie godziny.

Metryka kluczowa dla eksperymentu (`dropped`) pozostała zerowa, więc wynik
uznaje się za ważny, ale **przebieg nie był w 100 % nieskażony** i tak jest tu
odnotowany. Wniosek na przyszłość: nie wykonywać operacji resetujących sieć
na urządzeniu prowadzącym pomiar.

---

## 6. Wnioski

1. **Wynik Eksperymentu 4.5 jest własnością metody, nie platformy.** Ten sam
   kod, na innym SoC, innej architekturze (aarch64 vs ARMv6), innym systemie
   i innej magistrali SPI, dał **identyczny wynik liczbowy** (85 % / 100 %) i
   niemal identyczny zbiór wykrytych bajtów (11/12 przy wspólnym progu). To
   znacząco wzmacnia wiarygodność wniosków z 4.5.
2. **Orange Pi Zero 3 jest w pełni użyteczny dla tego zastosowania** — po
   jednorazowym koszcie napisania overlaya. 0 zgubionych ramek przy 465 ramek/s
   i 13 % obciążenia jednego z czterech rdzeni to duży zapas wydajności wobec
   jednordzeniowego Pi Zero W.
3. **Obawa o odtwarzalność (analiza 27.07, sekcja 6) potwierdziła się w innym
   miejscu, niż zakładano.** Problemem nie okazał się Armbian jako dystrybucja
   (działa stabilnie), tylko **sprzeczna dokumentacja publiczna pinoutu** i
   **niekompletne overlaye producenta**. Zabezpieczeniem okazała się weryfikacja
   krzyżowa: oficjalny manual + odczyt z device tree działającego sprzętu.
4. **Replikacja przyniosła ustalenie, którego oryginalny eksperyment nie
   pokazał**: sufit recall na poziomie 85 % wynika z reguły `bit_count <= 6`,
   a nie ze strojenia progu (sekcja 5c.2). Podniesienie limitu do 7 daje
   F1 95,0 % wobec obecnych 91,9 %. To najbardziej praktyczny wynik tej sesji
   i jedyny, który zmienia rekomendację dotyczącą samego algorytmu — a wyszedł
   na jaw dopiero przy powtórzeniu pomiaru na drugiej platformie.
   **Wymaga potwierdzenia na drugim korpusie przed wdrożeniem.**

---

## 7. Pliki

Na płytce (`10.42.0.132`, konta `root`/`dominik`, dostęp kluczem SSH):

- `/root/can-setup/mcp2515-can0.dts` — overlay (skomentowany), skompilowany do
  `/boot/overlay-user/mcp2515-can0.dtbo`, aktywny przez
  `user_overlays=mcp2515-can0` w `/boot/armbianEnv.txt`
- `/root/can-setup/scan_mcp2515.py` — skaner SPI wykrywający MCP2515
- `/etc/systemd/system/can0.service` — `can0` @250 kbit/s, trwałe po restarcie
- `/root/exp45/` — `pi_continuous_observer.py`, `observer_state.json`,
  `snapshots.jsonl` z tego przebiegu

Pinout złącza (zweryfikowany krzyżowo, wersja graficzna):
https://claude.ai/code/artifact/7b4770df-da2b-4b84-93ce-f8ead80cc76d

---

## 8. Zadania otwarte

1. Pełny, godzinny przebieg na Orange Pi dla bezpośredniego porównania
   1:1 z 4.5 (w tym weryfikacja dokładnej pozycji urwiska progu).
2. Faza 2 (embedded Qdrant budowany na żywo) — przeniesienie
   `pi_qdrant_warmstart_live.py`. Środowisko jest już gotowe
   (`qdrant-client` zainstalowany w `/root/venv45`).
3. Faza 3/4 (ewaluacja LLM, override ciągły) — wymaga wywołań płatnych API,
   **świadomie nie uruchamiana** w ramach tej sesji.
4. Rozważyć wykorzystanie faktu, że Faza 5 działa teraz na sprzęcie docelowym:
   embedding neuronowy dawał 100 % trafności cross-corpus wobec 92,8 % cech
   ręcznych — czyli rozwiązuje problem generalizacji z Eksperymentu 4.4
   (~70 % cross-corpus). Do tej pory była to możliwość czysto teoretyczna,
   bo brakowało platformy, na której dałoby się to wdrożyć.
