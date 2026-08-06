# Eksperyment 4.5 — uzasadnienie konieczności zakupu Raspberry Pi Zero 2 W

Data: 2026-08-06
Kontekst: uzupełnienie do `Eksperyment_4.5_Propozycja_Ciagla_Obserwacja_RaspberryPi_20260806.md`.
Ten dokument odpowiada wprost na pytanie: **dlaczego na obecnym etapie weryfikacji
hipotezy niezbędny jest zakup nowego sprzętu (Raspberry Pi Zero 2 W), zamiast
użycia już posiadanego ESP32 albo posiadanego Raspberry Pi Zero W (2017)?**

---

## 1. Dlaczego nie ESP32 (już posiadany, używany we wszystkich dotychczasowych eksperymentach)

Hipoteza Eksperymentu 4.5 (patrz dokument propozycji) wymaga urządzenia, które
potrafi **jednocześnie i w sposób CIĄGŁY** (godziny/dni, nie sekundy jednego
uruchomienia programu):

1. Utrzymywać w pamięci/na dysku historię per-bajt, per-CAN-ID, **przetrwałą
   restart programu** — ESP32 ma 320 KB RAM i brak trwałego systemu plików
   klasy ogólnego przeznaczenia (tylko flash, zwykle używany do samego
   firmware, nie do rosnącej bazy danych).
2. Uruchamiać bazę wektorową (Qdrant) i klasyfikator równolegle z nasłuchem
   magistrali — ESP32 nie ma pełnego systemu operacyjnego (Linux), nie
   uruchomi Pythona ani Qdrant w żadnej formie; cała logika musiałaby być
   napisana od zera w C++ pod mikrokontroler, tracąc bezpośrednią
   porównywalność z kodem już użytym w Eksperymentach 4.3 i 4.4 (Python).
3. **To nie jest ograniczenie, które da się obejść optymalizacją kodu** — to
   fundamentalna różnica klasy sprzętu (mikrokontroler bez OS vs komputer
   jednopłytkowy z Linuksem), zgodnie z wcześniejszą analizą
   `Analiza_ESP32_vs_RaspberryPi_OrangePi_20260727.txt`.

---

## 2. Dlaczego nie posiadany Raspberry Pi Zero W (2017, rev 1.1)

To jest kluczowy, techniczny argument, nie kwestia "nowsze zawsze lepsze":

**Posiadany Pi Zero W używa procesora ARM11 w architekturze ARMv6** — starszej
niż architektura (ARMv7/ARMv8), dla której nowoczesne narzędzia Python/ML mają
gotowe, prekompilowane pakiety binarne. Konkretnie:

- `qdrant-client` (biblioteka użyta w Eksperymencie 4.4) w trybie lokalnym
  korzysta z komponentów wymagających skompilowanych zależności (m.in.
  fragmenty w Rust) — **oficjalne kanały dystrybucji pakietów Python
  (w tym `piwheels`, używany przez Raspberry Pi OS) nie gwarantują pokrycia
  architektury ARMv6** dla tej klasy bibliotek, w odróżnieniu od ARMv7
  (Pi Zero 2 W, Pi 3/4) i ARM64.
- Ryzyko: próba uruchomienia identycznego kodu z Eksperymentu 4.4 na Pi Zero W
  (2017) może zakończyć się błędem instalacji/braku wsparcia platformy, **nie
  wynikiem naukowym** — testowalibyśmy wtedy zgodność architektury procesora,
  nie hipotezę badawczą.
- Nawet gdyby się udało (np. przez budowanie zależności ze źródeł, znacznie
  bardziej czasochłonne), pojedynczy rdzeń ARM11 @1GHz musiałby dzielić czas
  między nasłuch CAN w czasie rzeczywistym, klasyfikator i zapytania Qdrant —
  realne ryzyko utraty ramek CAN pod obciążeniem, co zafałszowałoby wynik
  (nie wiedzielibyśmy, czy słaby wynik to cecha hipotezy, czy przeciążony
  procesor).

**Wniosek: użycie posiadanego Pi Zero W (2017) do tego konkretnego testu
wprowadza zmienną zakłócającą (architektura/wydajność sprzętu), której
istnienia nie dałoby się odróżnić od wyniku merytorycznego.**

---

## 3. Dlaczego Raspberry Pi Zero 2 W, nie Orange Pi Zero

Orange Pi Zero byłby tańszą alternatywą sprzętową, ale — zgodnie z wcześniejszą
analizą (`Analiza_ESP32_vs_RaspberryPi_OrangePi_20260727.txt`) — ma **gorsze
wsparcie i odtwarzalność** (system Armbian, utrzymywany społecznościowo, nie
przez producenta, w odróżnieniu od oficjalnego Raspberry Pi OS). Dla
eksperymentu naukowego, gdzie **reprodukowalność metodologii** jest jednym z
kluczowych kryteriów jakości (patrz `Eksperyment_4.3_Uzasadnienie_i_Pytania...md`,
sekcja 4, punkt 4), ryzyko niestandardowego środowiska systemowego przeważa nad
oszczędnością rzędu pojedynczych dziesiątek złotych.

**Raspberry Pi Zero 2 W** (czterordzeniowy Cortex-A53, architektura ARMv8/ARM64,
wbudowane WiFi/BT, oficjalne wsparcie Raspberry Pi OS) usuwa WSZYSTKIE trzy
problemy jednocześnie:
- pełna zgodność z nowoczesnymi pakietami Python/ML (ta sama architektura co
  większość testowanych/dokumentowanych środowisk),
  bez ryzyka "testowania architektury procesora zamiast hipotezy",
- wystarczająca moc obliczeniowa (4 rdzenie) do jednoczesnego nasłuchu CAN,
  klasyfikatora i zapytań Qdrant bez utraty ramek,
- oficjalne wsparcie producenta — ta sama jakość odtwarzalności co reszta
  projektu (Raspberry Pi OS, nie systemy społecznościowe).

---

## 4. Koszt i uzasadnienie proporcjonalności wydatku

Raspberry Pi Zero 2 W kosztuje rzędu 60-80 zł (przy obecnych cenach rynkowych) —
niewielki wydatek w porównaniu do już poniesionych kosztów projektu (API LLM,
sprzęt CAN). Bez tego zakupu Eksperyment 4.5 nie da wiarygodnej odpowiedzi na
postawioną hipotezę — wynik byłby metodologicznie wątpliwy (nie wiadomo, czy
słaby wynik na Pi Zero W (2017) wynika z hipotezy czy z niedopasowania
architektury sprzętu do zadania), co czyni ten zakup **niezbędnym, nie
opcjonalnym ulepszeniem** na obecnym etapie.

---

## 5. Dodatkowa uwaga praktyczna (poza zakresem naukowym)

Niezależnie od wyboru płytki, potrzebny będzie **drugi czytnik kart microSD**
(obecny w laptopie roboczym czytnik jest zajęty kartą, z której działa bieżąca
sesja robocza) — koszt rzędu 10-20 zł, jednorazowy, niezależny od tego, ile
kolejnych kart SD trzeba będzie w przyszłości flashować.
