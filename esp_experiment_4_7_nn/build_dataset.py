#!/usr/bin/env python3
"""
Eksperyment 4.7, krok 1 -- budowa zbioru uczacego dla sieci neuronowej
zastepujacej regule reczna ("Kierunek B") w wykrywaniu bajtow z flagami bitowymi.

DLACZEGO TE CECHY: `pi_continuous_observer.py` juz liczy seen0/seen1/
changed_pairs/big_jumps/n_samples PRZYROSTOWO, w O(1) na ramke. Wszystkie
cechy ponizej daja sie policzyc z tego samego, stalego stanu (albo z jego
oczywistego rozszerzenia o kilka licznikow) -- dzieki temu siec da sie wdrozyc
w istniejacym demonie BEZ zmiany charakterystyki pamieciowej. Cechy oparte na
pelnej historii probek (np. FFT, autokorelacja) sa swiadomie pominiete.

Etykieta: binarna, per (CAN ID, bajt) -- czy bajt zawiera flagi bitowe.
To DOKLADNIE to samo zadanie, ktore wykonuje regula reczna, wiec metryki
(Recall/Precision/F1) sa bezposrednio porownywalne.

Uzycie:
  python3 build_dataset.py --seeds 1-40 --out train.json
  python3 build_dataset.py --seeds 41-50 --out val.json
"""
import argparse
import json
import math
import os
import subprocess
import sys
import tempfile
from collections import Counter

GENERATOR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "esp_experiment_4_3", "generate_traffic_diverse.py"
)

FEATURE_NAMES = [
    "bit_count",        # liczba bitow w masce seen0&seen1 (0-8), znormalizowana
    "jump_ratio",       # big_jumps / changed_pairs -- cecha uzywana przez regule
    "change_ratio",     # changed_pairs / n_samples
    "distinct_ratio",   # liczba unikalnych wartosci / 256
    "entropy",          # entropia Shannona wartosci bajtu / 8 bitow
    "mean_abs_delta",   # srednia |v[i]-v[i-1]| / 255
    "max_abs_delta",    # max |v[i]-v[i-1]| / 255
    "std_value",        # odchylenie standardowe wartosci / 255
    "extremes_frac",    # udzial wartosci 0x00 i 0xFF
    "popcount_std",     # odchylenie liczby zapalonych bitow (plynny skalar ma male)
]


def byte_features(values):
    """Cechy z sekwencji wartosci JEDNEGO bajtu. Wszystkie w [0,1]."""
    n = len(values)
    if n < 2:
        return None

    seen0 = seen1 = 0
    for v in values:
        for b in range(8):
            if v & (1 << b):
                seen1 |= (1 << b)
            else:
                seen0 |= (1 << b)
    mask = seen0 & seen1
    bit_count = bin(mask).count("1")

    changed_pairs = 0
    big_jumps = 0
    deltas = []
    for i in range(1, n):
        if values[i] != values[i - 1]:
            changed_pairs += 1
            d = abs(values[i] - values[i - 1])
            deltas.append(d)
            if d > 3:
                big_jumps += 1

    jump_ratio = (big_jumps / changed_pairs) if changed_pairs else 0.0
    change_ratio = changed_pairs / n

    counts = Counter(values)
    distinct_ratio = len(counts) / 256.0
    entropy = 0.0
    for c in counts.values():
        p = c / n
        entropy -= p * math.log2(p)
    entropy /= 8.0

    mean_abs_delta = (sum(deltas) / len(deltas) / 255.0) if deltas else 0.0
    max_abs_delta = (max(deltas) / 255.0) if deltas else 0.0

    mean_v = sum(values) / n
    var = sum((v - mean_v) ** 2 for v in values) / n
    std_value = math.sqrt(var) / 255.0

    extremes_frac = sum(1 for v in values if v in (0x00, 0xFF)) / n

    pops = [bin(v).count("1") for v in values]
    mean_p = sum(pops) / n
    pop_var = sum((p - mean_p) ** 2 for p in pops) / n
    popcount_std = math.sqrt(pop_var) / 4.0  # max sensowne ~4

    return [
        bit_count / 8.0,
        jump_ratio,
        change_ratio,
        distinct_ratio,
        min(entropy, 1.0),
        mean_abs_delta,
        max_abs_delta,
        std_value,
        extremes_frac,
        min(popcount_std, 1.0),
    ]


def raw_state(values):
    """Surowy stan w formacie observera -- do policzenia werdyktu REGULY
    na dokladnie tych samych danych (uczciwe porownanie)."""
    n = len(values)
    seen0 = seen1 = 0
    for v in values:
        for b in range(8):
            if v & (1 << b):
                seen1 |= (1 << b)
            else:
                seen0 |= (1 << b)
    changed_pairs = big_jumps = 0
    for i in range(1, n):
        if values[i] != values[i - 1]:
            changed_pairs += 1
            if abs(values[i] - values[i - 1]) > 3:
                big_jumps += 1
    return {"seen0": seen0, "seen1": seen1, "changed_pairs": changed_pairs,
            "big_jumps": big_jumps, "n_samples": n}


def corpus_to_rows(corpus):
    flag_bytes = set()
    for cfg in corpus["ground_truth"]:
        for s in cfg["signals"]:
            if s["kind"] == "bit_flag":
                flag_bytes.add((cfg["can_id"], s["byte_idx"]))

    rows = []
    for cfg in corpus["ground_truth"]:
        cid = cfg["can_id"]
        dlc = cfg.get("dlc", 8)
        samples = corpus["samples"][str(cid)]
        for b in range(dlc):
            values = [frame[b] for frame in samples if len(frame) > b]
            feats = byte_features(values)
            if feats is None:
                continue
            rows.append({
                "can_id": cid,
                "byte": b,
                "features": feats,
                "label": 1 if (cid, b) in flag_bytes else 0,
                "state": raw_state(values),
            })
    return rows


def parse_seeds(spec):
    out = []
    for part in spec.split(","):
        part = part.strip()
        if "-" in part:
            a, b = part.split("-")
            out.extend(range(int(a), int(b) + 1))
        else:
            out.append(int(part))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", required=True, help="np. 1-40 albo 1,2,5")
    ap.add_argument("--n-configs", type=int, default=30)
    ap.add_argument("--samples-per-id", type=int, default=200)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    seeds = parse_seeds(args.seeds)
    all_rows = []
    for i, seed in enumerate(seeds, 1):
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tf:
            tmp = tf.name
        cmd = [sys.executable, GENERATOR, "--seed", str(seed),
               "--n-configs", str(args.n_configs),
               "--dump-json", tmp,
               "--dump-samples-per-id", str(args.samples_per_id)]
        subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        with open(tmp) as f:
            corpus = json.load(f)
        os.unlink(tmp)
        rows = corpus_to_rows(corpus)
        for r in rows:
            r["seed"] = seed
        all_rows.extend(rows)
        print(f"  [{i}/{len(seeds)}] seed={seed}: {len(rows)} pozycji "
              f"({sum(r['label'] for r in rows)} z flagami)")

    pos = sum(r["label"] for r in all_rows)
    with open(args.out, "w") as f:
        json.dump({"feature_names": FEATURE_NAMES, "rows": all_rows}, f)
    print(f"\nZapisano {args.out}: {len(all_rows)} pozycji, "
          f"{pos} z flagami ({pos/len(all_rows):.1%}), ziaren: {len(seeds)}")


if __name__ == "__main__":
    main()
