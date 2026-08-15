#!/usr/bin/env python3
"""
Eksperyment 4.11, krok 1 -- zbieranie cech PER BIT z prawdziwego SocketCAN.

PROBLEM: Eksperyment 4.3 (Etap B) zmierzyl trafnosc maski bitowej na **15.8%**.
Obecna maska to `seen0 & seen1`, czyli "kazdy bit, ktory kiedykolwiek sie
zmienil". Ta regula z definicji NIE POTRAFI odroznic flagi od bitu nalezacego
do skalara -- oba sie zmieniaja. Stad tak niska trafnosc, i zadne strojenie
progu tego nie naprawi (progi dzialaja na poziomie BAJTU, nie bitu).

HIPOTEZA: bity skalara sa statystycznie SPRZEZONE, a flagi NIEZALEZNE:
  * w liczniku bit 0 przelacza sie najczesciej, bit 7 najrzadziej --
    powstaje monotoniczny gradient czestosci,
  * zmiany propaguja sie przez PRZENIESIENIA: bit i zmienia sie glownie wtedy,
    gdy bity 0..i-1 sa w stanie 1 (sygnatura inkrementacji),
  * flaga nie wykazuje korelacji z sasiadami.
Wszystkie te cechy sa liczalne PRZYROSTOWO, O(8) na ramke -- tak samo jak
dotychczasowe statystyki per-bajt.

Uzycie:
  python3 collect_bits.py --iface can0 --seed 7 --duration 100 --out bits7.json
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

CAN_FRAME_FMT = "=IB3x8s"
CAN_FRAME_SIZE = struct.calcsize(CAN_FRAME_FMT)

GENERATOR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "esp_experiment_4_3", "generate_traffic_diverse.py")

FEATURE_NAMES = [
    "toggle_rate",        # zmiany tego bitu / liczba probek
    "toggle_rank",        # ranga czestosci wsrod 8 bitow bajtu (0=najrzadszy)
    "toggle_ratio_max",   # toggle_rate / max(toggle_rate) w bajcie
    "duty",               # udzial probek, w ktorych bit = 1
    "carry_lift",         # P(zmiana | ponizsze bity same jedynki) - P(zmiana | inaczej)
    "co_change_lower",    # udzial zmian wspolnych z bitem i-1
    "co_change_upper",    # udzial zmian wspolnych z bitem i+1
    "n_toggling_norm",    # ile bitow bajtu w ogole sie zmienia / 8
    "monotonicity",       # jak monotoniczny jest gradient czestosci w bajcie
    "bit_index",          # pozycja bitu / 7
    "byte_jump_ratio",    # big_jumps/changed_pairs calego bajtu (kontekst)
]


def ground_truth_bits(seed, n_configs):
    """Etykieta per (can_id, bajt, bit): 1 = niezalezna flaga bitowa.
    Bity skalarow (pelnych i czesciowych) oraz nieuzywane -> 0."""
    with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as tf:
        tmp = tf.name
    subprocess.run([sys.executable, GENERATOR, "--seed", str(seed),
                    "--n-configs", str(n_configs), "--dump-json", tmp,
                    "--dump-samples-per-id", "2"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    corpus = json.load(open(tmp))
    os.unlink(tmp)

    flags = set()      # (can_id, byte, bit) -> flaga
    scalar_bits = set()  # bity nalezace do skalarow (pelnych/czesciowych)
    dlc_of = {}
    for cfg in corpus["ground_truth"]:
        cid = cfg["can_id"]
        dlc_of[cid] = cfg.get("dlc", 8)
        for s in cfg["signals"]:
            k = s["kind"]
            if k == "bit_flag":
                flags.add((cid, s["byte_idx"], s["bit_idx"]))
            elif k == "partial_scalar":
                mask = s["bit_mask"]
                for b in range(8):
                    if mask >> b & 1:
                        scalar_bits.add((cid, s["byte_idx"], b))
            elif k == "scalar":
                for off in range(s.get("byte_len", 1)):
                    for b in range(8):
                        scalar_bits.add((cid, s["byte_idx"] + off, b))
    return flags, scalar_bits, dlc_of


class ByteBits:
    """Liczniki per bajt i per bit. Wszystko przyrostowe, pamiec stala."""
    __slots__ = ("n", "prev", "has_prev", "changes", "ones", "carry_change",
                 "carry_n", "co_lower", "changed_pairs", "big_jumps")

    def __init__(self):
        self.n = 0
        self.prev = 0
        self.has_prev = False
        self.changes = [0] * 8
        self.ones = [0] * 8
        self.carry_change = [0] * 8   # bit zmienil sie, gdy ponizsze same 1
        self.carry_n = [0] * 8        # ile razy ponizsze byly same 1
        self.co_lower = [0] * 8       # bit i zmienil sie RAZEM z bitem i-1
        self.changed_pairs = 0
        self.big_jumps = 0

    def update(self, v):
        self.n += 1
        for b in range(8):
            if v >> b & 1:
                self.ones[b] += 1
        if self.has_prev:
            p = self.prev
            diff = p ^ v
            if diff:
                self.changed_pairs += 1
                d = p - v
                if d < 0:
                    d = -d
                if d > 3:
                    self.big_jumps += 1
            for b in range(8):
                changed = (diff >> b) & 1
                if changed:
                    self.changes[b] += 1
                    if b > 0 and (diff >> (b - 1)) & 1:
                        self.co_lower[b] += 1
                if b > 0:
                    lower = (1 << b) - 1
                    if (p & lower) == lower:
                        self.carry_n[b] += 1
                        if changed:
                            self.carry_change[b] += 1
        self.prev = v
        self.has_prev = True

    def bit_features(self):
        """Zwraca liste 8 wektorow cech (albo None, jesli za malo probek)."""
        n = self.n
        if n < 2:
            return None
        rates = [c / n for c in self.changes]
        max_rate = max(rates) or 1.0
        order = sorted(range(8), key=lambda i: rates[i])
        rank = {b: i / 7.0 for i, b in enumerate(order)}
        n_toggling = sum(1 for r in rates if r > 0)
        # monotonicznosc: ile par (i, i+1) ma rate malejacy -- licznik ma 7/7
        mono = sum(1 for i in range(7) if rates[i] >= rates[i + 1]) / 7.0
        byte_jump = (self.big_jumps / self.changed_pairs) if self.changed_pairs else 0.0

        out = []
        for b in range(8):
            ch = self.changes[b]
            # sygnatura przeniesienia
            p_carry = (self.carry_change[b] / self.carry_n[b]) if self.carry_n[b] else 0.0
            other_n = n - self.carry_n[b]
            other_ch = ch - self.carry_change[b]
            p_other = (other_ch / other_n) if other_n > 0 else 0.0
            carry_lift = max(min(p_carry - p_other, 1.0), -1.0)
            co_low = (self.co_lower[b] / ch) if ch else 0.0
            co_up = (self.co_lower[b + 1] / ch) if (b < 7 and ch) else 0.0
            out.append([
                rates[b],
                rank[b],
                rates[b] / max_rate,
                self.ones[b] / n,
                (carry_lift + 1.0) / 2.0,   # do [0,1]
                co_low,
                co_up,
                n_toggling / 8.0,
                mono,
                b / 7.0,
                byte_jump,
            ])
        return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iface", required=True)
    ap.add_argument("--seed", type=int, required=True)
    ap.add_argument("--n-configs", type=int, default=20)
    ap.add_argument("--duration", type=float, default=100.0)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    flags, scalar_bits, dlc_of = ground_truth_bits(args.seed, args.n_configs)

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
            except (socket.timeout, TimeoutError):
                continue
            if len(frame) < CAN_FRAME_SIZE:
                continue
            cid, dlc, payload = struct.unpack(CAN_FRAME_FMT, frame)
            cid &= 0x1FFFFFFF
            frames += 1
            for b in range(min(dlc, dlc_of.get(cid, 8))):
                key = (cid, b)
                a = acc.get(key)
                if a is None:
                    a = acc[key] = ByteBits()
                a.update(payload[b])
    finally:
        sock.close()

    rows = []
    for (cid, byte), a in acc.items():
        feats = a.bit_features()
        if feats is None:
            continue
        seen_mask_bits = [1 if a.changes[b] > 0 else 0 for b in range(8)]
        for bit in range(8):
            rows.append({
                "can_id": cid, "byte": byte, "bit": bit, "seed": args.seed,
                "features": feats[bit],
                "label": 1 if (cid, byte, bit) in flags else 0,
                "is_scalar_bit": 1 if (cid, byte, bit) in scalar_bits else 0,
                # odniesienie: obecna maska = bit kiedykolwiek sie zmienil
                "baseline_seen": seen_mask_bits[bit],
            })

    pos = sum(r["label"] for r in rows)
    with open(args.out, "w") as f:
        json.dump({"feature_names": FEATURE_NAMES, "rows": rows,
                   "frames": frames, "seed": args.seed}, f)
    print(f"seed={args.seed} iface={args.iface}: {frames} ramek, "
          f"{len(rows)} bitow, {pos} flag")


if __name__ == "__main__":
    main()
