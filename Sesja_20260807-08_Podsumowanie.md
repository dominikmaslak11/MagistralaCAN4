# Podsumowanie sesji 2026-08-07 → 2026-08-08

Notatka robocza dla Ciebie — nie formalny raport eksperymentu (te są osobno,
patrz lista na dole). Chronologiczny przebieg tego, co razem zrobiliśmy.

---

## Część 1 (07.08): instalacja Raspberry Pi Zero W

Zaczęliśmy od zwykłego flashowania karty microSD, ale po drodze:
- Raspberry Pi Imager 2.0+ blokuje personalizację (WiFi/SSH/hasło) dla
  lokalnie wskazanych obrazów — trzeba było robić to ręcznie.
- Obraz **Trixie** (Debian 13) w ogóle nie łączył WiFi mimo poprawnej
  konfiguracji — sterowniki na zbyt świeżym branchu. Przejście na
  **Bookworm (Legacy)** rozwiązało problem od razu.
- Cała długa przygoda z UART (ESP-Prog2, potem dedykowany adapter USB-serial)
  jako narzędzie diagnostyczne — ostatecznie ślepy zaułek (ESP-Prog2 ma
  własny wewnętrzny mikrokontroler, jego USB to NIE jest przezroczysty most
  do pinów). Jedyne co faktycznie działało: fizyczne wyjmowanie karty SD i
  patrzenie na pliki rozruchowe.
- W końcu: Pi Zero W wstało, `ssh pi@10.149.89.72` działa, dane logowania w
  `MagistralaCAN4_current/piCredentials.txt` (gitignored).
- Skonfigurowaliśmy HAT MCP2515 (Waveshare), `can0` na stałe przez systemd,
  zweryfikowaliśmy dwukierunkowo z PEAK PCAN-USB (candump/cansend, zero
  błędów).

## Część 2 (07-08.08): Eksperyment 4.5, pięć faz

**Faza 1** — demon Python na Pi, ciągły klasyfikator flag bitowych
(O(1)/klatkę, matematycznie zweryfikowany identyczny z wersją offline z
4.3). Godzina, 1,6 mln ramek. Wynik: precyzja 100% cały czas, recall
rośnie z 50%→65% w pierwszych minutach, potem **twardo zatrzymuje się na
60%**.

**Strojenie progu** — okazało się, że próg 0.5 w klasyfikatorze (nigdy
niestrojony) ucinał 5 z 20 prawdziwych flag za darmo. Obniżenie do 0.3
podniosło realny sufit recall do 85%, bez utraty precyzji. Zmieniłem to w
kodzie (`pi_continuous_observer.py`, i sparametryzowałem w
`etap_b_autolabel.py` bez zmiany domyślnej wartości, żeby nie zepsuć
odtwarzalności starych raportów 4.3/4.4).

**Faza 2** — embedded Qdrant budowany NA ŻYWO z tego samego przebiegu
(zamiast z osobnego korpusu offline jak w 4.4). Trafność 93.5%, płasko od
pierwszej sekundy — okazało się, że to nie długość obserwacji naprawiła
problem z 4.4, tylko dopasowanie rozkładu danych.

**Faza 3** — pełna ewaluacja 4 modeli LLM (Claude/GPT/DeepSeek/Gemini),
N=100 sparowanych prób, 800 wywołań API, zero błędów. Mieszany wynik:
warmstart dalej **szkodził** detekcji flag bitowych (jak w 4.4), mimo
świetnej biblioteki.

**Faza 4 — najsilniejszy wynik całej sesji.** Twardy "hybrydowy override"
z Eksperymentu 4.1 (nie miękka podpowiedź, tylko wymuszona podmiana
struktury), zasilony statystyką z Fazy 1 zamiast krótkiego okna: **+2.5 do
+18.0pp ogólnej detekcji, +1.3 do +32.4pp na flagach bitowych, u
wszystkich 4 modeli, 100% precyzji, ZERO nowych wywołań API** (reużyliśmy
zapisane odpowiedzi z Fazy 3). Przeniesiony do prawdziwego kodu C++
(`DecodingAccuracyRunner.cpp`) — skompilowany i zlinkowany bez błędów,
jako trzeci, dodatkowy tor metryk obok istniejących.

**Faza 5** — embeddingi neuronowe (gotowy model `all-MiniLM-L6-v2`, zero
treningu) zamiast ręcznych cech w Qdrant. Wygrywają wyraźnie we wszystkich
3 testach (+7 do +9pp), najmocniej na prawdziwych danych z magistrali. ALE:
**PyTorch/ONNX/TensorFlow Lite nie mają żadnych pakietów dla ARMv6** —
sprawdzone bezpośrednio na Pi Zero W, nie na wiarę. Niewdrażalne na obecnym
sprzęcie. Za to sprawdziłem: Twój **Orange Pi Zero 3** (aarch64) ma pełne
wsparcie PyTorch na PyPI — obiecująca ścieżka dalsza, jeszcze niezweryfikowana
na samej płytce.

---

## Wygenerowane dokumenty (repo root)

| Plik | Co zawiera |
|---|---|
| `Eksperyment_4.5_Raport_Koncowy_20260808.md` | Fazy 1-3, pełna metodyka i wnioski |
| `Eksperyment_4.5_Infografika_20260808.pdf` | Infografika do powyższego |
| `Eksperyment_4.5_Strojenie_Progu_Klasyfikatora_20260808.md` | Analiza progu 0.5→0.3 |
| `Eksperyment_4.5_Faza4_Override_Ciagly_20260808.md` | Najsilniejszy wynik — override |
| `Eksperyment_4.5_Faza4_Infografika_20260808.pdf` | Infografika do Fazy 4 |
| `Eksperyment_4.5_Faza5_Embeddingi_Neuronowe_20260808.md` | Embeddingi neuronowe + limit ARMv6 |
| `Eksperyment_4.5_Faza5_Infografika_20260808.pdf` | Infografika do Fazy 5 |
| `piCredentials.txt` | Dane logowania do Pi (gitignored) |

Kod: `esp_experiment_4_5_rpi/` (demony Pi, skrypty analizy, override offline,
embeddingi neuronowe). Zmiany w kodzie C++: `src/core/DecodingAccuracyRunner.h/.cpp`.

## Stan sprzętu na koniec sesji

Pi Zero W: **wyłączone** (bezpiecznie, `sudo shutdown`) — możesz odłączyć
zasilanie. PCAN-USB: możesz odłączyć, nic go teraz nie używa.

## Co dalej (jeśli będziesz kontynuować)

Naturalny pierwszy krok kolejnej sesji: podłączyć Orange Pi Zero 3,
sprawdzić `uname -m` (czy faktycznie aarch64) i spróbować realnej instalacji
`torch`/`sentence-transformers` na płytce — plus dokupienie generycznego
modułu MCP2515 (inny format złącza GPIO niż Raspberry Pi).
