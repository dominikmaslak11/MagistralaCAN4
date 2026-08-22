# Podsumowanie sesji 2026-08-10 → 2026-08-21

Notatka robocza — nie formalny raport (te są osobno, lista na dole).
**Napisana tak, żeby dało się wznowić pracę w nowej sesji bez czytania całej
historii czatu.**

---

> **Stan na 22.08.2026:** sesja przerwana na aktualizację Claude Code.
> Od 21.08 nie wykonano żadnych prac merytorycznych — ten dokument jest
> **aktualny w całości**. Ostatni commit nadal `5b9e2ff`; **23 pozycje
> niescommitowane** (patrz „Stan repozytorium").

## SZYBKI START — jeśli zaczynasz nową sesję, przeczytaj to najpierw

**Stan sprzętu:** Orange Pi Zero 3 jest **WYŁĄCZONY** (`poweroff` 20.08, 13:10).
Żeby wrócić do pracy: podłącz zasilanie USB-C i kabel Ethernet do laptopa.

**Jak się połączyć:**
```bash
ssh root@orangepi-zero3      # przez Tailscale, działa z dowolnego miejsca
ssh root@10.42.0.132         # przez kabel bezpośredni
```

**Jeśli kabel nie działa** — połączenie współdzielone na laptopie mogło wygasnąć:
```bash
sudo nmcli con up "Połączenie przewodowe 1"
```

**Jeśli płytka ma nie mieć internetu** — reguły NAT giną po restarcie laptopa:
```bash
sudo iptables -t nat -A POSTROUTING -s 10.42.0.0/24 -o usb0 -j MASQUERADE
sudo iptables -I FORWARD 1 -i eth0 -o usb0 -j ACCEPT
sudo iptables -I FORWARD 2 -i usb0 -o eth0 -m state --state RELATED,ESTABLISHED -j ACCEPT
```
(`usb0` = internet z telefonu przez USB; przy Tailscale zwykle niepotrzebne)

**Co czeka na Twoją decyzję** — patrz sekcja „Otwarte decyzje" na dole.

---

## Część 1 (10-14.08): Orange Pi Zero 3 jako drugi węzeł

Instalacja od zera: Armbian Debian 13 Trixie, wariant minimal/CLI, aarch64.
Karta 16 GB, suma SHA256 zweryfikowana bit-w-bit po zapisie.

**Pułapki, które kosztowały czas:**

1. **Płytka nie bootowała** — okazało się, że wystarczyło docisnąć kartę microSD
   i poprawić zasilanie. Diagnostyka, która się sprawdziła: `tcpdump -i eth0` —
   **zero pakietów przy zestawionym linku 1 Gb/s oznacza, że system nie wstał**.
   Dioda na gnieździe RJ45 świeci od samego układu PHY i nic nie mówi o boocie.
2. **Armbian nie ma gotowego overlaya dla MCP2515.** Trzeba było napisać własny.
3. **Na złączu wychodzi CS1, nie CS0** — `PH5` (CS0) siedzi na pinie 3 jako I2C.
   W overlayu musi być `reg = <1>`, urządzenie to `spidev1.1`.
4. **Stockowe overlaye Armbiana są niekompletne** — bazowy węzeł `spi@5011000`
   nie ma ŻADNEGO `pinctrl`, dostarczone `spidev*.dtbo` też go nie dodają.
   Własny overlay musi jawnie dać `pinctrl-0 = <&spi1_pins>, <&spi1_cs1_pin>`.
5. **Publikowane w sieci tabele pinoutu są sprzeczne.** Rozstrzygnięto oficjalnym
   manualem (v1.1, str. 123) + odczytem z device tree działającej płytki.

**Zainstalowane i skonfigurowane na płytce:**
- `can0` @250 kbit/s, trwale przez `/etc/systemd/system/can0.service`
- overlay w `/boot/overlay-user/mcp2515-can0.dtbo`, włączony przez `user_overlays=`
- Tailscale z autostartem — `orangepi-zero3` = `100.88.189.126`
- środowisko ML w `/root/venv45` (torch 2.13.0+cpu, sentence-transformers)
- skrypty w `/root/proj/`, `/root/can-setup/`, `/root/exp45/`

**PUŁAPKA PyTorch na aarch64:** domyślny wheel z PyPI ciągnie zależności CUDA
(`nvidia_cudnn_cu13`, ~445 MB), bezużyteczne bez GPU NVIDIA — instalacja się
wywala. Trzeba `--index-url https://download.pytorch.org/whl/cpu`.

---

## Część 2 (14-15.08): Eksperymenty 4.6 – 4.10

**4.6 — replikacja Eksperymentu 4.5 na Orange Pi.** Wynik **identyczny co do
liczby**: Recall 85,0 %, Precision 100 %. Faza 5 (embeddingi neuronowe) po raz
pierwszy uruchomiona na sprzęcie docelowym — wyniki zgodne z laptopem x86.
Wniosek: **rezultat jest własnością metody, nie płytki**.

**4.7-4.10 — sieć neuronowa kontra reguła.** Na 2936 pozycjach ze sprzętu:
sieć F1 98,8 % wobec 91,0 % obecnej reguły. Sieć ma 577 parametrów, uczy się
68 s na płytce, działa **bez PyTorcha** (wagi w JSON, czysty Python).

**Trzy hipotezy odrzucone** (4.8, 4.9, 4.10): hipoteza o większej odporności
sieci **nie potwierdziła się**. Dwie pierwsze próby były błędem projektowym —
manipulowały parametrami poziomu magistrali, a statystyki zależą od stosunku
częstotliwości próbkowania do dynamiki sygnału.

**Wycofany wniosek:** na teście 132-pozycyjnym reguła wypadła lepiej niż sieć.
Przy próbie 22× większej kierunek się odwrócił — różnica opierała się na dwóch
pozycjach, czyli na szumie.

**Rekomendacja potwierdzona na dwóch korpusach:** zmiana limitu bitów
**6 → 7** daje +5,0 pp i +4,9 pp F1, zerowy koszt obliczeniowy.
**Jeszcze niewdrożona do kodu.**

---

## Część 3 (15-17.08): Eksperymenty 4.11 i 4.12 — maska bitowa

**Najważniejsza część merytoryczna tej sesji.**

Problem był źle rozumiany. Maska `seen0 & seen1` jest **idealna dla bajtów
z samymi flagami (100 %) i ma dokładnie ZERO trafień dla mieszanych** (~22 %
przypadków). Średnia 82 % ukrywała to całkowicie.

**Pierwszy wynik:** sieć per bit, 30/30 = 100 % na bajtach mieszanych.

**I tu rzecz kluczowa — wynik został OBALONY własnym testem (4.12).**
W korpusie flagi siedziały zawsze na najniższych bitach, a liczba tworzyła
ciągły zakres nad nimi. Po rozproszeniu bitów:

| Wariant | Maska w bajtach mieszanych |
|---|---|
| bity ciągłe, ten sam model | 100 % |
| **bity rozproszone, ten sam model** | **14 %** |
| bity rozproszone, model uczony na reprezentatywnych | **59 %** |

**Liczby obowiązujące: 0 % → 59 % dla maski, 26,8 % → 96,5 % dla precyzji.**
**NIE mówić 100 %.**

Generator rozszerzono o `--scatter-partial-bits` (domyślnie wyłączone, ground
truth dla starych ziaren pozostaje bit-w-bit identyczny — zweryfikowane).

---

## Część 4 (17-21.08): materiały i dokumentacja

Wykonane pod sprawozdanie dla wykładowcy oraz publikację:

- scenariusze rozmowy, infografiki, tło do wideorozmowy 1920×1080,
- **katalog wszystkich eksperymentów** (1.1 → 4.12),
- wyjaśnienie mechanizmu override,
- dokument o licencjach i prawach do publikacji,
- **`wyniki_eksperymentow/`** — 13 katalogów, 27 CSV, 14 opisów; każdy
  eksperyment z metodą, kryteriami walidacji, przykładowymi danymi i wynikiem,
- archiwum `wyniki_eksperymentow_20260820.zip` (68 KB).

**Ważna uwaga o rzetelności danych:** przy budowie CSV rozdzielono trzy
kategorie pochodzenia — kopia surowych pomiarów, przeliczenie z danych
źródłowych, przepis z raportu. Weryfikacja wykryła **realny błąd**: w porównaniu
platform zestawiono liczbę ramek z 4-minutowego przebiegu Orange Pi obok
godzinnego przebiegu Raspberry Pi. Poprawione.

**Czego NIE zweryfikowano ponownie:** wyników samej sieci neuronowej
(4.7-4.10 i 4.11-4.12). Model i dane testowe są na płytce; ponowne przeliczenie
wymaga jej uruchomienia. Oznaczone w opisach.

---

## Stan repozytorium

**Ostatni commit:** `5b9e2ff` (15.08) — eksperymenty 4.5-4.11, raporty,
infografiki, demon z siecią, `--period-scale` w generatorze.

**NIESCOMMITOWANE** (praca z 17-21.08):
- `Eksperyment_4.11-4.12_Maska_Bitowa_20260817.md` (przemianowany z 4.11)
- `Sprawozdanie_Calosc_Scenariusz_20260820.md`, `Sprawozdanie_Ustne_Scenariusz_20260817.md`
- `Katalog_Wszystkich_Eksperymentow_20260820.md`
- `Override_Hybrydowy_Wyjasnienie_20260820.md`
- `LICENCJE_I_PRAWA_DO_PUBLIKACJI.md`
- `wyniki_eksperymentow/` (cały katalog) + archiwum ZIP
- infografiki PDF/PNG, `components_datasheet/pinouty/`
- `--scatter-partial-bits` w `generate_traffic_diverse.py`
- `esp_experiment_4_11_maska/` (skrypty)

---

## Otwarte decyzje — czekają na Ciebie

| # | Decyzja | Kontekst |
|---|---|---|
| 1 | **Brak pliku `LICENSE`** | README deklaruje MIT, pliku nie ma → formalnie „wszelkie prawa zastrzeżone". Blokuje deklarację odtwarzalności w artykule. |
| 2 | **Wariant ESP32 do rysunku pinoutu** | Płytka 30-pinowa potwierdzona. Do sprawdzenia jeden pin: nadruk przy `RX2` (prawa strona, między `TX2` a `D4`) — rysunek producenta podaje GPIO34, co jest najpewniej błędem (powinno być GPIO16/D16). |
| 3 | **Wdrożyć „limit 6 → 7"** do `etap_b_autolabel.py`, `DecodingAccuracyRunner.cpp`, `pi_continuous_observer.py` | potwierdzone na dwóch korpusach, +5 pp F1, zero kosztu |
| 4 | **Czy sieć ma zastąpić regułę** jako domyślny klasyfikator | argument ZA: wyższa jakość. Argument PRZECIW: dodatkowy plik modelu. Hipoteza o odporności ODRZUCONA — nie używać jej jako uzasadnienia. |
| 5 | **Etap D (fine-tuning)** — odblokowany | klasyfikator-nauczyciel naprawiony; ruszać czy najpierw walidacja na maszynie? |
| 6 | **Eksperyment 3.1** — pomiar terenowy | czeka na odpowiedzi wykładowcy na 10 kwestii metodologicznych |
| 7 | **Commit + push** pracy z 17-21.08 | |

---

## Zadania otwarte (techniczne)

1. **Walidacja na prawdziwym pojeździe** — największe ograniczenie całej pracy.
   Cały ruch jest syntetyczny. Pierwszy krok: zrzut J1939 z maszyny rolniczej,
   sprawdzenie standardowych PGN-ów (65253 = motogodziny, 65257 = paliwo).
2. **Podniesienie 59 % maski** — czy pomoże większy korpus, dłuższa obserwacja,
   czy brakuje cech opisujących sprzężenie **par** bitów wprost?
3. **Wdrożenie kaskady** w demonie: sieć per bajt (4.7) → sieć per bit (4.11).
4. **Porównanie trzech metod** (reguła / retrieval Qdrant / sieć) na jednym
   korpusie — wielokrotnie odkładane.

---

## Wątek poboczny: integracja z AgroErpMobile

Ustalono podczas sesji (kontekst: `/home/nz2xzhkzfeewkgbu/AgroErpMobile/`,
plik `TELEMETRIA_CAN.md`):

- **Paliwo i motogodziny są standardowe** w J1939 (PGN 65253, 65257) — cała
  praca nad klasyfikatorem **nie jest do nich potrzebna**.
- **Klasyfikator jest właściwym narzędziem do jednego sygnału**: „narzędzie
  pracuje" (hedera opuszczona / młóci) — ten NIE jest standaryzowany, jest
  producencki i binarny, czyli **jest flagą bitową w nieznanej ramce**.
- **RTK (dokładność centymetrowa) jest potrzebne**, jeśli mapa pokrycia ma
  wykrywać pominięte pasy. ASG-EUPOS jest **bezpłatny** od 10.2022.
- **Antena RTK musi być na dachu maszyny**, nie w kabinie — to przesądza,
  że odbiornik siedzi w skrzynce przy CAN, a nie w telefonie.
- Telefon może **przekazywać poprawki NTRIP** do płytki przez BLE/WiFi —
  unika drugiej karty SIM, zgodnie z zasadą z `TELEMETRIA_CAN.md`.

---

## Lista dokumentów z tej sesji

| Plik | Zawartość |
|---|---|
| `Eksperyment_4.6_Replikacja_OrangePiZero3_20260814.md` | replikacja na drugiej platformie |
| `Eksperyment_4.7-4.10_Siec_Neuronowa_vs_Regula_20260815.md` | sieć kontra reguła, 3 odrzucone hipotezy |
| `Eksperyment_4.11-4.12_Maska_Bitowa_20260817.md` | maska bitowa + obalenie własnego wyniku |
| `Katalog_Wszystkich_Eksperymentow_20260820.md` | wszystkie eksperymenty projektu |
| `Override_Hybrydowy_Wyjasnienie_20260820.md` | mechanizm override w szczegółach |
| `LICENCJE_I_PRAWA_DO_PUBLIKACJI.md` | licencje zależności, prawa do rysunków |
| `Sprawozdanie_Calosc_Scenariusz_20260820.md` | scenariusz rozmowy z wykładowcą |
| `wyniki_eksperymentow/` | 13 katalogów: CSV + opisy per eksperyment |

---

## Sześć liczb, które warto pamiętać

- **5 sekund kontra 110 mikrosekund** — poznanie reguły kontra jej stosowanie
- **0,00 %** — zgubionych ramek w całym zakresie testów przepustowości
- **0,0 %** — detekcja flag przez Claude, niezmienna mimo czterech interwencji
- **50 % → 96,7 %** — efekt hybrydowego override
- **0 % → 59 %** — maska bitowa, po korekcie własnego wyniku
- **0,6 %** — zużycie CPU przy parsowaniu ramek
