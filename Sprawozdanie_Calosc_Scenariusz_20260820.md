# Sprawozdanie z CAŁOŚCI projektu — scenariusz wideorozmowy

Data: 2026-08-20
Zakres: wszystkie eksperymenty projektu CAN-Edge-AI (MagistralaCAN4), lipiec–sierpień 2026
Czas: **15–20 minut** + pytania

---

## CZĘŚĆ 0: Zanim zaczniesz mówić — o czym w ogóle jest ten projekt

Przeczytaj to raz, powoli. Reszta scenariusza zakłada, że to rozumiesz.

**Problem:** magistrala CAN w pojeździe przesyła ramki, w których zakodowane są
sygnały — obroty silnika, temperatura, ale też pojedyncze **flagi bitowe**
(kierunkowskaz włączony, drzwi otwarte). Żeby je odczytać, trzeba znać regułę
dekodowania. Producenci jej nie udostępniają.

**Pomysł:** niech regułę odgadnie model językowy (LLM), obserwując surowe ramki.

**Co się okazało:** LLM-y radzą sobie dobrze z sygnałami ciągłymi (79–100%
skuteczności), ale **fatalnie z flagami bitowymi**. Claude nie wykrył ani jednej
flagi w 100 próbach. To jest oś całego projektu.

**Odpowiedź projektu:** skoro model nie radzi sobie z flagami, dołóżmy do niego
klasyczny klasyfikator, który się na tym zna — i pozwólmy mu **nadpisać** decyzję
modelu tam, gdzie model jest słaby. To zadziałało: 50% → 96,7%.

**Reszta projektu** to (a) sprawdzenie, czy sprzęt w ogóle udźwignie takie
rozwiązanie, i (b) ulepszanie tego klasyfikatora.

---

## CZĘŚĆ 1: Struktura wystąpienia — pięć grup eksperymentów

Mów w tej kolejności. Każda grupa odpowiada na inne pytanie.

| Grupa | Pytanie | Odpowiedź |
|---|---|---|
| **1. Czasy** | Ile trwa reakcja systemu? | Wnioskowanie LLM dominuje wszystko |
| **2. Przepustowość** | Czy ESP32 nadąża za magistralą? | Tak, z zapasem — 0% strat |
| **3. Łączność** | Jaki zasięg radiowy? | **Przygotowane, nie zmierzone** |
| **4. Trafność LLM** | Czy model odczyta nieznane sygnały? | Nie dla flag — stąd hybryda |
| **5. Zasoby** | Ile CPU/RAM to kosztuje? | Mało, ale pomiar zaburza pomiar |

---

## CZĘŚĆ 2: Grupa 1 — czasy reakcji (2 minuty)

### Eksperyment 1.1 — Cold Start, czyli ile czeka się na LLM

Pomiar na realnym sprzęcie: ESP32+MCP2515 → WiFi → aplikacja C++/Qt6. N=30 na model.

| Model | Czas odpowiedzi LLM | Czas całkowity |
|---|---|---|
| Claude Sonnet 5 | 4741 ±1305 ms | 5772 ms |
| GPT-5.6-sol | 4889 ±1111 ms | 5866 ms |
| Gemini-3.6-flash | 8522 ±3321 ms | 9488 ms |
| DeepSeek-v4-pro | 18642 ±1352 ms | 19550 ms |

**Wniosek:** czas wnioskowania LLM stanowi **ponad połowę** czasu całkowitego
u każdego modelu. To uzasadnia całą dalszą pracę: optymalizowanie sprzętu nie ma
sensu, dopóki wąskim gardłem są sekundy oczekiwania na model.

### Eksperyment 1.2 — Hot Execution, czyli reakcja na znaną już regułę

| Metoda pomiaru | Średnia | Odch. std. | N |
|---|---|---|---|
| Timer wewnętrzny ESP32 | **109,70 µs** | 1,52 µs | 1000 |
| Analizator logiczny (niezależnie) | **112,45 µs** | 1,54 µs | 570 |

**To jest mocny punkt — powiedz o nim.** Dwie **całkowicie niezależne** metody
dały zgodny wynik (różnica 2,5%). Wcześniej dwie próby weryfikacji się nie
udały: oscyloskop Hantek miał zawodne wyzwalanie, analizator DL16 miał trwale
niesprawny firmware. Trzecie podejście zamknęło wątek.

**Kluczowa proporcja do zapamiętania: 5 sekund kontra 110 mikrosekund.**
Różnica pięciu rzędów wielkości między poznawaniem reguły a jej stosowaniem.

---

## CZĘŚĆ 3: Grupa 2 — przepustowość (1,5 minuty)

### Eksperyment 2.1 — czy ESP32 gubi ramki

| Prędkość | Zakres testu | Sufit magistrali | Straty |
|---|---|---|---|
| 250 kbit/s | 100–5000 ramek/s | ~2260 ramek/s | **0,00%** |
| 500 kbit/s | 100–5000 ramek/s | ~4365 ramek/s | **0,00%** |

**Ani jedna zgubiona ramka** w całym zakresie, na obu prędkościach. Sprzętowy
licznik przepełnień MCP2515 też został na zerze.

Zdanie warte wypowiedzenia:

> „Wąskim gardłem okazała się **sama magistrala**, nie mikrokontroler.
> ESP32 nigdy nie zdradził oznak przeciążenia."

### Eksperyment 2.2 — kiedy przepełni się bufor

Potwierdzono empirycznie zależność **T = rozmiar_bufora / częstotliwość**.
Przy buforze 16 ramek i 1000 Hz margines to 16 ms — a na odpowiedź LLM czeka
się ~2,2 s.

**Znaleziona luka:** obecna architektura **nie ma bufora aplikacyjnego** — każda
ramka idzie od razu przez WebSocket. To brak w **oprogramowaniu**, nie
ograniczenie sprzętu: ESP32 ma ~270 KB wolnej pamięci, a potrzeba ~35 KB.

---

## CZĘŚĆ 4: Grupa 4 — trzon projektu, trafność LLM (5–6 minut)

To jest najważniejsza część. Poświęć jej najwięcej czasu.

### 4.1 — punkt wyjścia: cztery modele, ten sam problem

N=100 na model.

| Model | Wszystkie sygnały | **Flagi bitowe** |
|---|---|---|
| GPT-5.6-sol | 67,9% | 35,8% |
| Gemini-3.6-flash | 50,9% | 3,0% |
| Claude Sonnet 5 | 47,9% | **0,0%** |
| DeepSeek-v4-pro | 41,5% | 6,7% |

Sygnały ciągłe: 79–100%. Flagi: katastrofa. **Claude nie wykrył ani jednej flagi
w stu próbach.**

### 4.2 i 4.3 — cztery próby naprawy przez prompt, wszystkie nieudane

| Interwencja | Efekt na flagi |
|---|---|
| Few-shot (przykłady w promptcie) | 0,0% — bez zmian |
| Wymuszona analiza entropii | 0,0% — bez zmian |
| Naprawa zanieczyszczenia kontekstu | 0,0% — bez zmian |
| …a u GPT-5.6-sol | **regresja −35,8 pp** |

**To są wyniki negatywne i trzeba je przedstawić jako wynik, nie porażkę:**

> „Cztery niezależne interwencje — przykłady, wymuszone procedury, poprawa
> jakości danych wejściowych — dały identyczne 0,0%. To sugeruje, że problem
> nie leży w tym, **co** mówimy modelowi ani jak czysty jest kontekst, tylko
> w tym, że modele zawodnie wykonują systematyczną analizę bitową na surowych
> liczbach, nawet gdy wprost o to poprosić."

**Nieoczekiwana regresja GPT** po naprawie kontekstu jest ciekawa: liczba prób,
w których model w ogóle zaproponował rozbicie bajtu na osobne sygnały, spadła
z 12/33 do **0/33**. Robocza hipoteza: przypadkowy szum z innych ramek
(przed naprawą) zwiększał pozorną złożoność danych i skłaniał model do
rozważenia bardziej złożonej interpretacji. Czysty kontekst wyglądał na prostszy.

### 4.5 — hybrydowy override, pierwszy pozytywny wynik

Klasyczny klasyfikator wykrywa bajty wyglądające na flagi i **nadpisuje** decyzję
modelu tylko tam.

| | Surowy LLM | LLM + override |
|---|---|---|
| Średnia (10 sygnałów) | 50,0% | **96,7%** |
| Flagi bitowe (5 sygnałów) | F1 = 0 | **F1 = 1,000** |
| Sygnały ciągłe | bez zmian | **bez pogorszenia** |

**Dlaczego to działa, a prompt nie:** override nie prosi modelu, żeby zrobił coś,
czego nie umie — tylko odbiera mu tę konkretną decyzję i oddaje ją narzędziu,
które się do niej nadaje. Model dalej robi to, w czym jest dobry.

---

## CZĘŚĆ 5: Rozwinięcia klasyfikatora — sierpień (3–4 minuty)

Gdy override zadziałał, przedmiotem pracy stał się **sam klasyfikator**.

### Co zgłoszono 6 sierpnia

Na szerszym korpusie klasyfikator wypadł znacznie gorzej niż na wąskim:
**Recall 55,9%**, trafność maski bitowej **15,8%**. Zdiagnozowano dwie przyczyny:
za krótkie okno obserwacji i bajty mieszane (flaga + skalar w jednym bajcie).
Zostawiono wykładowcy pytanie: naprawiać klasyfikator, czy uczyć fine-tuning
na danych z ~44% błędnych etykiet?

### Co zrobiono od tamtej pory

**Przyczyna (a) — okno obserwacji:** ciągła obserwacja zamiast epizodycznej.
Recall 55,9% → 85% → **100%** (z klasyfikatorem uczonym).

**Replikacja na drugiej platformie:** ten sam kod na Orange Pi Zero 3 (inny
procesor, architektura, system) dał wynik **identyczny co do liczby**.
To dowód, że wniosek jest własnością **metody**, nie płytki.

**Przyczyna (b) — bajty mieszane:** sieć neuronowa klasyfikująca **pojedyncze
bity**. Precyzja per bit 26,8% → **96,5%**.

### I tu najważniejsze zdanie całego wystąpienia

> „Pierwszy wynik pokazał 100% trafności maski. Uznałem to za podejrzane
> i sprawdziłem, czy sieć nauczyła się prawdziwej zasady, czy regularności
> mojego generatora. Okazało się, że **regularności** — po rozproszeniu bitów
> wynik spadł ze 100% do 14%. Po przeuczeniu na reprezentatywnych danych
> wychodzi 59%. **Obaliłem własny wynik.**"

**Nie mów 100%.** Uczciwe liczby: **0% → 59%** dla maski, **26,8% → 96,5%**
dla precyzji per bit.

---

## CZĘŚĆ 6: Grupy 3 i 5 — krótko (1,5 minuty)

### Eksperyment 3.1 — zasięg radiowy: PRZYGOTOWANY, NIE ZMIERZONY

Gotowe: firmware ESP32 jako punkt dostępowy (ping-pong UDP), **pierwsza w
projekcie aplikacja mobilna** (Kotlin) mierząca RTT, RSSI i GPS, serwer odbiorczy.

**Pomiar terenowy czeka na Twoje odpowiedzi** — spisano 10 kwestii
metodologicznych przed napisaniem kodu (m.in. SoftAP kontra router pośredniczący,
liczebność próby 1000–2000 zamiast 10 000 pakietów na punkt).

To jest miejsce, gdzie **prosisz o decyzję**, a nie raportujesz wynik.

### Eksperyment 5.1 — profilowanie CPU i efekt obserwatora

Zużycie CPU przy parsowaniu ramek, rozłożone na składowe:

| Konfiguracja | CPU |
|---|---|
| Firmware Arduino (wyjściowy) | 0,60% |
| Port na ESP-IDF | 0,82% |
| Sama **gotowość** kanału trace | 1,79% |
| Aktywne śledzenie przez JTAG | 2,49% |

**Wynik metodologicznie ciekawy:** samo przygotowanie kanału pomiarowego —
jeszcze przed rozpoczęciem pomiaru — kosztuje więcej niż mierzona praca.
Klasyczny problem „obserwator wpływa na obserwowane zjawisko".

Jako wartość odniesienia przyjęto **0,6%** — pomiar najmniej inwazyjny.

Zrealizowano też trzeci stan metodyki: **aktualizacja OTA**.

---

## CZĘŚĆ 7: Zamknięcie — co z tego wynika (1 minuta)

Trzy zdania na koniec:

1. **Sprzęt nie jest problemem.** Zero zgubionych ramek, 110 µs reakcji, 0,6% CPU.
   Wąskim gardłem jest wnioskowanie LLM — sekundy wobec mikrosekund.
2. **Prompt engineering ma twardy sufit.** Cztery interwencje, cztery razy 0,0%.
   Hybryda klasyczna+LLM przeskoczyła to od razu: 50% → 96,7%.
3. **Ulepszanie klasyfikatora trwa i ma zmierzone granice.** Maska bitowa
   0% → 59%, po uczciwej korekcie własnego, zawyżonego wyniku.

---

## CZĘŚĆ 8: Pytania, które padną

| Pytanie | Odpowiedź |
|---|---|
| **Czy to działa na prawdziwym samochodzie?** | Nie wiem. Cały ruch jest syntetyczny, z własnego generatora. To najbliższy krok i największe ograniczenie. |
| **Dlaczego akurat te 4 modele?** | Dwa komercyjne wiodące (Claude, GPT), jeden tani (Gemini flash), jeden otwarty (DeepSeek). N=100 prób na model. |
| **Czemu Claude ma 0% na flagach, skoro jest dobry?** | Bo to nie jest kwestia „jakości" modelu, tylko rodzaju zadania — systematycznej analizy bitowej na surowych liczbach. GPT też spadł do 0% po zmianie kontekstu. |
| **Czy override to nie oszustwo?** | Nie, bo nie podpowiada odpowiedzi — wykrywa **typ** sygnału metodą statystyczną i przejmuje decyzję tylko dla tego typu. LLM dalej dekoduje wszystko inne. |
| **Ile to kosztowało w API?** | Warto mieć tę liczbę pod ręką — jeśli nie znasz, powiedz, że sprawdzisz. Nie zgaduj. |
| **Co dalej?** | Trzy rzeczy: pomiar terenowy 3.1 (czeka na Pana decyzję), walidacja na prawdziwej maszynie, fine-tuning (Etap D — odblokowany, bo klasyfikator-nauczyciel przestał być wąskim gardłem). |

---

## CZĘŚĆ 9: Liczby, które musisz znać na pamięć

Sześć liczb. Reszta jest na tle.

- **5 sekund kontra 110 mikrosekund** — poznanie reguły kontra jej stosowanie
- **0,00%** — zgubionych ramek w całym zakresie testów
- **0,0%** — detekcja flag przez Claude, niezmienna mimo czterech interwencji
- **50% → 96,7%** — efekt hybrydowego override
- **0% → 59%** — maska bitowa, po korekcie własnego wyniku
- **0,6%** — zużycie CPU przy parsowaniu

---

## CZĘŚĆ 10: Czego nie mówić

- **Nie mów, że coś działa na prawdziwym pojeździe.** Nie było testowane.
- **Nie mów „100% trafności maski"** — ta liczba została obalona własnym testem.
- **Nie mów, że eksperyment 3.1 jest zrobiony** — jest przygotowany, pomiar czeka.
- **Nie zgaduj kosztów API ani dat.** Lepiej „sprawdzę i odpiszę".
- **Nie ukrywaj wyników negatywnych** — cztery nieudane interwencje promptowe
  to nie wstyd, tylko główny argument za hybrydą.
