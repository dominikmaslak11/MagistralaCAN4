#!/usr/bin/env python3
"""
Eksperyment 4.3, Etap B — automatyczne etykietowanie korpusu (wygenerowanego
w Etapie A, generate_traffic_diverse.py --dump-json) przez klasyczny,
deterministyczny klasyfikator "Kierunku B" (identyczna logika co
DecodingAccuracyRunner::looksLikeBitFlags/independentBitMask w C++,
src/core/DecodingAccuracyRunner.cpp linie 500-551 - PRZENIESIONA 1:1, nie
przeprojektowana).

WAZNE (cytat z Eksperyment_4.3_Propozycja...md, sekcja Etap B): "Weryfikacja
jakosci etykiet przez porownanie z ground truth generatora (nie tylko ufac
klasyfikatorowi w ciemno - jesli klasyfikator sie myli, blad trafi do zbioru
uczacego)." Ten skrypt wprost mierzy i raportuje, jak czesto klasyfikator
(nasz "nauczyciel") myli sie na tym duzo szerszym, zroznicowanym korpusie niz
oryginalny mini-DBC z Eksperymentu 4.1 (na ktorym klasyfikator mial 100%/0%
falszywych trafien).
"""
import argparse
import json
from collections import Counter


def independent_bit_mask(byte_series: list[int]) -> int:
    """1:1 port DecodingAccuracyRunner::independentBitMask (C++)."""
    seen0 = seen1 = 0
    for v in byte_series:
        for b in range(8):
            if v & (1 << b):
                seen1 |= (1 << b)
            else:
                seen0 |= (1 << b)
    return seen0 & seen1


def looks_like_bit_flags(byte_series: list[int], big_jump_ratio_threshold: float = 0.5) -> bool:
    """1:1 port DecodingAccuracyRunner::looksLikeBitFlags (C++, v2 heurystyka).

    Domyslny prog (0.5) jest CELOWO niezmieniony wzgledem oryginalu C++ i
    wynikow juz opublikowanych w Eksperyment_4.3_Raport (odtwarzalnosc).
    Eksperyment 4.5 (strojenie na danych z godzinnego przebiegu, seed=999)
    wykazal: recall=85% (nie 60%) przy progu <=0.46, precyzja niezmienna
    (100%, 0 FP) na calym zakresie 0.0-1.0 - urwisko lezy dokladnie miedzy
    0.46 a 0.47. Do uzytku "produkcyjnego" (pi_continuous_observer.py) przyjeto
    prog=0.3 (margines bezpieczenstwa od urwiska). Tutaj pozostawiono 0.5 jako
    parametr z domyslna, historyczna wartoscia - podaj inna wartosc jawnie,
    zeby uzyc postrojonej wersji."""
    if len(byte_series) < 2:
        return False
    mask = independent_bit_mask(byte_series)
    bit_count = bin(mask).count("1")
    if bit_count < 2 or bit_count > 6:
        return False

    big_jumps = changed_pairs = 0
    for i in range(1, len(byte_series)):
        a, b = byte_series[i - 1], byte_series[i]
        if a == b:
            continue
        changed_pairs += 1
        if abs(a - b) > 3:
            big_jumps += 1
    if changed_pairs == 0:
        return False
    return (big_jumps / changed_pairs) >= big_jump_ratio_threshold


def true_structure_by_can_id(ground_truth):
    """Prawdziwa struktura per bajt: 'bit_flag'+prawdziwa_maska, 'scalar', 'partial_scalar', albo None (nieuzywany)."""
    out = {}
    for cfg in ground_truth:
        m_kind = {}
        m_true_mask = {}
        for s in cfg["signals"]:
            if s["kind"] == "bit_flag":
                b = s["byte_idx"]
                m_kind[b] = "bit_flag"
                m_true_mask[b] = m_true_mask.get(b, 0) | (1 << s["bit_idx"])
            elif s["kind"] == "partial_scalar":
                m_kind.setdefault(s["byte_idx"], "partial_scalar")
            else:
                for off in range(s["byte_len"]):
                    m_kind.setdefault(s["byte_idx"] + off, "scalar")
        out[cfg["can_id"]] = (m_kind, m_true_mask)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", default="../esp_experiment_4_4_qdrant/corpus_diverse.json")
    ap.add_argument("--out", default="etap_b_labels.json")
    ap.add_argument("--threshold", type=float, default=0.5,
                     help="prog big_jumps/changed_pairs (domyslnie 0.5, historyczny/odtwarzalny; "
                          "Eksperyment 4.5 sugeruje <=0.3-0.46 dla wyzszego recall bez utraty precyzji)")
    args = ap.parse_args()

    with open(args.corpus) as f:
        corpus = json.load(f)

    truth = true_structure_by_can_id(corpus["ground_truth"])

    labels = {}  # can_id -> {byte_idx: {"classifier_says": "bit_flag"/"not_bit_flag", "mask": int|None}}
    confusion = Counter()  # (true_is_flag, classifier_says_flag) -> count
    mask_exact_matches = mask_total = 0

    for cfg in corpus["ground_truth"]:
        can_id = cfg["can_id"]
        dlc = cfg["dlc"]
        samples = corpus["samples"][str(can_id)]
        true_kind, true_mask = truth[can_id]

        labels[can_id] = {}
        for byte_idx in range(dlc):
            series = [frame[byte_idx] for frame in samples if byte_idx < len(frame)]
            is_flags = looks_like_bit_flags(series, args.threshold)
            mask = independent_bit_mask(series) if is_flags else None
            labels[can_id][byte_idx] = {
                "classifier_says_bit_flags": is_flags,
                "classifier_mask": mask,
            }

            true_is_flag = true_kind.get(byte_idx) == "bit_flag"
            confusion[(true_is_flag, is_flags)] += 1
            if true_is_flag and is_flags:
                mask_total += 1
                if mask == true_mask.get(byte_idx):
                    mask_exact_matches += 1

    tp = confusion[(True, True)]
    fp = confusion[(False, True)]
    fn = confusion[(True, False)]
    tn = confusion[(False, False)]
    precision = tp / (tp + fp) if (tp + fp) else 0.0
    recall = tp / (tp + fn) if (tp + fn) else 0.0
    f1 = 2 * precision * recall / (precision + recall) if (precision + recall) else 0.0

    print(f"Korpus: {len(corpus['ground_truth'])} konfiguracji CAN ID, "
          f"{sum(len(v) for v in labels.values())} pozycji bajtowych ocenionych.")
    print()
    print("Macierz pomylek klasyfikatora (prawda: czy bajt to NAPRAWDE bit_flag):")
    print(f"  TP={tp}  FP={fp}  FN={fn}  TN={tn}")
    print(f"  Precision={precision*100:.1f}%  Recall={recall*100:.1f}%  F1={f1*100:.1f}%")
    print(f"  Gdy TP: dokladnosc samej MASKI (ktore konkretnie bity) = "
          f"{mask_exact_matches}/{mask_total} = {100*mask_exact_matches/mask_total if mask_total else 0:.1f}%")

    out = {
        "corpus": args.corpus,
        "confusion": {"tp": tp, "fp": fp, "fn": fn, "tn": tn},
        "precision": precision, "recall": recall, "f1": f1,
        "maskAccuracyGivenTruePositive": mask_exact_matches / mask_total if mask_total else None,
        "labels": labels,
    }
    with open(args.out, "w") as f:
        json.dump(out, f, indent=2)
    print(f"\nZapisano etykiety: {args.out}")


if __name__ == "__main__":
    main()
