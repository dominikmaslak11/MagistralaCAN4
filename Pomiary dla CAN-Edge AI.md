## Pomiary dla CAN-Edge AI

## Grupa 1: Pomiary opóźnień czasowych (Latency Performance)

Wykazanie różnicy czasu między fazą adaptacji (Cold Start) a fazą pracy ustalonej (Hot Execution) jest kluczowe dla obrony tezy o przydatności systemu w czasie rzeczywistym.

### Eksperyment 1.1: Profilowanie czasu fazy adaptacji (Cold Start Latency Breakdown)

* **Cel:** Określenie, które elementy architektury generują największe opóźnienie podczas pierwszego kontaktu z nieznaną ramką CAN.
* **Metodyka:** 
  1. Wprowadź na magistralę nową, niezdefiniowaną wcześniej ramkę CAN.
  2. Zaimplementuj w kodzie ESP32 oraz serwera nadrzędnego (Raspberry Pi/MCP) znaczniki czasu (timestamping) o rozdzielczości mikrosekundowej.
  3. Zmierz poszczególne składowe całkowitego czasu $T_{total}$:
     * $t_{det}$ – czas od pojawienia się ramki do podjęcia decyzji o wysłaniu zapytania.
     * $t_{tx\_up}$ – czas transmisji bezprzewodowej (ESP32 $\rightarrow$ serwer MCP).
     * $t_{llm}$ – czas wnioskowania modelu LLM (od wysłania zapytania do otrzymania pełnej odpowiedzi z regułą).
     * $t_{comp}$ – czas ewentualnej kompilacji kodu na serwerze i przygotowania binarnego payloadu.
     * $t_{ota}$ – czas bezprzewodowej aktualizacji tabeli reguł na ESP32 (Over-The-Air).
  4. Wykonaj $N = 30$ prób dla każdego z testowanych modeli LLM (np. Claude 3.5 Sonnet, DeepSeek-V3, GPT-4o - Wybór dowolny przez użytkownika).
* **Mierzone parametry:** Czas [ms] dla każdej składowej, średnia arytmetyczna ($\mu$), odchylenie standardowe ($\sigma$).
* **Format prezentacji:** Tabela porównawcza składowych opóźnienia dla różnych modeli LLM oraz wykres słupkowy skumulowany.
* **Liczba pomiaró do weryfikacji:** 30 dla kazdego z LLM

### Eksperyment 1.2: Opóźnienie reakcji w stanie ustalonym (Hot Execution Latency)

* **Cel:** Zmierzenie czasu reakcji systemu na ramkę CAN po wdrożeniu reguły na ESP32.
* **Metodyka:**
  1. Podłącz oscyloskop dwukanałowy lub analizator stanów logicznych.
  2. Kanał 1 podłącz pod linię RX transceivera CAN (moment nadejścia ramki wyzwalającej).
  3. Kanał 2 podłącz pod pin GPIO mikrokontrolera ESP32, który jest aktywowany w odpowiedzi na tę ramkę (lub pod linię TX transceivera CAN, jeśli odpowiedzią jest nowa ramka).
  4. Wykonaj $N = 1000$ pomiarów przy stabilnej pracy magistrali.
* **Mierzone parametry:** Czas reakcji $t_{resp}$ [$\mu$s] (różnica czasu między zboczem opadającym na Kanale 1 a zboczem na Kanale 2).
* **Format prezentacji:** Wykres rozkładu gęstości prawdopodobieństwa (histogram) czasu reakcji.
* **Liczba pomiarów do weryfikacji:** 30

---

## Grupa 2: Testy obciążalności i przepustowości (Stress & Throughput Testing)

Testy te wykażą granice stabilności urządzenia brzegowego przy intensywnym ruchu na magistrali.

### Eksperyment 2.1: Maksymalna bezstratna przepustowość (CAN Frame Throughput)

* **Cel:** Określenie maksymalnej liczby ramek na sekundę, jaką ESP32 może przetwarzać w czasie rzeczywistym bez gubienia pakietów.
* **Metodyka:**
  1. Za pomocą zewnętrznego generatora (np. drugiego mikrokontrolera lub dedykowanego interfejsu USB-CAN) generuj ruch na magistrali z krokiem co 100 ramek/s (od 100 do 5000 ramek/s).
  2. Ustaw prędkości magistrali CAN na standardowe wartości motoryzacyjne: $250 \text{ kbps}$ oraz $500 \text{ kbps}$.
  3. Zliczaj ramki wysłane przez generator ($N_{sent}$) i odebrane bezbłędnie przez ESP32 ($N_{rcvd}$).
* **Mierzone parametry:** Współczynnik utraty ramek (Frame Loss Rate) określony jako:

$FLR = \left(1 - \frac{N_{rcvd}}{N_{sent}}\right) \times 100%$

* **Format prezentacji:** Wykres liniowy przedstawiający $FLR$ [%] w funkcji natężenia ruchu [ramek/s] dla różnych prędkości magistrali.
* **Liczba pomiarów do weryfikacji:** dowolna (większa niż 30)

### Eksperyment 2.2: Odporność bufora podczas fazy adaptacji (Buffer Overflow Threshold)

* **Cel:** Zbadanie zachowania systemu w sytuacji, gdy nadchodzi nowa ramka, a system czeka ~2,2 s na odpowiedź z LLM.
* **Metodyka:**
  1. Wywołaj stan "Cold Start" (zapytanie do LLM).
  2. W tym samym czasie symuluj ciągły napływ innych standardowych ramek z częstotliwością $f$ (np. 100, 500, 1000 Hz).
  3. Sprawdź, przy jakiej częstotliwości i jakim rozmiarze bufora odbiorczego (Rx Buffer) w ESP32 dochodzi do przepełnienia i utraty danych telemetrycznych.
* **Mierzone parametry:** Maksymalny bezpieczny czas oczekiwania na LLM przed przepełnieniem bufora dla danej częstotliwości wejściowej.
* **Format prezentacji:** Tabela progowa (częstotliwość vs dopuszczalny czas oczekiwania).
* **Liczba pomiaró do weryfikacji:** 60

---

## Grupa 3: Jakość komunikacji bezprzewodowej i zasięg (Wireless Range & Quality of Service)

W przypadku sterowania półautonomicznego (np. kombajnem), krytyczna jest stabilność połączenia bezprzewodowego między serwerem na Raspberry Pi a ESP32 na maszynie.

### Eksperyment 3.1: Wpływ odległości i przeszkód na opóźnienia i stratność pakietów

* **Cel:** Określenie bezpiecznego promienia sterowania pojazdem w otwartej przestrzeni oraz w środowisku z przeszkodami (np. zabudowania gospodarstwa).
* **Metodyka:**
  1. Umieść serwer Raspberry Pi w stałym punkcie (np. jako stacja bazowa).
  2. Oddalaj ESP32 (zamontowane na pojeździe) na odległości: 5m, 10m, 25m, 50m, 75m, 100m, 150m.
  3. Przeprowadź testy dla dwóch scenariuszy:
     * LOS (Line of Sight) – bezpośrednia widoczność anten.
     * NLOS (Non-Line of Sight) – przeszkody mechaniczne (ściany budynków, metalowe poszycie maszyn).
  4. Na każdym dystansie prześlij serię 10 000 pakietów kontrolnych.
* **Mierzone parametry:** Poziom mocy odbieranego sygnału RSSI [dBm], współczynnik utraty pakietów [%], średni czas RTT (Round Trip Time) połączenia bezprzewodowego.
* **Format prezentacji:** Wykres dwuosiowy: RSSI oraz Packet Loss w funkcji odległości dla scenariuszy LOS i NLOS.
* **Liczba pomiaró do weryfikacji:** 10

---

## Grupa 4: Adaptowalność i skuteczność deszyfracji (Adaptability & Accuracy)

Eksperymenty te wykażą naukową "inteligencję" systemu na różnych obiektach badawczych.

### Eksperyment 4.1: Skuteczność identyfikacji sygnałów (Decoding Accuracy vs Ground Truth)

* **Cel:** Porównanie poprawności odszyfrowania parametrów przez model LLM w zestawieniu z rzeczywistą bazą danych (plik DBC).
* **Metodyka:**
  1. Podłącz system do magistrali pojazdu, dla którego posiadasz pełny, zweryfikowany plik DBC (np. ciągnik rolniczy lub samochód osobowy).
  2. Zarejestruj 10 wybranych sygnałów (np. RPM, temperatura płynu, kąt skrętu kierownicy, stan świateł) za pomocą tradycyjnego dekodera DBC (jako Ground Truth).
  3. Uruchom algorytm deszyfracji oparty na LLM i analizie trendów.
* **Mierzone parametry:**
  * Błąd średniokwadratowy (MSE) dla sygnałów ciągłych (np. RPM, ciśnienie).
  * Macierz pomyłek (Confusion Matrix), precyzja (Precision) oraz czułość (Recall) dla stanów dyskretnych (np. otwarte/zamknięte drzwi, światła wł/wył).
* **Format prezentacji:** Macierz pomyłek oraz tabela metryk statystycznych (MSE, Precision, Recall, F1-score) dla każdego badanego parametru.
* **Liczba pomiaró do weryfikacji:** 100

### Eksperyment 4.2: Porównanie międzymaszynowe (Cross-Machine Adaptability)

* **Cel:** Wykazanie uniwersalności systemu poprzez testy na różnych typach maszyn.
* **Metodyka:**
  1. Przeprowadź procedurę automatycznego uczenia i deszyfracji na co najmniej dwóch różnych maszynach (np. samochód osobowy osobowy z protokołem OBD-II/CAN oraz kombajn/traktor z protokołem ISOBUS/SAE J1939).
  2. Zmierz liczbę iteracji promptu (zapytań korygujących do LLM) oraz łączny czas potrzebny do poprawnego zmapowania pierwszych 5 podstawowych parametrów na każdej z maszyn.
* **Mierzone parametry:** Liczba zapytań do LLM, łączny czas konfiguracji [s], procentowa zgodność z rzeczywistymi parametrami maszyn.
* **Format prezentacji:** Tabela porównawcza parametrów adaptacji dla różnych maszyn/protokołów.
* **Liczba pomiaró do weryfikacji:** dowolna

---

## Grupa 5: Zużycie zasobów sprzętowych (Resource Profiling)

Niezbędne dla wykazania, czy system mieści się w limitach urządzeń klasy Edge.

### Eksperyment 5.1: Profilowanie pamięci i CPU

* **Cel:** Ocena obciążenia sprzętowego mikrokontrolera ESP32.
* **Metodyka:**
  1. Wykorzystaj natywne funkcje diagnostyczne systemu operacyjnego (np. FreeRTOS na ESP32) do monitorowania zasobów.
  2. Zmierz użycie zasobów w trzech stanach pracy:
     * *Idle* – nasłuchiwanie magistrali bez wykrywania anomalii.
     * *Parsing & Filtering* – aktywna filtracja i klasyfikacja ruchu.
     * *OTA Update* – moment aktualizacji i kompilacji nowej reguły w pamięci.
* **Mierzone parametry:** Zużycie pamięci RAM [kB], użycie pamięci Flash [kB], obciążenie procesora CPU [%].
* **Format prezentacji:** Tabela profilowania zasobów dla różnych stanów pracy.
* **Liczba pomiaró do weryfikacji:** 40


