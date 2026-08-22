# Źródła materiałów referencyjnych — pinouty i dokumentacja płytek

Data pobrania: **2026-08-17**

Wszystkie pliki pobrano **bezpośrednio z oficjalnych serwerów producentów**.
Sumy kontrolne pozwalają zweryfikować, że plik nie zmienił się od czasu pobrania —
przydatne przy cytowaniu w artykule, bo producenci aktualizują dokumentację
pod tym samym adresem.

---

## 1. Pobrane dokumenty — pełne adresy źródłowe

### Orange Pi Zero 3 — oficjalny manual użytkownika

| | |
|---|---|
| **Plik** | `OrangePi_Zero3_User_Manual_v1.1_oficjalny.pdf` |
| **URL** | https://orangepi.net/wp-content/uploads/2023/12/OrangePi_Zero3_H618_user-manual_v1.1.pdf |
| **Wydawca** | Shenzhen Xunlong Software Co., Ltd. |
| **Wersja** | v1.1 |
| **Rozmiar / strony** | 16 MB / 304 strony |
| **SHA-256 (początek)** | `c005b629693b0d43…` |
| **Co zawiera** | **Tabela złącza 26-pin na stronie 123** — jedyne autorytatywne źródło pinoutu tej płytki |

### ESP32-WROOM-32 — datasheet modułu

| | |
|---|---|
| **Plik** | `ESP32-WROOM-32_datasheet_espressif.pdf` |
| **URL** | https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf |
| **Wydawca** | Espressif Systems |
| **Rozmiar / strony** | 608 KB / 46 stron |
| **SHA-256 (początek)** | `a88f0a4376106498…` |
| **Co zawiera** | Wyprowadzenia i funkcje pinów **modułu**. Uwaga: płytki „ESP32 CH340" to klony DevKitC z tym modułem — rozmieszczenie na listwie zależy od wariantu płytki (30/36/38 pinów), nie od modułu |

### Raspberry Pi Zero — schemat uproszczony

| | |
|---|---|
| **Plik** | `raspberry-pi-zero-reduced-schematics.pdf` |
| **URL** | https://datasheets.raspberrypi.com/rpizero/raspberry-pi-zero-reduced-schematics.pdf |
| **Wydawca** | Raspberry Pi Ltd |
| **Rozmiar / strony** | 120 KB / 1 strona |
| **SHA-256 (początek)** | `1eb1e7055028fc29…` |

### Raspberry Pi Zero — rysunek mechaniczny

| | |
|---|---|
| **Plik** | `raspberry-pi-zero-mechanical-drawing.pdf` |
| **URL** | https://datasheets.raspberrypi.com/rpizero/raspberry-pi-zero-mechanical-drawing.pdf |
| **Wydawca** | Raspberry Pi Ltd |
| **Rozmiar / strony** | 44 KB / 1 strona |
| **SHA-256 (początek)** | `b9c361883c94ab80…` |
| **Co zawiera** | Wymiary płytki i **położenie złącza GPIO** |

---

## 2. Źródła sprawdzone i ODRZUCONE — ważne dla metodyki

Przy ustalaniu pinoutu Orange Pi Zero 3 natrafiono na **sprzeczne dane**
w źródłach wtórnych. Odnotowane, bo pokazuje, dlaczego potrzebna była
weryfikacja krzyżowa:

| Źródło | Co podaje | Ocena |
|---|---|---|
| https://docs.mainsail.xyz/mainsailos/supported-sbcs/orange-pi-zero-3/ | SPI na pinach 21-26 (MOSI=26, MISO=24, SCLK=22, CS=21) | **błędne numery pinów**; poprawnie wskazuje natomiast, że używany jest CS1 |
| https://docs.cirkitdesigner.com/component/d9748184-1fd4-40fe-9023-29bde4ff02c1/orange-pi-zero-3 | układ zgodny z Raspberry Pi (19/21/23/24) | **zgodne z manualem**, ale podaje nazwy w konwencji Raspberry Pi, nie Allwinnera |

**Rozstrzygnięcie:** oficjalny manual (str. 123) **plus** odczyt z device tree
działającej płytki (`sun50i-h618-orangepi-zero3.dtb` — węzły `spi1-pins`,
`spi1-cs0-pin`, `spi1-cs1-pin`). Dwa niezależne źródła, zgodne ze sobą.

Szczegóły: `Eksperyment_4.6_Replikacja_OrangePiZero3_20260814.md`, sekcja 3.

---

## 3. Czego świadomie NIE pobrano

**Kolorowych „rysunków pinoutu" z blogów, sklepów i wiki społecznościowych.**
Są to utwory objęte prawem autorskim; ich powszechna dostępność nie jest zgodą
na rozpowszechnianie, a przedruk w artykule naukowym byłby naruszeniem.

Zamiast tego: **diagramy rysowane samodzielnie** na podstawie danych
z powyższych dokumentów. Samo przyporządkowanie „pin 19 = PH7 = SPI1_MOSI"
jest faktem, a fakty nie podlegają prawu autorskiemu — chroniona jest konkretna
forma graficzna.

Szersze omówienie: `LICENCJE_I_PRAWA_DO_PUBLIKACJI.md`, sekcja 3.

---

## 4. Diagramy własne projektu

| Diagram | Plik | Podstawa | Status praw |
|---|---|---|---|
| Orange Pi Zero 3, złącze 26-pin | `../OrangePi_Zero3_Pinout_26pin_20260814.pdf` (źródło HTML obok) | manual v1.1 str. 123 + device tree płytki | **utwór własny — publikowalny bez ograniczeń** |

Proponowany podpis pod rysunkiem w artykule:

> Rys. X. Złącze 26-pin Orange Pi Zero 3 z zaznaczonymi liniami SPI1 i wejściem
> przerwania kontrolera CAN. Opracowanie własne na podstawie *OrangePi Zero3 H618
> User Manual v1.1*, s. 123 oraz odczytu drzewa urządzeń działającego systemu.

---

## 5. Do uzupełnienia

- **Raspberry Pi Zero (2017)** — złącze 40-pin w układzie standardowym dla
  Raspberry Pi. Diagram własny do narysowania na podstawie pobranych schematów.
- **ESP32 DevKitC / klon z CH340** — **wymaga potwierdzenia wariantu płytki**
  (30, 36 czy 38 pinów). Datasheet modułu opisuje wyprowadzenia układu, ale nie
  rozmieszczenie na listwie konkretnej płytki deweloperskiej. Narysowanie złego
  wariantu byłoby gorsze niż brak diagramu.

---

## 6. Jak cytować w artykule

```
[1] Shenzhen Xunlong Software Co., Ltd., "Orange Pi Zero3 H618 User Manual",
    wersja 1.1, s. 123. [Online]. Dostępne:
    https://orangepi.net/wp-content/uploads/2023/12/OrangePi_Zero3_H618_user-manual_v1.1.pdf
    [dostęp: 17.08.2026]

[2] Espressif Systems, "ESP32-WROOM-32 Datasheet". [Online]. Dostępne:
    https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf
    [dostęp: 17.08.2026]

[3] Raspberry Pi Ltd, "Raspberry Pi Zero Reduced Schematics". [Online]. Dostępne:
    https://datasheets.raspberrypi.com/rpizero/raspberry-pi-zero-reduced-schematics.pdf
    [dostęp: 17.08.2026]
```

Data dostępu jest istotna: producenci aktualizują dokumenty pod tym samym
adresem, więc bez niej odwołanie może z czasem przestać się zgadzać.
Sumy kontrolne z sekcji 1 pozwalają wykazać, do której wersji się odnosiliście.
