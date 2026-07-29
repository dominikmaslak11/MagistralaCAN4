# Eksperyment 4.3, Etap A — rozbudowany generator syntetycznego ruchu

Status: Etap A (rozbudowa generatora) UKOŃCZONY i zweryfikowany. Etapy B
(automatyczne etykietowanie klasyfikatorem), C (format treningowy), D
(fine-tuning + ewaluacja) — NIEWYKONANE, poza zakresem tej iteracji.

Patrz `Eksperyment_4.3_Propozycja_Bootstrapped_FineTuning_20260728.md` w
katalogu głównym repo dla pełnego kontekstu i uzasadnienia.

## Czego to rozwiązuje

Eksperyment 4.1 używał JEDNEGO, stałego zestawu 10 sygnałów na 3 CAN ID
(`esp_experiment_4_1/generate_traffic.py`) — za wąskiego do fine-tuningu bez
ryzyka, że model nauczy się rozpoznawać te konkretne ramki zamiast ogólnej
zasady. `generate_traffic_diverse.py` generuje **wiele** (domyślnie 30, N
konfigurowalne) różnych konfiguracji CAN ID, każda z losowo (ale
reprodukowalnie — ustalone ziarno) dobranym zestawem sygnałów spośród 6
wzorców:

1. `pure_scalars` — same sygnały ciągłe, zero flag (kontrola negatywna)
2. `pure_flags_one_byte` — 2-6 flag bitowych w jednym bajcie (jak 0x200 w 4.1, ale ze zmienną liczbą flag)
3. `flags_spread_multi_byte` — flagi rozproszone na 2 różnych bajtach ramki
4. `mixed_byte` — bajt MIESZANY: część bitów flagi, część mniejszy "podskalar" (nietestowane dotąd w tym projekcie)
5. `scalars_plus_flags` — sygnały ciągłe i flagi w tej samej ramce, na różnych bajtach

## Użycie

```bash
# Tryb offline (bez sprzętu) - budowa korpusu do fine-tuningu:
python3 generate_traffic_diverse.py --n-configs 30 --seed 42 \
    --dump-json corpus.json --dump-samples-per-id 300

# Tryb na żywo (z prawdziwym ESP32+PEAK PCAN-USB, jak oryginalny generator 4.1):
python3 generate_traffic_diverse.py --iface can0 --duration 3600 \
    --n-configs 30 --seed 42
```

## Zweryfikowane działanie

- Składnia i uruchomienie: OK.
- Bajty mieszane (flagi + podskalar w tym samym bajcie): potwierdzone poprawne
  współistnienie (np. CAN ID 0x340: bity 0-1 flagi, bity 2-5 podskalar —
  wartości bajtu 23→27 pokazują zmianę TYLKO podskalara, flagi bez zmian w
  krótkim oknie, zgodnie z oczekiwaniem).
- Przełączanie flag w czasie: potwierdzone na przykładzie z 6 flagami (CAN ID
  0x3e0) — 5 różnych kombinacji bitów zaobserwowanych na przestrzeni 300
  próbek (15s symulowanego czasu), zgodnie z ustawionymi interwałami
  przełączania (2-25s).

## Format ground truth (wyjście `--dump-json`)

Kompatybilny ze schematem już używanym w `DecodingAccuracyRunner` (C++):
`name`, `byteIdx`, `byteLen`, `littleEndian`, `isSigned`, `bitMask`, `scale`,
`offset`. Pole `bitMask` w istniejącym kodzie C++ (`LlmSignalRule::decode()`)
już obsługuje ogólnie maski wielobitowe (nie tylko pojedyncze bity) — dzięki
temu "podskalary" (częściowe wykorzystanie bajtu) NIE wymagają zmian w
istniejącym kodzie dekodowania C++. Wymagają natomiast małej zmiany w logice
DOPASOWANIA dla sygnałów dyskretnych (`findMatchingRule()`), która dziś
zakłada tylko pojedyncze bity — w tym generatorze podskalary są celowo
klasyfikowane jako CIĄGŁE (`is_discrete: false`), co działa już dziś bez
zmian w C++ (ocena przez RMSE zamiast F1) — decyzja projektowa, nie
ograniczenie fundamentalne.

## Następne kroki (niewykonane)

- **Etap B**: uruchomić klasyfikator Kierunku B (`looksLikeBitFlags()` /
  `independentBitMask()` z `DecodingAccuracyRunner.cpp`) na wygenerowanym
  korpusie, żeby automatycznie etykietować każdą próbkę.
- **Etap C**: sformatować etykietowane przykłady jako pary (prompt,
  completion) w schemacie identycznym z promptem `DecodingAccuracyRunner`.
- **Etap D**: fine-tuning (GPT-4.1/o4-mini) + ewaluacja tym samym harnessem
  co Eksperyment 4.1.
