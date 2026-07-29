# Pytania, kwestie i sugestie do przedyskutowania z wykładowcą

Data: 2026-07-29
Kontekst: Eksperyment 3.1 (Wpływ odległości i przeszkód na opóźnienia i stratność
pakietów, Grupa 3 metodyki — `Pomiary dla CAN-Edge AI.md`, linie 69-85). Test
realnego zasięgu WiFi i BLE modułu ESP32, planowany do przeprowadzenia w terenie.
Poniższe kwestie wynikły w trakcie projektowania firmware'u i aplikacji pomiarowej,
zanim napisano jakikolwiek kod.

---

## 1. Kwestia architektoniczna: co dokładnie ma być zmierzone?

Metodyka mówi o "stabilności połączenia bezprzewodowego między serwerem na
Raspberry Pi a ESP32 na maszynie" — sugeruje to architekturę z **infrastrukturą
WiFi pośredniczącą** (router), analogiczną do docelowego wdrożenia (Pi + router
gospodarstwa/pojazdu + ESP32 jako klient). Rozważana implementacja testu zakłada
jednak **ESP32 jako własny punkt dostępowy (SoftAP)**, do którego łączy się
bezpośrednio telefon — to mierzy surowy zasięg RADIA ESP32 samego w sobie, NIE
zasięg całego łańcucha (ESP32 + typowy router gospodarczy), który w realnym
wdrożeniu może mieć zupełnie inny (zwykle gorszy) zasięg niż sam moduł ESP32.

**Pytanie:** czy cel eksperymentu to (a) scharakteryzowanie fizycznego limitu
radia ESP32 (uzasadnia SoftAP — czysty, powtarzalny pomiar, niezależny od
konkretnego routera), czy (b) realistyczne odwzorowanie docelowej architektury
systemu (wymagałoby dodania rzeczywistego routera/AP do testu i pomiaru całego
łańcucha, bliżej oryginalnego sformułowania metodyki)? Można też zrobić oba jako
dwa warianty tego samego eksperymentu, jeśli zakres pracy na to pozwala.

---

## 2. Role fizyczne: co się rusza, a co stoi w miejscu?

Metodyka: ESP32 zamontowane na pojeździe **oddala się** od stałej stacji bazowej
(Raspberry Pi). Planowana implementacja odwraca to fizycznie — ESP32 zostaje na
miejscu (dla wygody zasilania/portu szeregowego), a telefon z aplikacją pomiarową
jest urządzeniem mobilnym, które user odnosi na kolejne odległości.

Z punktu widzenia fizyki radiowej odległość i przeszkody działają symetrycznie
(nie ma znaczenia, które urządzenie faktycznie się porusza) — ale jeśli praca ma
odwzorowywać konkretny scenariusz użycia (np. sterowanie kombajnem oddalającym
się od stacji operatora), **anteny/obudowa/wysokość montażu** mogą różnić się
między "ESP32 na pojeździe" a "ESP32 na stole w pomieszczeniu", co mogłoby dać
inny wynik niż w realnym wdrożeniu.

**Pytanie:** czy odwrócenie ról (ESP32 stacjonarne, telefon mobilny) jest
akceptowalnym uproszczeniem metodologicznym (analogicznie do innych świadomych
odstępstw w poprzednich eksperymentach), czy wykładowca oczekuje, żeby to
faktycznie ESP32 było fizycznie przenoszone (np. zamontowane na koszu/wózku),
zgodnie z dosłownym brzmieniem metodyki?

---

## 3. Zakres testu: liczba pakietów i czas w terenie

Metodyka wymaga 10 000 pakietów kontrolnych na każdą odległość. Pełna macierz
(7 odległości × 2 scenariusze LOS/NLOS × 2 technologie WiFi/BLE = 28 przebiegów)
przy rozsądnym tempie (np. 20 pakietów/s) to ok. **4 godziny czystego czasu
transmisji** plus czas przemieszczania się między punktami — realistycznie
sesja terenowa rozciągnięta na więcej niż jeden dzień.

**Pytanie:** czy trzymać się pełnych 10 000 pakietów/punkt (zgodnie z literą
metodyki), czy zaakceptować mniejszą, ale wciąż statystycznie sensowną próbę
(np. 1000-2000 pakietów/punkt), analogicznie do wcześniejszych, uzgodnionych
z wykładowcą uproszczeń zakresu w innych eksperymentach tej pracy? Dodatkowo:
zapis "Liczba pomiarów do weryfikacji: 10" w metodyce — czy oznacza to 10
NIEZALEŻNYCH powtórzeń całego przebiegu 10 000 pakietów na każdą kombinację
odległość/scenariusz/technologia (co pomnożyłoby czas terenowy dziesięciokrotnie
i raczej nie jest realistyczne), czy jest to ogólna wskazówka liczebności próby
inaczej rozumiana niż w eksperymentach z Grupy 4 (gdzie N odnosiło się do liczby
powtórzeń pojedynczego pomiaru)?

---

## 4. Definicja scenariusza NLOS

Metodyka wymienia jako przykłady przeszkód "ściany budynków" i "metalowe poszycie
maszyn" — to dwa fizycznie bardzo różne tłumienia (żelbet/cegła vs metal, który
może niemal całkowicie ekranować sygnał w pewnych konfiguracjach).

**Pytanie:** czy jeden reprezentatywny scenariusz NLOS (np. ściana budynku
gospodarczego) wystarczy do celów pracy, czy wykładowca oczekuje rozróżnienia
kilku podscenariuszy NLOS (różne materiały/grubości przeszkód) jako osobnych
serii pomiarowych?

---

## 5. Asymetria oczekiwanego zasięgu WiFi vs BLE

Klasyczny zasięg BLE (nawet Bluetooth 5 Long Range, którego ESP32 klasyczne nie
wspiera) jest typowo znacząco krótszy niż WiFi — realistycznie należy się
spodziewać, że BLE straci połączenie całkowicie (100% utraconych pakietów) już
przy średnich odległościach z zaplanowanej listy (50-100m), podczas gdy WiFi
może utrzymać łączność znacznie dalej. To samo w sobie jest wartościowym,
poprawnym wynikiem (pokazuje twardy sufit technologii), nie błędem pomiaru.

**Pytanie:** czy zestawienie WiFi i BLE na tym samym wykresie/w tej samej tabeli
mimo drastycznie różnych zasięgów jest czytelne i pożądane dla pracy, czy lepiej
przedstawić je jako dwa osobne wykresy z różną skalą odległości (BLE: np.
0-50m, WiFi: 0-150m), żeby uniknąć "spłaszczenia" ciekawych danych BLE do
jednego punktu na końcu wspólnej osi?

---

## 6. Status aplikacji Android w strukturze pracy

Test wymaga napisania pierwszej w tym projekcie aplikacji mobilnej (Android/
Kotlin) — dotąd cały projekt to C++/Qt/firmware ESP32, bez komponentu mobilnego.
We wcześniejszej analizie strategicznej (ESP32 vs Raspberry Pi/Orange Pi) temat
"przyszłej aplikacji Android" pojawił się jako kierunek rozwoju, nie zrealizowany
dotąd komponent.

**Pytanie:** czy ta aplikacja pomiarowa (ping-pong + zapis wyników) powinna być
traktowana w pracy jako **jednorazowe narzędzie testowe** (wzmianka w rozdziale
metodycznym, kod w repozytorium, ale bez rozbudowanego opisu), czy jako
**pierwszy, udokumentowany krok w stronę docelowej aplikacji sterującej**
(wtedy warto już teraz zaprojektować ją szerzej niż tylko pod kątem tego
jednego testu, z myślą o rozbudowie)?

---

## 7. Reprezentatywność syntetycznego protokołu ping-pong

Zaplanowany pomiar RTT opiera się na syntetycznych pakietach ping-pong
(telefon wysyła znacznik, ESP32 natychmiast odsyła echo) — nie na rzeczywistym
ruchu CAN/telemetrii, jaki system przesyłałby w praktyce (który ma inny rozmiar
pakietu, inną częstotliwość, inny kierunek dominującego ruchu — głównie
ESP32→stacja bazowa, nie dwukierunkowo w równych proporcjach).

**Pytanie:** czy syntetyczny ping-pong jest wystarczający do scharakteryzowania
"jakości łącza w funkcji odległości" (co jest celem tego eksperymentu — czysta
właściwość radiowa, niezależna od treści ruchu), czy wykładowca oczekuje testu
bliższego rzeczywistemu profilowi ruchu systemu (np. jednokierunkowy strumień
ramek CAN w formacie zbliżonym do Eksperymentu 1.1, z osobnym kanałem
potwierdzeń do liczenia strat)?

---

## 8. Geolokalizacja jako dodatkowa weryfikacja wiarygodności

Po konsultacji z użytkownikiem zdecydowano rozszerzyć aplikację pomiarową o
zapis geolokalizacji (GPS) przy każdej próbce, obok znacznika czasu/daty i
informacji o pakiecie (numer sekwencyjny, RTT, status utraty, RSSI). Cel:
podniesienie wiarygodności eksperymentu przez obiektywną, niemanipulowalną
weryfikację rzeczywistego dystansu, niezależną od ręcznego pomiaru
kroków/taśmą.

**Ograniczenie do odnotowania**: typowa dokładność GPS smartfona w terenie to
ok. 3-5m, a w scenariuszu NLOS (blisko zabudowań) bywa gorsza z powodu
wielodrogowości sygnału (multipath) — czyli akurat tam, gdzie GPS miałby
najbardziej pomóc zweryfikować dystans, jego własna dokładność jest
najsłabsza. Rekomendacja: GPS jako dana **potwierdzająca/korygującą**
(wyliczony dystans od współrzędnych stacji bazowej jako kolumna kontrolna w
danych), nie jako zamiennik ręcznie wpisanej, kontrolowanej odległości.

**Pytanie:** czy taki dodatkowy ślad GPS (z jawnie udokumentowanym
ograniczeniem dokładności) wystarczy do celów "podniesienia wiarygodności",
czy wykładowca oczekuje dokładniejszej metody geodezyjnej (np. RTK-GPS,
dalmierz laserowy) dla samego pomiaru referencyjnego dystansu?

---

## 9. Sposób zapisu i eksportu danych z aplikacji

Rozważono dwie opcje zapisu wyników: serwer domowy (komputer użytkownika) oraz
Dysk Google. Pełna integracja z Google Drive API (OAuth, konsola Google
Cloud, ekran zgody) uznana za nieproporcjonalny nakład pracy dla narzędzia
czysto pomiarowego. Przyjęte rozwiązanie: aplikacja zawsze zapisuje CSV
lokalnie na telefonie jako źródło prawdy (bufor odporny na przerwanie łącza
pod testem), a eksport odbywa się dwutorowo — (1) wysyłka JSON-em do
prostego serwera HTTP na komputerze użytkownika, (2) natywny ekran
"Udostępnij" Androida, pozwalający zapisać plik do aplikacji Dysku Google
jednym dotknięciem, bez wbudowanego API.

**Pytanie:** czy taki sposób zapisu (lokalny bufor + eksport na żądanie) jest
wystarczający, czy wykładowca oczekuje zapisu w czasie rzeczywistym
("na żywo") podczas samego testu — co byłoby trudniejsze do zagwarantowania
akurat wtedy, gdy łącze pod testem jest zdegradowane (duży dystans/NLOS)?

---

## 10. Sugestie własne (do zaakceptowania/odrzucenia przez wykładowcę)

1. **Rekomenduję wariant (a) z punktu 1** (SoftAP, czysty pomiar radia ESP32)
   jako pierwszy, prostszy krok — daje czytelny, powtarzalny wynik niezależny
   od wyboru konkretnego routera. Wariant z pośredniczącym routerem (b) można
   rozważyć jako rozszerzenie, jeśli zakres pracy i czas na to pozwolą.
2. **Rekomenduję odwrócenie ról (ESP32 stacjonarne, telefon mobilny)** jako
   akceptowalne, jawnie udokumentowane odstępstwo — fizyka radiowa jest
   symetryczna względem tego, co się porusza, a różnica w wysokości/obudowie
   anteny jest pomijalna wobec rzędu wielkości badanych odległości (5-150m).
3. **Rekomenduję zredukowaną liczbę pakietów (1000-2000/punkt)** zamiast
   pełnych 10 000 — praktyczny kompromis czas/rygor, spójny z podejściem przyjętym
   we wcześniejszych eksperymentach tej pracy przy podobnych ograniczeniach
   terenowych/czasowych.
4. **Rekomenduję jeden reprezentatywny scenariusz NLOS** (ściana budynku
   gospodarczego) w pierwszym podejściu, z możliwością rozszerzenia o dodatkowe
   materiały later, jeśli wyniki pierwszego okażą się interesujące/niejednoznaczne.
5. **Rekomenduję osobne wykresy dla WiFi i BLE** (różne skale odległości) —
   uczciwiej pokazuje charakter obu technologii niż wymuszona wspólna oś.
6. **Rekomenduję traktować aplikację Android jako narzędzie testowe w tej
   fazie** — pełnoprawny projekt aplikacji sterującej to osobny, znacznie
   większy temat, który zasługuje na własną decyzję zakresu pracy, nie powinien
   być "dorzucony przy okazji" tego konkretnego eksperymentu.
7. **Rekomenduję syntetyczny ping-pong** — eksperyment bada właściwość samego
   łącza radiowego (RSSI/strata/RTT w funkcji odległości), nie zachowanie
   konkretnej aplikacji, więc niezależny od treści syntetyczny protokół jest
   metodologicznie czystszy i łatwiejszy do interpretacji niż mieszanie efektu
   odległości z efektem konkretnego wzorca ruchu CAN.
8. **Rekomenduję GPS jako dane potwierdzające, nie zastępujące** ręcznie
   wpisany dystans — z jawnie opisanym w pracy ograniczeniem dokładności
   (3-5m, gorzej w NLOS), żeby uniknąć wrażenia fałszywej precyzji.
9. **Rekomenduję lokalny bufor CSV + eksport na żądanie** (serwer domowy przez
   HTTP, Dysk Google przez natywny ekran "Udostępnij") zamiast zapisu w czasie
   rzeczywistym — odporne na degradację łącza pod testem, prostsze we
   wdrożeniu, bez integracji z Google Drive API.
