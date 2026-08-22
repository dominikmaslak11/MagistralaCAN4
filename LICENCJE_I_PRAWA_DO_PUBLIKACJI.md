# Licencje i prawa — przygotowanie do publikacji artykułu

Data: 2026-08-17
Kontekst: przygotowanie `Artykul_Naukowy_LLM_CAN_Bitowe_Flagi.md` do publikacji.

> **Zastrzeżenie:** to jest zestawienie techniczne, nie porada prawna. Licencje
> pakietów odczytano z metadanych **faktycznie zainstalowanych** wersji
> (nie z pamięci ani z dokumentacji), ale interpretacja prawna w konkretnym
> przypadku — zwłaszcza umowa z wydawcą — wymaga potwierdzenia przez prawnika
> albo dział prawny uczelni.

---

## 1. Problem do naprawienia w pierwszej kolejności

**`README.md` deklaruje: „MIT License — zobacz plik LICENSE".**
**Pliku `LICENSE` w repozytorium NIE MA.**

Konsekwencja: formalnie kod jest objęty domyślnym „wszelkie prawa zastrzeżone",
mimo deklaracji w README. Dla artykułu naukowego to realny problem, bo:

- recenzent albo czytelnik nie może legalnie użyć kodu do odtworzenia wyników,
- deklaracja odtwarzalności („kod dostępny na GitHubie") jest wtedy pusta,
- sprzeczność między README a stanem faktycznym może zostać podniesiona.

**Naprawa: dodać plik `LICENSE` z pełnym tekstem licencji MIT** (albo zmienić
deklarację w README, jeśli intencja była inna). To jedna czynność, a zdejmuje
najpoważniejsze ryzyko.

---

## 2. Licencje zależności — stan zweryfikowany

Odczytane z metadanych pakietów zainstalowanych na Orange Pi Zero 3
(`/root/venv45`), stan na 2026-08-17:

| Pakiet | Wersja | Licencja | Uwagi |
|---|---|---|---|
| `torch` | 2.13.0+cpu | Apache-2.0 AND BSD-2 AND BSD-3 AND BSL-1.0 AND MIT | permisywna |
| `sentence-transformers` | 5.7.0 | Apache-2.0 | permisywna |
| `transformers` | 5.15.0 | Apache-2.0 | permisywna |
| `qdrant-client` | 1.19.0 | Apache-2.0 | permisywna |
| `huggingface-hub` | 1.27.0 | Apache-2.0 | permisywna |
| `numpy` | 2.5.2 | BSD-3-Clause AND 0BSD AND MIT AND Zlib AND CC0-1.0 | permisywna |
| `scipy` | 1.18.0 | BSD | permisywna |
| **`python-can`** | **4.6.1** | **LGPL-3.0-only** | **jedyna copyleft — patrz niżej** |

Model językowy:

| Zasób | Licencja |
|---|---|
| `sentence-transformers/all-MiniLM-L6-v2` | Apache-2.0 |

Warstwa systemowa (nie dystrybuujecie jej, ale warto wymienić w metodyce):
Linux (GPL-2.0), Armbian/Debian (zbiór licencji, w większości GPL),
`can-utils` (GPL-2.0/BSD), `weasyprint` (BSD-3).

### 2.1. Jedyny pakiet wymagający uwagi: `python-can` (LGPL-3.0)

LGPL to licencja copyleft, ale **słaba** — w praktyce dla Was oznacza:

- **Używanie przez `import`** (tak właśnie robicie) **nie zmusza** do objęcia
  Waszego kodu licencją LGPL. To jest kluczowa różnica między LGPL a GPL.
- Warunek: biblioteka musi pozostać **wymienialna** — przy imporcie w Pythonie
  jest to spełnione z natury (linkowanie dynamiczne).
- Obowiązek: **podać informację o użyciu i licencji** oraz nie usuwać
  oryginalnych not licencyjnych.

**Wniosek:** możecie opublikować swój kod na MIT mimo zależności od
`python-can`. Wystarczy wymienić ją w sekcji o zależnościach.

Uwaga praktyczna: w Eksperymencie 4.11 i w demonie `pi_observer_nn.py`
świadomie zrezygnowano z `python-can` na rzecz **surowych gniazd SocketCAN**
(moduł `socket` z biblioteki standardowej). To był wybór techniczny (jedna
zależność mniej), ale ma efekt uboczny: **te konkretne skrypty nie mają
żadnej zależności copyleft.**

---

## 3. Rysunki i materiały producentów w artykule

To najczęstsza pułapka przy publikacji.

### 3.1. Czego NIE wolno bez zgody

**Kopiowania kolorowych „rysunków pinoutu" krążących w sieci.** To utwory
objęte prawem autorskim, a ich powszechna dostępność nie oznacza zgody na
rozpowszechnianie. Dotyczy to także grafik z blogów, sklepów i wiki
społecznościowych.

### 3.2. Co jest bezpieczne

| Materiał | Status |
|---|---|
| **Tabela przyporządkowania pinów** („pin 19 = PH7 = SPI1_MOSI") | **fakt** — fakty nie podlegają prawu autorskiemu; chroniona jest forma graficzna, nie treść |
| **Diagram narysowany samodzielnie** z takiej tabeli | Wasz utwór, pełne prawa |
| **Cytat fragmentu** dokumentacji z podaniem źródła | dozwolony użytek (prawo cytatu, art. 29 ustawy o prawie autorskim) — w zakresie uzasadnionym wyjaśnieniem lub analizą |
| Zrzut ekranu **własnego** oprogramowania i własne wykresy | Wasze |

**W tym projekcie diagram pinoutu Orange Pi Zero 3 jest rysowany samodzielnie**
(`components_datasheet/OrangePi_Zero3_Pinout_26pin.html` → PDF), na podstawie
oficjalnego manuala i odczytu z device tree. Można go publikować bez ograniczeń.

### 3.3. Materiały producentów zebrane w projekcie

W `components_datasheet/pinouty/` leżą **oficjalne dokumenty producentów**
(manual Orange Pi, datasheet Espressif, schematy Raspberry Pi). Służą jako
**źródło danych i odniesienie bibliograficzne**, nie do przedrukowania rysunków.

Do artykułu: **cytować je w bibliografii**, a rysunki robić własne.

---

## 4. Dane pomiarowe i korpusy

| Zasób | Status |
|---|---|
| Ruch CAN z generatora `generate_traffic_diverse.py` | **w pełni Wasz** — dane syntetyczne z własnego kodu |
| Zebrane korpusy (`bits_data`, `e48_*`, `e49_*`, `e410_*`) | Wasze |
| Wytrenowane modele (`model_47.pt`, `model_bits.pt`) | Wasze — wytrenowane na własnych danych |
| Wyniki pomiarów, tabele, wykresy | Wasze |

To jest **mocna strona tej pracy z punktu widzenia publikacji**: nie ma tu ani
jednego zbioru danych obcego pochodzenia, więc nie ma pytań o prawa do danych
ani o zgody. Warto to w artykule napisać wprost — recenzenci to doceniają.

**Uwaga na przyszłość:** gdyby doszły dane z **prawdziwej maszyny rolniczej**,
sytuacja się zmienia. Ruch z magistrali konkretnego pojazdu może być uznany za
informację o produkcie objętą tajemnicą producenta, a publikacja
zdekodowanych ramek producenckich bywa traktowana jak ujawnienie inżynierii
odwrotnej. Wtedy: zanonimizować identyfikatory, opisać metodę bez podawania
gotowego „klucza" do konkretnego modelu, i sprawdzić warunki gwarancji.

---

## 5. Co zrobić przed wysłaniem artykułu

1. **Dodać plik `LICENSE`** (MIT, zgodnie z deklaracją w README). Priorytet.
2. **Sekcja „Dostępność kodu i danych"** w artykule: link do repozytorium,
   nazwa licencji, numer commita (np. `5b9e2ff`) — commit czyni odwołanie
   jednoznacznym mimo dalszych zmian.
3. **Sekcja o zależnościach** z tabelą z punktu 2 — pokazuje staranność
   i zdejmuje pytania recenzenta.
4. **Wszystkie rysunki własne** albo z jawną zgodą. Podpis pod rysunkiem
   pinoutu: „opracowanie własne na podstawie [manual v1.1, str. 123] oraz
   odczytu z device tree".
5. **Sprawdzić umowę z wydawcą** — czy wymaga przeniesienia praw autorskich,
   czy licencji (np. CC BY). Od tego zależy, czy będziecie mogli dalej używać
   własnych rysunków w innych publikacjach i w pracy dyplomowej.
6. **Sprawdzić regulamin uczelni** co do praw do wyników pracy dyplomowej —
   bywa, że uczelnia ma współudział w prawach majątkowych.

---

## 6. Wybór licencji dla własnego kodu — krótko

| Licencja | Kiedy sensowna |
|---|---|
| **MIT** | deklarowana w README. Maksymalna swoboda użycia, minimum formalności. Dobra dla kodu towarzyszącego publikacji naukowej — nie utrudnia nikomu odtworzenia wyników. |
| Apache-2.0 | jak MIT, plus jawna klauzula patentowa. Warto rozważyć, jeśli w grę wchodzi wdrożenie komercyjne. |
| GPL-3.0 | wymusza otwartość prac pochodnych. Sensowne, jeśli zależy Wam, żeby nikt nie zamknął tego w produkcie zamkniętym — ale zniechęca część zastosowań przemysłowych. |

**Rekomendacja: zostać przy MIT** — jest już zadeklarowana, jest zgodna ze
wszystkimi zależnościami (łącznie z LGPL `python-can` przy imporcie) i najlepiej
służy odtwarzalności, która jest argumentem naukowym tej pracy.
