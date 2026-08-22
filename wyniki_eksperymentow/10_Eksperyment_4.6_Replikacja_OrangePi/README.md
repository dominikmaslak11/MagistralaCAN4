# Eksperyment 4.6 — replikacja na drugiej platformie (Orange Pi Zero 3)

## 1. Co badano — wyjaśnienie dla osoby spoza tematu

Eksperyment 4.5 dał dobry wynik na Raspberry Pi. Powstało pytanie, które
w badaniach naukowych zadaje się zawsze:

**Czy ten wynik jest własnością METODY, czy tylko tej konkretnej płytki?**

Jeśli ten sam program na zupełnie innym sprzęcie da ten sam wynik — metoda jest
wiarygodna. Jeśli wynik się rozjedzie — mierzyliśmy właściwości sprzętu, nie metody.

## 2. Czym i jak to zrobiono

**Zmieniono wszystko, co dało się zmienić** poza samym programem:

| Element | Raspberry Pi Zero W | Orange Pi Zero 3 |
|---|---|---|
| Procesor | Broadcom BCM2835, 1 rdzeń | Allwinner H618, 4 rdzenie |
| Architektura | ARMv6 (32-bit) | ARMv8 / aarch64 (64-bit) |
| System | Raspberry Pi OS Bookworm Legacy | Armbian, Debian 13 Trixie |
| Magistrala SPI | SPI0 | **SPI1, wybór układu CS1** |

**Program pozostał niezmieniony** — przeniesiono go bez modyfikacji ani jednej linii.

**Koszt integracji sprzętowej (podany uczciwie):** Armbian **nie ma gotowej
konfiguracji** dla układu MCP2515 — trzeba było napisać własny opis sprzętu
(overlay device-tree). Dodatkowo publikowane w internecie tabele wyprowadzeń
Orange Pi Zero 3 **są ze sobą sprzeczne**. Rozstrzygnięto to oficjalnym
podręcznikiem producenta oraz odczytem konfiguracji z działającego systemu.

Przed uruchomieniem właściwego pomiaru napisano **skaner sprawdzający sam układ
MCP2515** — reset, odczyt rejestrów kontrolnych, test zapisu i odczytu wzorców.
Rozdziela to dwie klasy błędów, które wyglądają identycznie: złe okablowanie od
złej konfiguracji programu.

## 3. Pliki wynikowe

| Plik | Co zawiera | Pochodzenie |
|---|---|---|
| `porownanie_platform.csv` | wyniki obu platform obok siebie | **przeliczone** z surowych stanów obserwatora |
| `strojenie_progu.csv` | wpływ progu klasyfikatora na wynik | **przeliczone** z surowego stanu |

### Kolumny `strojenie_progu.csv`

| Kolumna | Znaczenie |
|---|---|
| `prog` | wartość progu skokowości (`big_jumps / changed_pairs`) |
| `wykrytych` | ile pozycji uznano za flagi |
| `TP` / `FP` | trafienia / fałszywe alarmy |
| `Recall_%` / `Precision_%` | czułość i precyzja |

## 4. Przykładowe dane

```
prog,wykrytych,TP,FP,Recall_%,Precision_%
0.50,12,12,0,60.0,100.0
0.46,14,14,0,70.0,100.0
0.30,17,17,0,85.0,100.0
```

Odczyt: obniżanie progu zwiększa liczbę wykrytych flag, **nie powodując ani
jednego fałszywego alarmu** — precyzja pozostaje 100 % na całym zakresie.

## 5. Jak sprawdzano poprawność

1. **Weryfikacja warstwy sprzętowej przed pomiarem** — skaner SPI potwierdził,
   że układ odpowiada poprawnie (rejestr stanu = 0x80 po resecie, rejestr
   sterujący = 0x87, wzorce 0x55 i 0xAA odczytane bez błędu).
2. **Potwierdzenie kwarcu pośrednio** — sterownik ustawił czas kwantu 250 ns przy
   dzielniku 2, co wychodzi **wyłącznie** przy kwarcu 16 MHz. Częstotliwości
   kwarcu nie da się odczytać programowo.
3. **Test dwukierunkowy z PEAK PCAN-USB** — 6 ramek w obie strony, zero błędów,
   liczniki błędów magistrali na zerze.
4. **Porównanie zbiorów wykrytych pozycji**, nie tylko liczb — przy wspólnym
   progu sprawdzono, czy obie platformy wskazały **te same bajty**.

## 6. Wynik końcowy

| Platforma | Recall | Precision | Ramek w przebiegu | Zgubionych |
|---|---|---|---|---|
| Raspberry Pi Zero W | **85,0 %** | **100 %** | 1 625 649 | 0 |
| Orange Pi Zero 3 | **85,0 %** | **100 %** | 1 685 554 | 0 |

**Wynik identyczny co do liczby.** Przy wspólnym progu zbiory wykrytych bajtów
pokrywają się w **11 z 12** pozycji (jedna różnica to przypadek graniczny).

**Dodatkowo:** Faza 5 (embeddingi neuronowe) uruchomiona **po raz pierwszy na
sprzęcie docelowym** — na Raspberry Pi Zero W było to niewykonalne z powodu
architektury ARMv6. Wyniki zgodne co do ostatniej cyfry z laptopem.

## 7. Wniosek

**Wniosek Eksperymentu 4.5 jest własnością metody, nie platformy sprzętowej.**
Inny procesor, inna architektura, inny system operacyjny i inna magistrala SPI
dały ten sam wynik liczbowy.

**Zastrzeżenie praktyczne:** Orange Pi ma słabsze wsparcie peryferiów niż
Raspberry Pi — nakładki projektowane pod Raspberry Pi nie działają
bezpośrednio. Koszt jest jednorazowy (jeden plik konfiguracyjny), ale realny.
