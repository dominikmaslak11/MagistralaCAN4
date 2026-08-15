#!/usr/bin/env python3
"""
Eksperyment 4.7, krok 1b -- zbieranie danych uczacych z PRAWDZIWEGO SocketCAN.

DLACZEGO NIE Z --dump-json: tryb offline generatora symuluje czas skokowo
(fake_dt=0.05 s/probke) i daje ~200 probek na bajt. Na zywej magistrali ramki
plyna z ~23 Hz i zbieramy ich dziesiatki tysiecy. Zmierzono empirycznie
(2026-08-14), ze ta sama regula reczna daje:
    dane offline (200 probek):  Recall 42.6%, Precision 74.3%
    zywa magistrala (1h):       Recall 85.0%, Precision 100.0%
czyli rozklady sa ISTOTNIE ROZNE. Uczenie sieci na danych offline oznaczaloby
trenowanie na innym rozkladzie niz docelowy -- klasyczny blad metodologiczny.

Ten skrypt slucha realnego interfejsu (fizycznego albo vcan) i akumuluje
statystyki per (CAN ID, bajt) przyrostowo, dokladnie jak pi_continuous_observer.
Etykiety bierze ze schematu ground truth (ktory NIE zalezy od taktowania).

Uzycie:
  python3 collect_live.py --iface vcan0 --seed 7 --duration 300 --out seed7.json
"""
import argparse
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
from collections import Counter

# Surowe gniazdo SocketCAN -- ten sam mechanizm, ktorego uzywa
# generate_traffic_diverse.py po stronie nadawczej. Swiadomie BEZ python-can:
# jedna zaleznosc mniej, identyczny format ramki po obu stronach.
CAN_FRAME_FMT = "=IB3x8s"
CAN_FRAME_SIZE = struct.calcsize(CAN_FRAME_FMT)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_dataset import byte_features, FEATURE_NAMES  # noqa: E402

GENERATOR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "esp_experiment_4_3", "generate_traffic_diverse.py"
)


def ground_truth_for_seed(seed, n_configs):
    """Etykiety dla ziarna. Konfiguracje sa deterministyczne od ziarna, wiec
    schemat da sie wziac z trybu offline -- etykiety NIE zaleza od taktowania,
    w odroznieniu od statystyk wartosci."""
    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tf:
        tmp = tf.name
    subprocess.run(
        [sys.executable, GENERATOR, "--seed", str(seed), "--n-configs", str(n_configs),
         "--dump-json", tmp, "--dump-samples-per-id", "2"],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with open(tmp) as f:
        corpus = json.load(f)
    os.unlink(tmp)
    flag_bytes = set()
    dlc_of = {}
    for cfg in corpus["ground_truth"]:
        dlc_of[cfg["can_id"]] = cfg.get("dlc", 8)
        for s in cfg["signals"]:
            if s["kind"] == "bit_flag":
                flag_bytes.add((cfg["can_id"], s["byte_idx"]))
    return flag_bytes, dlc_of


class ByteAcc:
    """Akumulator per bajt. Counter wartosci jest ograniczony do 256 pozycji,
    wiec pamiec pozostaje stala niezaleznie od dlugosci przebiegu."""
    __slots__ = ("counts", "prev", "changed_pairs", "big_jumps", "n",
                 "sum_v", "sum_v2", "sum_delta", "max_delta",
                 "sum_pop", "sum_pop2", "extremes")

    def __init__(self):
        self.counts = Counter()
        self.prev = None
        self.changed_pairs = 0
        self.big_jumps = 0
        self.n = 0
        self.sum_v = 0
        self.sum_v2 = 0
        self.sum_delta = 0
        self.max_delta = 0
        self.sum_pop = 0
        self.sum_pop2 = 0
        self.extremes = 0

    def update(self, v):
        self.counts[v] += 1
        self.n += 1
        self.sum_v += v
        self.sum_v2 += v * v
        p = bin(v).count("1")
        self.sum_pop += p
        self.sum_pop2 += p * p
        if v in (0x00, 0xFF):
            self.extremes += 1
        if self.prev is not None and self.prev != v:
            self.changed_pairs += 1
            d = abs(self.prev - v)
            self.sum_delta += d
            if d > self.max_delta:
                self.max_delta = d
            if d > 3:
                self.big_jumps += 1
        self.prev = v

    def state(self):
        seen0 = seen1 = 0
        for v in self.counts:
            for b in range(8):
                if v & (1 << b):
                    seen1 |= (1 << b)
                else:
                    seen0 |= (1 << b)
        return {"seen0": seen0, "seen1": seen1,
                "changed_pairs": self.changed_pairs, "big_jumps": self.big_jumps,
                "n_samples": self.n}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iface", required=True)
    ap.add_argument("--seed", type=int, required=True)
    ap.add_argument("--n-configs", type=int, default=30)
    ap.add_argument("--duration", type=float, default=300.0)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    flag_bytes, dlc_of = ground_truth_for_seed(args.seed, args.n_configs)

    sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    sock.bind((args.iface,))
    sock.settimeout(1.0)

    acc = {}
    t_end = time.time() + args.duration
    frames = 0
    try:
        while time.time() < t_end:
            try:
                frame = sock.recv(CAN_FRAME_SIZE)
            except socket.timeout:
                continue
            if len(frame) < CAN_FRAME_SIZE:
                continue
            cid, dlc, payload = struct.unpack(CAN_FRAME_FMT, frame)
            cid &= 0x1FFFFFFF  # maska flag EFF/RTR/ERR
            frames += 1
            n = min(dlc, dlc_of.get(cid, 8))
            for b in range(n):
                key = (cid, b)
                a = acc.get(key)
                if a is None:
                    a = acc[key] = ByteAcc()
                a.update(payload[b])
    finally:
        sock.close()

    rows = []
    for (cid, b), a in acc.items():
        # cechy licze z rozkladu wartosci (Counter) -- rownowaznie do listy probek
        values = list(a.counts.elements())
        feats = byte_features_from_acc(a, values)
        if feats is None:
            continue
        rows.append({
            "can_id": cid, "byte": b, "seed": args.seed,
            "features": feats,
            "label": 1 if (cid, b) in flag_bytes else 0,
            "state": a.state(),
        })

    pos = sum(r["label"] for r in rows)
    with open(args.out, "w") as f:
        json.dump({"feature_names": FEATURE_NAMES, "rows": rows,
                   "frames": frames, "seed": args.seed}, f)
    print(f"seed={args.seed} iface={args.iface}: {frames} ramek, "
          f"{len(rows)} pozycji, {pos} z flagami")


def byte_features_from_acc(a, values):
    """Cechy liczone z akumulatora -- te same definicje co byte_features(),
    ale bez trzymania pelnej historii (poza rozkladem wartosci)."""
    import math
    n = a.n
    if n < 2:
        return None
    st = a.state()
    mask = st["seen0"] & st["seen1"]
    bit_count = bin(mask).count("1")
    jump_ratio = (a.big_jumps / a.changed_pairs) if a.changed_pairs else 0.0
    change_ratio = a.changed_pairs / n
    distinct_ratio = len(a.counts) / 256.0
    entropy = 0.0
    for c in a.counts.values():
        p = c / n
        entropy -= p * math.log2(p)
    entropy /= 8.0
    mean_abs_delta = (a.sum_delta / a.changed_pairs / 255.0) if a.changed_pairs else 0.0
    max_abs_delta = a.max_delta / 255.0
    mean_v = a.sum_v / n
    var = max(a.sum_v2 / n - mean_v * mean_v, 0.0)
    std_value = math.sqrt(var) / 255.0
    extremes_frac = a.extremes / n
    mean_p = a.sum_pop / n
    pvar = max(a.sum_pop2 / n - mean_p * mean_p, 0.0)
    popcount_std = math.sqrt(pvar) / 4.0
    return [bit_count / 8.0, jump_ratio, change_ratio, distinct_ratio,
            min(entropy, 1.0), mean_abs_delta, max_abs_delta, std_value,
            extremes_frac, min(popcount_std, 1.0)]


if __name__ == "__main__":
    main()
