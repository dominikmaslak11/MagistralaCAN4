# Pytania, kwestie i sugestie do przedyskutowania z wykładowcą

Data: 2026-08-01
Kontekst: Eksperyment 5.1 (Profilowanie pamięci i CPU, Grupa 5 metodyki —
`Pomiary dla CAN-Edge AI.md`, linie 122-133) — dokończony w pełni: wszystkie trzy
wymagane stany (Idle, Parsing & Filtering, OTA Update) zmierzone, dodatkowo
zweryfikowany sprzętowo przez JTAG (ESP-Prog, SystemView), z niespodziewanym przy
okazji wynikiem o wpływie samego narzędzia pomiarowego na mierzoną wielkość.
Pełne dane i uzasadnienia w `esp_experiment_5_1_jtag/README.md`.

---

## 1. Który wynik CPU trafia do finalnej tabeli metodyki?

Mamy teraz **cztery** wiarygodne, w pełni zweryfikowane liczby dla tego samego
pomiaru (stan Idle/Parsing, ta sama logika firmware, N=40 każda):

| Wariant | CPU IDLE | CPU PARSING |
|---|---|---|
| Oryginalny firmware Arduino IDE (bez JTAG) | 0,598% | 0,599% |
| Port ESP-IDF, bez JTAG/apptrace | 0,817% | 0,823% |
| Port ESP-IDF, JTAG podłączony biernie | 1,789% | 1,799% |
| Port ESP-IDF, JTAG aktywnie nagrywa (SystemView) | 2,476% | 2,500% |

Różnica między skrajnymi wartościami (0,6% vs 2,5%) to ponad 4-krotność — mimo że
to dokładnie ten sam algorytm na tym samym sprzęcie. Rozkłada się to na trzy
niezależne, zmierzone przyczyny (środowisko budowania, sama gotowość kanału trace,
aktywne przesyłanie danych — pełny opis w README).

**Pytanie:** czy do finalnej tabeli metodyki Grupy 5 w pracy ma trafić (a) tylko
wynik najmniej inwazyjny (oryginalny Arduino, 0,6%) jako główna liczba, z
pozostałymi trzema jako dyskusja/dodatek, (b) wszystkie cztery liczby wprost w
tabeli głównej jako demonstracja "efektu obserwatora" w profilowaniu systemów
wbudowanych, czy (c) coś pomiędzy (np. dwie skrajne wartości)? Rekomendujemy (a) —
jest zgodny z duchem metodyki (ocena rzeczywistego obciążenia urządzenia w
docelowej konfiguracji, nie w konfiguracji deweloperskiej z podłączonym
debuggerem), a warianty JTAG i tak są udokumentowane w pracy jako materiał
dyskusyjny.

---

## 2. Czy sam "efekt obserwatora" JTAG jest wart osobnego wątku w pracy?

Odkrycie, że samo skompilowanie firmware z gotowym kanałem trace JTAG (bez
aktywnego przechwytu!) podnosi obciążenie CPU bardziej (+0,97pp) niż samo aktywne
przesyłanie danych (+0,69pp), jest dla nas zaskakujące i — jak sądzimy —
nietrywialne dla kogoś projektującego systemy edge z myślą o debugowaniu
sprzętowym w polu.

**Pytanie:** czy warto rozwinąć to w osobną, krótką sekcję dyskusyjną w pracy
(analogicznie do już opisanego "efektu obserwatora" w naszym głównym wątku
LLM/flagi bitowe — patrz sekcja 5.1 artykułu naukowego), czy potraktować to
wyłącznie jako uzasadnienie metodologiczne wyboru liczby z punktu 1, bez
rozwijania w osobny wątek?

---

## 3. Interpretacja "OTA Update" — czy nasza definicja jest akceptowalna?

Metodyka mówi o stanie "OTA Update — moment aktualizacji i kompilacji nowej
reguły w pamięci". Zinterpretowaliśmy to jako: urządzenie odbiera przez CAN i
zapisuje do własnej, aktywnej tabeli regułę dekodującą pojedynczy sygnał (te same
pola co reguła generowana przez LLM po stronie komputera w Eksperymencie 4.1:
indeks bajtu, maska bitowa, skala, przesunięcie) — NIE klasyczną aktualizację
całej binarki firmware przez sieć (typowe znaczenie skrótu "OTA" w kontekście
ESP32).

**Pytanie:** czy ta interpretacja (aktualizacja pojedynczej reguły interpretacji
sygnału, nie całego oprogramowania urządzenia) odpowiada temu, co metodyka miała
na myśli, czy oczekiwano dosłownie mechanizmu aktualizacji binarki (co wymagałoby
zupełnie innej, znacznie cięższej implementacji — partycji OTA, podpisywania
obrazu, itd. — i inaczej mierzonego obciążenia)?

---

## 4. Częstotliwość zdarzeń OTA — czy 10 reguł/s to sensowny wybór?

Przyjęliśmy 10 "aktualizacji reguły"/s jako parametr obciążeniowy dla pomiaru
(analogicznie do 50 ramek/s przyjętych dla stanów Idle/Parsing) — jawnie
oznaczony w kodzie i raporcie jako parametr stress-testowy do uzyskania stabilnej
statystyki w krótkim (3s) oknie pomiarowym, NIE jako literalne odwzorowanie
rzeczywistej częstotliwości aktualizacji reguł LLM w praktyce (te występowałyby
rzadziej — rzędu pojedynczych reguł na cały cykl Cold Start trwający sekundy,
zob. Eksperyment 1.1).

**Pytanie:** czy taki wybór (świadomie wyższa niż realistyczna częstotliwość, dla
celów statystycznych) jest akceptowalny, analogicznie do podobnych uproszczeń
przyjętych już wcześniej w innych eksperymentach tej pracy, czy oczekiwana jest
dodatkowa seria pomiarowa przy realistycznej (znacznie niższej) częstotliwości
zdarzeń OTA?

---

## 5. RAM/Flash identyczne między stanami — czy to satysfakcjonująca odpowiedź?

Metodyka wymaga zmierzenia zużycia RAM i Flash osobno dla każdego z trzech
stanów. W naszej architekturze tabele reguł/statystyk są przydzielane statycznie
przy starcie firmware, niezależnie od aktywnego trybu — więc zużycie RAM i Flash
wyszło identyczne dla wszystkich trzech stanów (29,50 kB / 272,4 kB).

**Pytanie:** czy to jest satysfakcjonująca, poprawna odpowiedź na wymóg metodyki
(pokazuje, że architektura NIE ma stanowo-zależnego narzutu pamięciowego — samo w
sobie wartościowa informacja), czy wykładowca oczekiwałby architektury z
dynamiczną alokacją specyficzną dla stanu (co pokazałoby różnice kosztem większej
złożoności i ryzyka fragmentacji sterty, nietypowego dla urządzeń klasy Edge)?

---

## 6. Sugestie własne (do zaakceptowania/odrzucenia przez wykładowcę)

1. **Rekomendujemy wariant (a) z punktu 1** — oryginalny wynik Arduino jako
   główna liczba metodyki, warianty JTAG jako dodatek dyskusyjny.
2. **Rekomendujemy rozwinięcie efektu obserwatora JTAG w krótką, samodzielną
   sekcję dyskusyjną** (punkt 2) — to nietrywialny, dobrze udokumentowany wynik
   uboczny, spójny z ogólnym tonem pracy (transparentne raportowanie także
   wyników niezwiązanych bezpośrednio z główną tezą).
3. **Rekomendujemy zaakceptowanie naszej interpretacji "OTA Update"** (punkt 3) —
   pełna aktualizacja binarki byłaby nieproporcjonalnie dużym nakładem pracy
   wobec wartości poznawczej dla tezy pracy, a nasza interpretacja jest zgodna z
   dosłownym brzmieniem metodyki ("kompilacja nowej reguły", nie "aktualizacja
   oprogramowania").
4. **Rekomendujemy pozostawienie częstotliwości 10 reguł/s** (punkt 4) jako
   świadomego, udokumentowanego uproszczenia — zgodnie z konwencją przyjętą już
   we wcześniejszych eksperymentach tej pracy.
