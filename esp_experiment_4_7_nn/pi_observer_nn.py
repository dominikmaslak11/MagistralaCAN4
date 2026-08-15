#!/usr/bin/env python3
"""
Demon ciaglej obserwacji can0 z DWOMA klasyfikatorami rownolegle:
regula reczna ("Kierunek B") ORAZ siec neuronowa z Eksperymentu 4.7.

Pochodzi z `esp_experiment_4_5_rpi/pi_continuous_observer.py` (Eksperyment 4.5,
Faza 1). Roznice wzgledem oryginalu:

  1. ByteStat sledzi dodatkowe liczniki potrzebne sieci -- WSZYSTKIE
     przyrostowe, O(1) na ramke. Histogram wartosci to tablica STALEJ dlugosci
     256 (`array("I")`, ~1 KB na bajt), wiec pamiec pozostaje ograniczona
     niezaleznie od dlugosci przebiegu -- kluczowa wlasnosc oryginalu zachowana.
  2. Inferencja sieci w CZYSTYM PYTHONIE z wag w JSON. Demon NIE wymaga
     PyTorcha (1.3 GB instalacji, kilka sekund importu) do policzenia 577
     parametrow -- dziala na golym Pythonie 3.
  3. Werdykt zawiera oba rozstrzygniecia, wiec mozna je porownywac na zywo
     bez uruchamiania dwoch demonow na jednej magistrali.

Regula pozostaje domyslna i niezmieniona -- siec jest DODATKIEM, nie zamiennikiem,
dopoki jej przewaga nie zostanie potwierdzona poza punktem strojenia
(Eksperyment 4.9).

Uzycie:
  python3 pi_observer_nn.py --iface can0 --model model_47.json \\
      --duration 300 --snapshot-interval 60
"""
import argparse
import json
import math
import os
import signal
import socket
import struct
import sys
import time
from array import array

CAN_FRAME_FMT = "=IB3x8s"
CAN_FRAME_SIZE = struct.calcsize(CAN_FRAME_FMT)

# Prog reguly -- patrz Eksperyment_4.5_Strojenie_Progu_Klasyfikatora_20260808.md
# oraz weryfikacja w Eksperymencie 4.6 (urwisko na 0.4595 przy przebiegu 1h).
BIG_JUMP_RATIO_THRESHOLD = 0.3
RULE_MAX_BITS = 6


# --------------------------------------------------------------------------
# Regula reczna -- 1:1 z pi_continuous_observer.py
# --------------------------------------------------------------------------
def independent_bit_mask_from_seen(seen0, seen1):
    return seen0 & seen1


def looks_like_bit_flags_from_state(mask, changed_pairs, big_jumps, n_samples,
                                    max_bits=RULE_MAX_BITS):
    if n_samples < 2:
        return False
    bit_count = bin(mask).count("1")
    if bit_count < 2 or bit_count > max_bits:
        return False
    if changed_pairs == 0:
        return False
    return (big_jumps / changed_pairs) >= BIG_JUMP_RATIO_THRESHOLD


# --------------------------------------------------------------------------
# Siec neuronowa -- inferencja bez PyTorcha
# --------------------------------------------------------------------------
class TinyMLP:
    """Wagi z JSON (export_model.py). ReLU miedzy warstwami, ostatnia bez
    aktywacji. Decyzja: logit >= 0 (rownowazne sigmoid(logit) >= 0.5)."""

    def __init__(self, path):
        with open(path) as f:
            d = json.load(f)
        self.n_features = d["n_features"]
        self.layers = [(l["W"], l["b"]) for l in d["layers"]]
        self.feature_names = d.get("feature_names", [])

    def logit(self, x):
        for i, (W, b) in enumerate(self.layers):
            out = []
            for row, bias in zip(W, b):
                s = bias
                for w, v in zip(row, x):
                    s += w * v
                out.append(s)
            if i < len(self.layers) - 1:          # ReLU poza ostatnia warstwa
                out = [v if v > 0.0 else 0.0 for v in out]
            x = out
        return x[0]

    def predict(self, features):
        return self.logit(features) >= 0.0


# --------------------------------------------------------------------------
# Stan per bajt
# --------------------------------------------------------------------------
class ByteStat:
    __slots__ = ("seen0", "seen1", "prev", "has_prev", "changed_pairs", "big_jumps",
                 "n_samples", "hist", "n_distinct", "sum_v", "sum_v2", "sum_delta",
                 "max_delta", "sum_pop", "sum_pop2", "extremes")

    def __init__(self):
        self.seen0 = 0
        self.seen1 = 0
        self.prev = None
        self.has_prev = False
        self.changed_pairs = 0
        self.big_jumps = 0
        self.n_samples = 0
        # rozszerzenie dla sieci -- wszystko O(1) na ramke, pamiec STALA
        self.hist = array("I", bytes(4 * 256))   # histogram wartosci, ~1 KB
        self.n_distinct = 0
        self.sum_v = 0
        self.sum_v2 = 0
        self.sum_delta = 0
        self.max_delta = 0
        self.sum_pop = 0
        self.sum_pop2 = 0
        self.extremes = 0

    def update(self, v):
        for b in range(8):
            if v & (1 << b):
                self.seen1 |= (1 << b)
            else:
                self.seen0 |= (1 << b)
        if self.hist[v] == 0:
            self.n_distinct += 1
        self.hist[v] += 1
        self.sum_v += v
        self.sum_v2 += v * v
        p = bin(v).count("1")
        self.sum_pop += p
        self.sum_pop2 += p * p
        if v == 0x00 or v == 0xFF:
            self.extremes += 1
        if self.has_prev and self.prev != v:
            self.changed_pairs += 1
            d = self.prev - v
            if d < 0:
                d = -d
            self.sum_delta += d
            if d > self.max_delta:
                self.max_delta = d
            if d > 3:
                self.big_jumps += 1
        self.prev = v
        self.has_prev = True
        self.n_samples += 1

    def features(self):
        """10 cech w kolejnosci z build_dataset.FEATURE_NAMES, wszystkie w [0,1]."""
        n = self.n_samples
        if n < 2:
            return None
        mask = self.seen0 & self.seen1
        bit_count = bin(mask).count("1")
        cp = self.changed_pairs
        jump_ratio = (self.big_jumps / cp) if cp else 0.0
        change_ratio = cp / n
        distinct_ratio = self.n_distinct / 256.0
        entropy = 0.0
        for c in self.hist:
            if c:
                p = c / n
                entropy -= p * math.log2(p)
        entropy /= 8.0
        mean_abs_delta = (self.sum_delta / cp / 255.0) if cp else 0.0
        max_abs_delta = self.max_delta / 255.0
        mean_v = self.sum_v / n
        var = self.sum_v2 / n - mean_v * mean_v
        std_value = math.sqrt(var if var > 0 else 0.0) / 255.0
        extremes_frac = self.extremes / n
        mean_p = self.sum_pop / n
        pvar = self.sum_pop2 / n - mean_p * mean_p
        popcount_std = math.sqrt(pvar if pvar > 0 else 0.0) / 4.0
        return [bit_count / 8.0, jump_ratio, change_ratio, distinct_ratio,
                min(entropy, 1.0), mean_abs_delta, max_abs_delta, std_value,
                extremes_frac, min(popcount_std, 1.0)]

    def verdict(self, mlp=None):
        mask = independent_bit_mask_from_seen(self.seen0, self.seen1)
        rule = looks_like_bit_flags_from_state(
            mask, self.changed_pairs, self.big_jumps, self.n_samples)
        out = {
            "rule_says_bit_flags": rule,
            "classifier_mask": mask if rule else None,
            "n_samples": self.n_samples,
        }
        if mlp is not None:
            feats = self.features()
            out["nn_says_bit_flags"] = bool(mlp.predict(feats)) if feats else False
        return out


# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iface", default="can0")
    ap.add_argument("--model", default=None,
                    help="model_47.json -- bez tego dziala sama regula")
    ap.add_argument("--duration", type=float, default=None)
    ap.add_argument("--snapshot-interval", type=float, default=60.0)
    ap.add_argument("--snapshot-log", default="snapshots_nn.jsonl")
    ap.add_argument("--bench", action="store_true",
                    help="zmierz narzut czasowy na ramke i zakoncz")
    args = ap.parse_args()

    mlp = TinyMLP(args.model) if args.model else None
    if mlp:
        print(f"[start] siec zaladowana: {mlp.n_features} cech, "
              f"{len(mlp.layers)} warstw")
    else:
        print("[start] tylko regula reczna (bez sieci)")

    sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    sock.bind((args.iface,))
    sock.settimeout(1.0)

    stats = {}
    running = [True]

    def stop(_sig, _frm):
        running[0] = False
    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)

    t0 = time.time()
    t_end = t0 + args.duration if args.duration else None
    next_snap = t0 + args.snapshot_interval
    frames = 0
    update_time = 0.0

    while running[0]:
        if t_end and time.time() >= t_end:
            break
        try:
            frame = sock.recv(CAN_FRAME_SIZE)
        except (socket.timeout, TimeoutError):
            continue
        if len(frame) < CAN_FRAME_SIZE:
            continue
        cid, dlc, payload = struct.unpack(CAN_FRAME_FMT, frame)
        cid &= 0x1FFFFFFF
        frames += 1
        ts = time.perf_counter()
        for b in range(min(dlc, 8)):
            key = (cid, b)
            s = stats.get(key)
            if s is None:
                s = stats[key] = ByteStat()
            s.update(payload[b])
        update_time += time.perf_counter() - ts

        now = time.time()
        if now >= next_snap:
            next_snap = now + args.snapshot_interval
            snap = {"elapsed_seconds": round(now - t0, 1), "total_frames": frames,
                    "n_tracked_positions": len(stats), "labels": {}}
            n_rule = n_nn = 0
            for (c, b), s in stats.items():
                v = s.verdict(mlp)
                snap["labels"].setdefault(str(c), {})[str(b)] = v
                n_rule += bool(v["rule_says_bit_flags"])
                n_nn += bool(v.get("nn_says_bit_flags"))
            with open(args.snapshot_log, "a") as f:
                f.write(json.dumps(snap) + "\n")
            msg = (f"[snapshot t={int(now - t0)}s] ramek={frames} "
                   f"pozycji={len(stats)} regula={n_rule}")
            if mlp:
                msg += f" siec={n_nn}"
            print(msg, flush=True)

    sock.close()
    el = time.time() - t0
    print(f"[koniec] ramek={frames} czas={el:.1f}s")
    if frames:
        print(f"[narzut] akumulacja stanu: {update_time / frames * 1e6:.1f} us/ramke "
              f"({update_time / el * 100:.1f}% czasu)")

    if args.bench and mlp:
        # czas samej inferencji sieci na jednej pozycji
        sample = next((s for s in stats.values() if s.n_samples > 1), None)
        if sample:
            feats = sample.features()
            t = time.perf_counter()
            for _ in range(2000):
                mlp.predict(feats)
            dt = (time.perf_counter() - t) / 2000
            print(f"[narzut] inferencja sieci: {dt * 1e6:.1f} us/pozycje")
            t = time.perf_counter()
            for _ in range(2000):
                sample.features()
            dtf = (time.perf_counter() - t) / 2000
            print(f"[narzut] policzenie cech:  {dtf * 1e6:.1f} us/pozycje")
            print(f"[narzut] razem na werdykt: {(dt + dtf) * 1e6:.1f} us "
                  f"(liczony raz na snapshot, nie na ramke)")


if __name__ == "__main__":
    main()
