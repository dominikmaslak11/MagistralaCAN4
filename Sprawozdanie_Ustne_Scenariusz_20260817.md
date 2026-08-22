# Sprawozdanie ustne — scenariusz rozmowy z wykładowcą

Data przygotowania: 2026-08-17
Zakres: Eksperymenty 4.5 – 4.12 (6–17 sierpnia 2026)
Przewidywany czas: **12–15 minut** wystąpienia + pytania

---

## 0. Zasada nadrzędna

**Nie relacjonuj chronologicznie, co robiłeś.** Odpowiedz na pytanie, które
zostało otwarte 6 sierpnia. Wykładowca pamięta tamten dokument — nawiązanie do
niego od pierwszego zdania pokazuje ciągłość myślenia, a nie zbieranie wyników.

Druga zasada: **sam zgłaszaj słabości, zanim padnie pytanie.** Masz w tej sesji
trzy odrzucone hipotezy i jeden wycofany wniosek. To nie są wstydliwe potknięcia,
tylko najmocniejszy dowód, że pomiary były uczciwe. Student, który sam mówi
„tu się myliłem i tak to naprawiłem", jest wiarygodniejszy niż taki, który ma
same sukcesy.

---

## 1. Otwarcie (30 sekund — naucz się na pamięć)

> „6 sierpnia zgłosiliśmy, że klasyfikator flag bitowych na szerszym korpusie
> spada do Recall 55,9% i trafności maski 15,8%. Zdiagnozowaliśmy wtedy dwie
> przyczyny — za krótkie okno obserwacji i bajty mieszane — i zostawiliśmy
> pytanie, czy naprawiać klasyfikator przed fine-tuningiem, czy uczyć na danych
> z ~44% błędnych etykiet. **Naprawiliśmy obie przyczyny. Etap D jest odblokowany.**"

Potem pauza. To zdanie ma wystarczyć, żeby wykładowca wiedział, o czym będzie
reszta rozmowy.

---

## 2. Trzon: dwie przyczyny, dwa rozwiązania (6–7 minut)

### Przyczyna (a): za krótkie okno obserwacji

**Rozwiązanie:** ciągła obserwacja zamiast epizodycznej — demon działający
godzinami zamiast pojedynczego uruchomienia programu.

| Etap | Recall | Precision |
|---|---|---|
| korpus szeroki, 6 sierpnia | 55,9% | 82,6% |
| ciągła obserwacja + strojenie progu | 85,0% | 100% |
| z klasyfikatorem uczonym | **100%** | 97,3% |

**Zastrzeżenie, które musisz podać sam:** to nie są te same korpusy, więc nie
jest to czysty ciąg jednej metryki. Wspólny mianownik jest taki, że każdy kolejny
krok mierzono na trudniejszych, nie łatwiejszych danych.

### Przyczyna (b): bajty mieszane

Kluczowe odkrycie: problem był źle rozumiany. Maska `seen0 & seen1` nie jest
„ogólnie kiepska" — jest **idealna dla bajtów z samymi flagami (100%) i ma
dokładnie zero trafień dla mieszanych**. Bajty mieszane to ~22% przypadków.

Zdanie, które warto wypowiedzieć dosłownie:

> „Gdybym raportował samą średnią, wyszłoby 82% i wyglądałoby to na drobną
> niedoskonałość — zamiast na całkowitą bezradność w co piątym przypadku.
> Dlatego ewaluacja rozbija oba typy osobno."

Pierwszy wynik: sieć per bit osiągnęła **30/30 = 100%** na bajtach mieszanych.

---

## 2b. NAJWAŻNIEJSZY FRAGMENT ROZMOWY — sam podważ własny wynik

**To jest miejsce, w którym możesz zyskać najwięcej.** Nie czekaj na pytanie.

> „Ten wynik 100% mnie zaniepokoił, więc sprawdziłem, czy sieć nauczyła się
> prawdziwej zasady, czy regularności mojego generatora. W korpusie flagi
> siedziały zawsze na najniższych bitach, a skalar tworzył ciągły zakres nad
> nimi. Rozproszyłem bity i powtórzyłem pomiar na sprzęcie."

| Test | Maska w bajtach mieszanych |
|---|---|
| bity ciągłe (pierwotny korpus) | **100%** |
| bity rozproszone, **ten sam model** | **14%** |
| bity rozproszone, model uczony na reprezentatywnych danych | **59%** |

> „Model nie generalizował. Nauczył się w dużej mierze układu pozycji, nie
> zasady. **Wynik 100% był zawyżony przez konstrukcję mojego korpusu.**
> Po przeuczeniu na danych reprezentatywnych wychodzi 59% — wciąż dużo wobec
> zera, ale to nie jest problem rozwiązany."

**Uczciwy headline, którego się trzymaj:**

| Metryka | odniesienie | sieć |
|---|---|---|
| Precyzja per bit | 26,8% | **96,5%** |
| F1 per bit | 42,3% | **98,2%** |
| Maska w bajtach mieszanych | 0% | **59%** |

Dlaczego to jest **mocniejsze** niż „mam 100%": pokazujesz, że sam szukasz
dziur we własnych wynikach i znajdujesz je, zanim zrobi to ktoś inny.
Gdyby wykładowca odkrył ten artefakt sam, cała reszta stałaby się podejrzana.

Zdanie na zakończenie tego wątku:

> „Nauczyło mnie to, że zastrzeżenie zapisane pod wynikiem trzeba **sprawdzić**,
> a nie tylko odnotować. Sprawdzenie zajęło jedno popołudnie i obaliło główną
> liczbę tamtego eksperymentu."

## 3. Dlaczego akurat sieć neuronowa (2 minuty)

Wykładowca **na pewno** zapyta, czy to nie jest ML dla samego ML. Masz na to
twardą odpowiedź, bo sprawdziłeś to celowo — tym samym protokołem, próg dobierany
na danych uczących, oceniany na sprzęcie:

| Podejście | F1 | maska w bajtach mieszanych |
|---|---|---|
| najlepszy pojedynczy próg | 57,8% | 10% |
| najlepsza koniunkcja dwóch cech | 81,9% | 57% |
| **sieć (11 cech, 1105 parametrów)** | **99,9%** | **100%** |

**Uwaga — te liczby pochodzą z korpusu ciągłego** (przed korektą z sekcji 2b).
Jeśli wykładowca zapyta o wersję rozproszoną: sieć osiąga tam F1 98,2% wobec
42,3% odniesienia, więc **przewaga nad regułą pozostaje**, choć maska spada
do 59%. Porównania „próg kontra para cech kontra sieć" nie powtórzono na
korpusie rozproszonym — to uczciwie zadanie otwarte, nie wynik.

I kontrapunkt, który pokazuje, że nie jesteś zakochany w sieciach:

> „W poprzednim eksperymencie sprawdziłem to samo i wyszło odwrotnie — tam
> zmiana **jednej stałej** w regule dawała +5 punktów F1, prawie tyle co sieć.
> Dlatego rekomendowałem zmianę stałej, nie sieć. Dopiero przy masce bitowej
> żadna prosta reguła nie wystarcza."

To jest różnica między „użyłem ML" a „sprawdziłem, kiedy ML jest potrzebny".

---

## 4. Co się nie udało — zgłoś sam (2–3 minuty)

### Trzy próby testu odporności, hipoteza odrzucona

Hipoteza: reguła strojona w jednym punkcie pracy powinna degradować się poza nim
szybciej niż sieć uczona na wielu konfiguracjach.

| Próba | Zmienna | Czy zmieniła statystyki? |
|---|---|---|
| 4.8 | prędkość magistrali 125–1000 kbit/s | **nie** — błąd projektowy |
| 4.9 | liczba CAN ID 5–60 | **nie** — błąd projektowy |
| 4.10 | skala okresów ramek 0,25–4× | **tak, 15×** |

Dwie pierwsze próby manipulowały parametrami **poziomu magistrali**, podczas gdy
statystyki per-bajt zależą od **stosunku częstotliwości próbkowania do dynamiki
sygnału**. Dopiero trzecia zmienna to zmienia.

Wynik przy działającej manipulacji: **hipoteza odrzucona** — wszystkie metody
okazały się bardzo stabilne (rozstępy 1,2–2,5 pp), a najstabilniejsza była
*reguła*, nie sieć.

Wniosek, który warto podać jako pozytywny:

> „Przewaga sieci wynika wyłącznie z jakości klasyfikacji, nie z odporności —
> i tak to opisuję. Za to wyszedł wynik uboczny, który jest praktycznie cenny:
> metoda nie wymaga strojenia pod punkt pracy."

### Wycofany wniosek

Na małym teście (132 pozycje) wyszło, że reguła bije sieć — i tak to zapisałem.
Przy próbie 22× większej kierunek się odwrócił. Różnica opierała się na **dwóch
pozycjach**, czyli na szumie.

> „Nauczyło mnie to, że przy 20 pozytywach w zbiorze testowym różnice rzędu
> kilku punktów procentowych nie są interpretowalne. Powtórzyłem pomiar,
> zanim ktokolwiek to zakwestionował."

---

## 5. Największa słabość — powiedz to sam (30 sekund)

> „Cały ruch jest syntetyczny, z naszego generatora. Test na innych ziarnach
> ogranicza ryzyko zapamiętywania, ale inne ziarno to wciąż ten sam generator.
> Traktuję te wyniki jako mocną przesłankę, **nie dowód** na prawdziwej
> magistrali."

Zastrzeżenie o ciągłych zakresach bitów **zostało już sprawdzone** (sekcja 2b)
i potwierdziło się — to podnosi wiarygodność pozostałych zastrzeżeń, bo
pokazuje, że nie są kurtuazyjne.

Jeśli tego nie powiesz Ty, powie to wykładowca — i wtedy zabrzmi jak zarzut,
a nie jak świadomość ograniczeń.

---

## 6. Wątek platformy sprzętowej (1–2 minuty, jeśli będzie czas)

Warto, bo to odpowiada na wcześniejszą dyskusję ESP32 vs SBC:

Powtórzyliśmy Eksperyment 4.5 na **drugiej, niezależnej platformie** (Orange Pi
Zero 3: inny procesor, inna architektura, inny system, inna magistrala SPI).
Wynik **identyczny co do liczby**: Recall 85,0%, Precision 100%.

> „To znaczy, że wniosek jest własnością **metody**, a nie konkretnej płytki."

Dodatkowo: Faza 5 (embeddingi neuronowe) po raz pierwszy ruszyła na sprzęcie
docelowym — na Raspberry Pi Zero W było to niewykonalne, bo PyTorch nie ma
pakietów dla ARMv6. Wyniki zgodne co do ostatniej cyfry z laptopem x86.

Koszt integracji, uczciwie: Armbian nie ma gotowego overlaya dla MCP2515,
dokumentacja pinoutu w sieci jest sprzeczna. Trzeba było napisać własny overlay
i rozstrzygnąć pinout oficjalnym manualem plus odczytem z device tree.

---

## 7. Pytania, które padną — i odpowiedzi

| Pytanie | Odpowiedź |
|---|---|
| **Czy 100% to nie przeuczenie?** | **Częściowo tak — sam to wykryłem.** Po usunięciu regularności generatora wynik spadł do 14%. Po przeuczeniu na danych reprezentatywnych: 59%. Uczciwa liczba to 59%, nie 100%. |
| **Skąd wiecie, że 59% to nie ten sam artefakt?** | Bo test przeprowadzono na korpusie, w którym pozycje flag są losowe, a bity skalara przeplecione — czyli usunięto obie regularności, które mogły pomagać. Zostaje pytanie o inne, nierozpoznane jeszcze regularności; stąd walidacja na prawdziwej maszynie jako następny krok. |
| **Dlaczego sieć, a nie reguła?** | Sprawdziłem regułę tym samym protokołem: 57,8% (jeden próg), 81,9% (dwie cechy), 99,9% (sieć). W poprzednim eksperymencie ta sama analiza wskazała **regułę**, nie sieć. |
| **Ile to kosztuje na urządzeniu brzegowym?** | 1105 parametrów, uczenie 68 s na płytce, inferencja 282 µs, wdrożenie **bez PyTorcha** — wagi w JSON, kilkanaście linii czystego Pythona. Pamięć pozostaje stała. |
| **Czy to zadziała na prawdziwej maszynie?** | Nie wiem i nie twierdzę, że wiem. To najbliższy krok: zrzut z prawdziwej maszyny rolniczej. |
| **Po co to komu?** | Sygnał „narzędzie pracuje" w maszynie rolniczej nie jest standaryzowany w J1939 — jest producencki i binarny, czyli jest flagą bitową w nieznanej ramce. Dokładnie ten problem. |
| **Czemu nie użyliście gotowego DBC?** | Do paliwa i motogodzin **należy** użyć — to standardowe PGN-y J1939. Cała ta praca dotyczy tego jednego sygnału, którego w standardzie nie ma. |
| **Ile danych?** | ~7600 pozycji bajtowych i 57 280 bitów, wszystko z prawdziwego kontrolera MCP2515, w trzech niezależnych zbiorach. |

---

## 8. Liczby do zapamiętania (tylko te)

Nie ucz się tabel. Zapamiętaj **sześć liczb**:

- **55,9% → 100%** — Recall wykrywania flag, od zgłoszenia z 6 sierpnia do dziś
- **0% → 59%** — maska w bajtach mieszanych, po korekcie (**nie mów 100%**)
- **26,8% → 96,5%** — precyzja per bit; to jest najuczciwsza miara poprawy
- **100% → 14%** — spadek modelu z 4.11 po usunięciu regularności generatora
- **22%** — udział bajtów mieszanych, czyli skala problemu
- **1105 parametrów, 68 sekund** — koszt na urządzeniu brzegowym
- **3 odrzucone hipotezy + 1 obalony własny wynik** — uczciwość pomiarów

---

## 9. Zamknięcie i pytania do wykładowcy (1 minuta)

Zakończ **decyzjami do podjęcia**, nie podsumowaniem. To zamienia sprawozdanie
w konsultację:

1. **Etap D (fine-tuning) jest odblokowany** — klasyfikator-nauczyciel nie jest
   już wąskim gardłem. Czy ruszać, czy najpierw walidacja na prawdziwej maszynie?
2. **Czy wdrażać sieć jako domyślny klasyfikator**, czy zostawić regułę
   z poprawioną stałą (+5 pp, zero kosztu obliczeniowego) i sieć jako opcję?
3. **Czy rozszerzenie o drugą platformę sprzętową** (Orange Pi jako węzeł
   porównawczy) mieści się w zakresie pracy, czy rozmywa tezę o mikrokontrolerze?
4. **Czy walidacja na maszynie rolniczej** to osobny eksperyment w tej pracy,
   czy materiał na kolejną?

---

## 10. Czego NIE mówić

- **Nie mów „mam 100% trafności maski".** Ta liczba została obalona własnym
  testem — mów 59%, i opowiedz, jak do tego doszło.
- **Nie mów „sieć rozpoznaje ramki CAN z dokładnością 100%".** To nieprawda
  i wykładowca to rozbierze. Sieć odpowiada na dwa wąskie pytania: który bajt
  zawiera flagi i które bity nimi są. Semantyka — co sygnał znaczy — pozostaje
  nierozwiązana (LLM: 3–24%).
- **Nie porównuj wyników z Eksperymentem 4.1** (dekodowanie przez LLM). To inne,
  znacznie trudniejsze zadanie i zestawienie 99% z 24% byłoby manipulacją.
- **Nie obiecuj, że zadziała na maszynie.** Powiedz, że to następny krok.
- **Nie ukrywaj trzech nieudanych prób.** One są argumentem za Tobą.

---

## Materiały na spotkanie

| Plik | Do czego |
|---|---|
| `Sprawozdanie_Ustne_Infografika_20260817.pdf` | jedna kartka na stół — cała narracja wizualnie |
| `Eksperyment_4.11-4.12_Maska_Bitowa_20260817.md` | szczegóły + korekta wyniku (sekcja 5b) |
| `Eksperyment_4.7-4.10_Siec_Neuronowa_vs_Regula_20260815.md` | protokół, odrzucone hipotezy |
| `Eksperyment_4.6_Replikacja_OrangePiZero3_20260814.md` | wątek platformy |

Wszystko jest w repozytorium na GitHubie (commit `5b9e2ff`), więc możesz odesłać
do źródeł, jeśli padnie pytanie o odtwarzalność.
