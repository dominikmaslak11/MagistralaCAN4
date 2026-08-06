# Eksperyment 4.5 (propozycja) — Ciągła, długotrwała obserwacja sygnału na Raspberry Pi Zero jako remedium na krótkie okno "Cold Start"

Data: 2026-08-06
Status: PROPOZYCJA do przedyskutowania z wykładowcą — nie wdrożone, nie uruchomione.
Geneza: bezpośrednia konsekwencja dwóch wyników z tej samej sesji (Eksperyment 4.3
Etap B, Eksperyment 4.4) — nie ogólny pomysł "spróbujmy nowszego sprzętu".

---

## 1. Skąd ten pomysł — dwa konkretne, dzisiejsze wyniki

**Wynik 1 (Eksperyment 4.3, Etap B — auto-etykietowanie):** klasyczny klasyfikator
Kierunku B, który na wąskim mini-DBC Eksperymentu 4.1 osiągał ~97-100% skuteczności,
na szerszym, zróżnicowanym korpusie (40 CAN ID) spadł do Recall=55.9%, a trafność
samej maski bitowej (które konkretnie bity) do zaledwie **15.8%**. Zdiagnozowana
przyczyna (część 1 z 2): **okno obserwacji było za krótkie** — niektóre flagi bitowe
nie zdążyły przełączyć się w oba stany (0 i 1) w dostępnych ~200 próbkach, mimo że
naprawdę są flagami.

**Wynik 2 (Eksperyment 4.4 — Qdrant retrieval-augmented warm-start):** podpowiedź
oparta na bibliotece zbudowanej z INNEGO przebiegu/seed (offline, symulacja) miała
tylko ~70% trafności na żywych danych z prawdziwej magistrali CAN — i ta niedoskonała
podpowiedź czasem **utwierdzała model w istniejącym błędzie** zamiast go korygować
(regres na bit_flag, −3.7 do −4.2pp u dwóch przebadanych modeli).

**Wspólny mianownik obu problemów:** nasza dotychczasowa architektura (ESP32,
epizodyczny "Cold Start" trwający sekundy/minuty w ramach jednego uruchomienia
programu) fizycznie nie ma czasu ani pamięci, żeby zbudować DŁUGOTRWAŁĄ,
wiarygodną statystykę per-bajt z TEGO KONKRETNEGO pojazdu.

---

## 2. Hipoteza

Urządzenie z **ciągłym, wielogodzinnym/wielodniowym dostępem do magistrali CAN**
(a nie krótkim zrzutem) mogłoby:

1. Akumulować statystykę `independentBitMask` przez cały czas pracy pojazdu —
   flagi o rzadkich przełączeniach (np. kierunkowskaz włączany raz na godzinę)
   dostałyby wystarczająco dużo czasu, żeby klasyfikator zobaczył OBA stany.
2. Budować bibliotekę Qdrant **NA BIEŻĄCO, z prawdziwych, poprawnie
   zweryfikowanych sygnałów TEGO pojazdu**, zamiast polegać na osobnym,
   syntetycznym korpusie offline (usuwa dokładnie problem "cudzej biblioteki"
   z Eksperymentu 4.4 — spadek trafności z 89% w obrębie tego samego korpusu do
   ~70% między różnymi przebiegami).

**Pytanie badawcze:** czy słabość klasyfikatora (4.3) i podpowiedzi retrieval (4.4)
wynikała z samej METODY, czy tylko z ZA KRÓTKIEGO CZASU OBSERWACJI — i czy
wydłużenie tego czasu (przez ciągłą pracę urządzenia) rozwiązuje oba problemy
bez zmiany ani jednej linii logiki klasyfikatora/retrieval.

---

## 3. Dlaczego Raspberry Pi Zero, nie kolejny ESP32

Nie chodzi o wymianę ESP32 jako "lepszy/nowszy sprzęt" (patrz wcześniejsza analiza
`Analiza_ESP32_vs_RaspberryPi_OrangePi_20260727.txt` — tam wniosek: nie zastępować,
ewentualnie dodać jako drugi poziom porównawczy). Chodzi o coś, czego ESP32 w
dotychczasowych eksperymentach fizycznie nie robił: **pracę ciągłą, w tle, z pełnym
Linuksem i wystarczającą pamięcią/mocą, żeby jednocześnie**:
- nasłuchiwać magistralę CAN (SocketCAN, bezpośrednio, bez pośrednictwa MCP2515+SPI
  jeśli użyty zostanie HAT z kontrolerem CAN natywnie wspieranym przez jądro Linux),
- utrzymywać per-bajt/per-CAN-ID historię NIEOGRANICZONĄ do jednego uruchomienia
  programu (zapis na dysk/SD, przetrwanie restartu),
- uruchamiać lokalnie Qdrant (embedded) i klasyfikator Kierunku B w tle, cały czas,
- ewentualnie buforować/synchronizować z LLM w chmurze tylko gdy naprawdę potrzebne
  (Cold Start = pierwsze spotkanie z nowym CAN ID), zamiast za każdym razem od zera.

---

## 4. Proponowana metodyka (szkic)

0. **Integracja z istniejącym sprzętem (bez zmian po stronie generatora):**
   PEAK PCAN-USB zostaje podłączony do laptopa jak dotychczas i generuje ruch
   TYM SAMYM skryptem co w Eksperymencie 4.4 (`generate_traffic_diverse.py
   --iface can0`). Raspberry Pi dołącza jako KOLEJNY WĘZEŁ na tej samej,
   fizycznej magistrali CAN_H/CAN_L (obok ESP32, albo na czas tego testu
   zamiast niego) — wymaga własnego kontrolera CAN (HAT z MCP2515, ten sam typ
   chipu co już używany z ESP32, tylko w wersji na złącze GPIO). Różnica
   wobec ESP32: Raspberry Pi (Linux) obsługuje SocketCAN natywnie w jądrze —
   nie potrzebuje pośrednictwa WebSocket, może nasłuchiwać `can0` bezpośrednio
   przez `python-can`/`candump`.
1. Firmware/oprogramowanie: Raspberry Pi OS Lite (headless) + Python, demon
   nasłuchujący SocketCAN, budujący per-bajt statystyki `independentBitMask`
   w sposób CIĄGŁY (nie per-sesja) — zapis stanu na dysk co N sekund.
2. Test kontrolowany: powtórzyć DOKŁADNIE ten sam syntetyczny ruch z Etapu A/4.4
   (te same konfiguracje CAN ID), ale obserwowany przez WIELE GODZIN zamiast
   pojedynczego krótkiego okna — zmierzyć, czy recall/trafność maski klasyfikatora
   rośnie w funkcji czasu obserwacji (wykres: dokładność vs godziny obserwacji).
3. Jeśli zasoby pozwolą: powtórzyć test Qdrant warm-start (Eksperyment 4.4) z
   biblioteką budowaną NA BIEŻĄCO z tego samego przebiegu, zamiast z osobnego
   korpusu — sprawdzić, czy trafność podpowiedzi wraca w okolice 89% (jak w
   teście w obrębie jednego korpusu) zamiast ~70% (jak między różnymi
   przebiegami).

---

## 5. Otwarte pytania / ryzyka do zaznaczenia

1. Raspberry Pi Zero (oryginalny, jednordzeniowy ARM11 @1GHz) może być zbyt słaby
   do jednoczesnego prowadzenia Qdrant + klasyfikatora + nasłuchu CAN w czasie
   rzeczywistym — **Raspberry Pi Zero 2 W (czterordzeniowy Cortex-A53) może być
   niezbędny** zamiast oryginalnego Zero. Do ustalenia po sprawdzeniu, który
   konkretnie model użytkownik posiada.
2. Wymaga kontrolera CAN podłączonego do Pi (HAT z MCP2515 przez SPI, analogicznie
   do ESP32, lub HAT z natywnym kontrolerem CAN) — **dodatkowy zakup sprzętu,
   OBOK samego Raspberry Pi Zero 2 W** (patrz `Eksperyment_4.5_Uzasadnienie_Zakupu_RaspberryPi_20260806.md`),
   nie jest jeszcze dostępny.
3. Test "wieloosobowy" (wiele godzin ciągłej obserwacji) wymaga innego rytmu
   pracy niż dotychczasowe eksperymenty (minuty/pojedyncza sesja) — dłuższy
   czas kalendarzowy do przeprowadzenia.
4. To NIE zastępuje pytań z `Pytania_Do_Wykladowcy_Eksperyment_4.3_20260806.md`
   (jakość klasyfikatora, wybór modelu do fine-tuningu) — to osobny, komplementarny
   kierunek badawczy.

---

## 6. Status praktyczny (2026-08-06)

Użytkownik potwierdził posiadanie fizycznego Raspberry Pi Zero i chęć
przetestowania tego pomysłu. Wymagana pomoc: wgranie systemu operacyjnego na
kartę microSD (użytkownik nie ma w tym doświadczenia). Do ustalenia przed
rozpoczęciem: dokładny model posiadanego Pi Zero (oryginalny/W/2 W — patrz
ryzyko 1 wyżej) oraz dostępność karty microSD i czytnika (na komputerze
roboczym wykryto już podłączony czytnik kart, `Alcor Micro Corp. Multi Flash
Reader`, USB ID 058f:6366 — prawdopodobnie nadaje się do zapisu obrazu systemu).
