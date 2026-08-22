# Eksperymenty 4.7–4.10 — sieć neuronowa kontra reguła ręczna

## 1. Co badano — wyjaśnienie dla osoby spoza tematu

Klasyfikator flag ma zapisaną na sztywno zasadę: „bajt zawiera flagi, jeśli
przełącza się **od 2 do 6** z ośmiu bitów, a zmiany są skokowe".

Pomiar godzinny ujawnił, że ta zasada ma **sufit nie do przeskoczenia**: bajty
zawierające **7 lub 8** flag są odrzucane z definicji, niezależnie od
jakiegokolwiek dostrajania. Ich współczynniki skokowości (0,97 / 0,84 / 0,82)
przeszłyby każdy próg — odrzuca je sam warunek liczby bitów.

Podniesienie limitu do 8 daje wykrywalność 100 %, ale precyzja spada do ~50 %,
bo bajt, w którym zmieniają się wszystkie bity, jest **nieodróżnialny** od
szybko zmieniającej się liczby.

**Pytanie: czy uczony klasyfikator poradzi sobie tam, gdzie prosta zasada nie może?**

## 2. Czym i jak to zrobiono

**Sieć:** świadomie mała — 10 cech wejściowych → 24 → 12 → 1 wyjście,
**577 parametrów**. Wszystkie cechy liczone przyrostowo, żeby dało się ją
wdrożyć w istniejącym programie bez zmiany zużycia pamięci.

**Protokół (istotny dla wiarygodności):**

| Zbiór | Źródło | Rola |
|---|---|---|
| uczący | ziarna 1–24, wirtualne magistrale | uczenie |
| walidacyjny | ziarna 25–30, wirtualne magistrale | wybór momentu zakończenia uczenia |
| **testowy** | **prawdziwy MCP2515** | **jedyne raportowane liczby** |

**Pułapka wykryta i ominięta:** początkowo dane uczące miały pochodzić z trybu
symulacji offline generatora. Sprawdzono to, uruchamiając na nich **regułę
ręczną**, której zachowanie na żywej magistrali jest znane:

| Źródło danych | Recall | Precision |
|---|---|---|
| symulacja offline | 42,6 % | 74,3 % |
| **wirtualna magistrala (realne taktowanie)** | **85,0 %** | **100 %** |
| prawdziwy MCP2515 | 85,0 % | 100 % |

Symulacja offline ma **inny rozkład danych**. Sieć nauczona na niej wyglądałaby
dobrze na walidacji i zawiodłaby przy wdrożeniu.

## 3. Pliki wynikowe

| Plik | Co zawiera | Pochodzenie |
|---|---|---|
| `wyniki_4.8_predkosci_magistrali.csv` | wyniki przy zmiennej prędkości magistrali | **przeliczone** z surowych korpusów |
| `wyniki_4.9_obciazenie_magistrali.csv` | wyniki przy zmiennym obciążeniu | **przeliczone** z surowych korpusów |

Wiersze dla reguły przeliczono od nowa z danych. **Wiersz „sieć neuronowa"
przepisano z wyjścia programu** — model znajduje się na płytce, więc ponowne
przeliczenie wymaga jej uruchomienia.

## 4. Przykładowe dane

**Wejście** — 10 cech opisujących jeden bajt (wszystkie w zakresie 0–1):

```
liczba_bitow=0.625  skokowosc=0.845  czestosc_zmian=0.31  unikalne=0.02
entropia=0.44  srednia_delta=0.19  max_delta=0.51  odchylenie=0.22 ...
```

**Wyjście:** jedna liczba — decyzja „ten bajt zawiera flagi" albo „nie".

## 5. Jak sprawdzano poprawność

1. **Test wyłącznie na prawdziwym sprzęcie** — zbiory uczący i walidacyjny
   pochodzą z magistral wirtualnych, testowy z fizycznego układu MCP2515.
2. **Porównanie z regułą na tych samych danych** — nie z liczbami z innego przebiegu.
3. **Sprawdzenie, czy sieć jest w ogóle potrzebna** — dobrano najlepszy możliwy
   pojedynczy próg i najlepszą parę cech, dobierane na danych uczących
   i oceniane na sprzęcie.

## 6. Wynik końcowy

**Na 2936 pozycjach z prawdziwego sprzętu:**

| Metoda | Recall | Precision | F1 |
|---|---|---|---|
| reguła, limit 6 bitów (obecna) | 87,7 % | 94,4 % | 91,0 % |
| reguła, limit 7 bitów | 97,4 % | 94,5 % | 95,9 % |
| **sieć neuronowa (577 parametrów)** | **100 %** | **97,7 %** | **98,8 %** |

**Potwierdzenie na drugim, niezależnym korpusie (2742 pozycje):** zysk ze zmiany
limitu 6 → 7 wyniósł **+5,0 pp** i **+4,9 pp** — rozbieżność 0,1 punktu.

**Koszt wdrożenia:** uczenie **68 s** na płytce, inferencja 282 µs,
działa **bez biblioteki PyTorch** (wagi zapisane w JSON, kilkanaście linii
czystego Pythona). Równoważność z PyTorch zweryfikowana: maksymalna różnica
2,66·10⁻⁶, zero rozbieżnych decyzji.

## 7. Wnioski, w tym trzy odrzucone hipotezy

**Hipoteza:** reguła strojona w jednym punkcie pracy powinna degradować się poza
nim szybciej niż sieć uczona na wielu konfiguracjach.

| Próba | Zmienna | Czy zmieniła statystyki? |
|---|---|---|
| 4.8 | prędkość magistrali 125–1000 kbit/s | **nie** — błąd w projekcie eksperymentu |
| 4.9 | liczba identyfikatorów CAN 5–60 | **nie** — błąd w projekcie eksperymentu |
| 4.10 | skala okresów nadawania 0,25–4× | **tak, 15-krotnie** |

Dwie pierwsze manipulacje dotyczyły parametrów **poziomu magistrali**, podczas
gdy statystyki zależą od **stosunku częstotliwości próbkowania do dynamiki
sygnału**. Dopiero trzecia to zmieniła.

**Przy działającej manipulacji hipoteza została odrzucona:** wszystkie metody
okazały się bardzo stabilne (rozstępy 1,2–2,5 pp), a **najstabilniejsza była
reguła**, nie sieć.

**Wniosek:** przewaga sieci wynika wyłącznie z **jakości klasyfikacji**, nie
z większej odporności — i tak należy ją przedstawiać.

**Wycofany wniosek:** na wcześniejszym teście obejmującym 132 pozycje reguła
wypadła lepiej niż sieć i tak to zapisano. Przy próbie 22-krotnie większej
kierunek się odwrócił — różnica opierała się na **dwóch pozycjach**, czyli na
szumie statystycznym.
