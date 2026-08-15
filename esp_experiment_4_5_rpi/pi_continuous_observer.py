#!/usr/bin/env python3
"""
Eksperyment 4.5, Faza 1 -- ciagly (nie per-sesja) demon obserwujacy can0 na
Raspberry Pi Zero, budujacy per-bajt statystyki "Kierunku B" identyczne z
etap_b_autolabel.py / DecodingAccuracyRunner.cpp, ale w formie INKREMENTALNEJ
(O(1) pamieci i czasu na klatke), tak zeby dzialac godzinami na sprzecie
z 512MB RAM / jednym rdzeniem ARM11 bez rosnacego zuzycia pamieci/CPU.

Rownowaznosc matematyczna z wersja offline (etap_b_autolabel.py):
  - seen0/seen1 to dokladnie te same bitmaski co w independent_bit_mask(),
    aktualizowane przyrostowo zamiast przeliczane od zera z calej listy.
  - changed_pairs/big_jumps to te same liczniki co w looks_like_bit_flags(),
    rowniez aktualizowane przyrostowo (potrzebuja tylko poprzedniej wartosci).
  - independent_bit_mask()/looks_like_bit_flags() w tym pliku sa 1:1 tymi
    samymi funkcjami co w etap_b_autolabel.py (verdict liczony na zadanie
    z biezacego stanu, nie z historii probek).

Uzycie:
  python3 pi_continuous_observer.py --iface can0 --state state.json \\
      --save-interval 30 --snapshot-interval 60 --snapshot-log snapshots.jsonl
"""
import argparse
import json
import os
import time
import signal
import sys

import can


def independent_bit_mask_from_seen(seen0: int, seen1: int) -> int:
    """1:1 rownowaznik independent_bit_mask() z etap_b_autolabel.py,
    liczony z JUZ zaakumulowanych seen0/seen1 zamiast z listy probek."""
    return seen0 & seen1


# Strojenie progu (Eksperyment 4.5, analiza po godzinnym przebiegu, seed=999):
# przy progu <=0.46 recall = 85% (17/20) z precyzja wciaz 100% (0 FP); dopiero
# >=0.47 zaczyna sie utrata prawdziwych trafien (urwisko dokladnie 0.46->0.47).
# Domyslne 0.5 z oryginalnej wersji (etap_b_autolabel.py/DecodingAccuracyRunner.cpp)
# ucinalo 5 z 20 prawdziwych flag bez zadnej korzysci w precyzji - czysta strata.
# 0.3 zostawia margines bezpieczenstwa od urwiska (0.46) i wciaz odrzuca sygnaly
# o samych drobnych, plynnych zmianach (np. powolny drift skalara).
BIG_JUMP_RATIO_THRESHOLD = 0.3


def looks_like_bit_flags_from_state(mask: int, changed_pairs: int, big_jumps: int, n_samples: int) -> bool:
    """Rownowaznik looks_like_bit_flags() z etap_b_autolabel.py, z poprawionym
    (postrojonym) progiem - patrz BIG_JUMP_RATIO_THRESHOLD wyzej."""
    if n_samples < 2:
        return False
    bit_count = bin(mask).count("1")
    if bit_count < 2 or bit_count > 6:
        return False
    if changed_pairs == 0:
        return False
    return (big_jumps / changed_pairs) >= BIG_JUMP_RATIO_THRESHOLD


class ByteStat:
    __slots__ = ("seen0", "seen1", "prev", "has_prev", "changed_pairs", "big_jumps", "n_samples")

    def __init__(self):
        self.seen0 = 0
        self.seen1 = 0
        self.prev = None
        self.has_prev = False
        self.changed_pairs = 0
        self.big_jumps = 0
        self.n_samples = 0

    def update(self, v: int):
        for b in range(8):
            if v & (1 << b):
                self.seen1 |= (1 << b)
            else:
                self.seen0 |= (1 << b)
        if self.has_prev and self.prev != v:
            self.changed_pairs += 1
            if abs(self.prev - v) > 3:
                self.big_jumps += 1
        self.prev = v
        self.has_prev = True
        self.n_samples += 1

    def verdict(self):
        mask = independent_bit_mask_from_seen(self.seen0, self.seen1)
        is_flags = looks_like_bit_flags_from_state(mask, self.changed_pairs, self.big_jumps, self.n_samples)
        return {
            "classifier_says_bit_flags": is_flags,
            "classifier_mask": mask if is_flags else None,
            "n_samples": self.n_samples,
        }

    def to_dict(self):
        return {
            "seen0": self.seen0, "seen1": self.seen1,
            "prev": self.prev, "has_prev": self.has_prev,
            "changed_pairs": self.changed_pairs, "big_jumps": self.big_jumps,
            "n_samples": self.n_samples,
        }

    @classmethod
    def from_dict(cls, d):
        s = cls()
        s.seen0 = d["seen0"]; s.seen1 = d["seen1"]
        s.prev = d["prev"]; s.has_prev = d["has_prev"]
        s.changed_pairs = d["changed_pairs"]; s.big_jumps = d["big_jumps"]
        s.n_samples = d["n_samples"]
        return s


class Observer:
    def __init__(self):
        self.stats = {}  # (can_id, byte_idx) -> ByteStat, klucz jako "id:idx" po serializacji
        self.start_time = time.time()
        self.total_frames = 0

    def on_frame(self, can_id: int, data: bytes):
        self.total_frames += 1
        for idx, v in enumerate(data):
            key = (can_id, idx)
            st = self.stats.get(key)
            if st is None:
                st = ByteStat()
                self.stats[key] = st
            st.update(v)

    def snapshot(self):
        elapsed = time.time() - self.start_time
        per_id = {}
        for (can_id, idx), st in self.stats.items():
            per_id.setdefault(str(can_id), {})[str(idx)] = st.verdict()
        return {
            "elapsed_seconds": round(elapsed, 1),
            "total_frames": self.total_frames,
            "n_tracked_positions": len(self.stats),
            "labels": per_id,
        }

    def save_state(self, path):
        tmp = path + ".tmp"
        state = {
            "start_time": self.start_time,
            "total_frames": self.total_frames,
            "stats": {f"{cid}:{idx}": st.to_dict() for (cid, idx), st in self.stats.items()},
        }
        with open(tmp, "w") as f:
            json.dump(state, f)
        os.replace(tmp, path)

    def load_state(self, path):
        with open(path) as f:
            state = json.load(f)
        self.start_time = state["start_time"]
        self.total_frames = state["total_frames"]
        self.stats = {}
        for key, d in state["stats"].items():
            cid_s, idx_s = key.split(":")
            self.stats[(int(cid_s), int(idx_s))] = ByteStat.from_dict(d)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iface", default="can0")
    ap.add_argument("--state", default="observer_state.json", help="plik trwalego stanu (przezywa restart)")
    ap.add_argument("--save-interval", type=float, default=30.0, help="co ile sekund zapisywac stan na dysk")
    ap.add_argument("--snapshot-interval", type=float, default=60.0, help="co ile sekund logowac werdykt klasyfikatora")
    ap.add_argument("--snapshot-log", default="snapshots.jsonl", help="plik jsonl z historia snapshotow w czasie")
    ap.add_argument("--duration", type=float, default=None, help="opcjonalny limit czasu [s], domyslnie brak (dziala w nieskonczonosc)")
    args = ap.parse_args()

    obs = Observer()
    if os.path.exists(args.state):
        obs.load_state(args.state)
        print(f"[wznowiono] {obs.total_frames} ramek, {len(obs.stats)} sledzonych pozycji, "
              f"czas trwania dotychczas: {time.time()-obs.start_time:.0f}s", flush=True)
    else:
        print("[start] nowa sesja obserwacji", flush=True)

    running = True

    def handle_sigterm(signum, frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGTERM, handle_sigterm)
    signal.signal(signal.SIGINT, handle_sigterm)

    bus = can.interface.Bus(channel=args.iface, interface="socketcan")

    last_save = last_snapshot = time.time()
    t_start = time.time()

    try:
        while running:
            if args.duration is not None and (time.time() - t_start) > args.duration:
                break
            msg = bus.recv(timeout=1.0)
            if msg is not None and not msg.is_error_frame:
                obs.on_frame(msg.arbitration_id, bytes(msg.data))

            now = time.time()
            if now - last_save >= args.save_interval:
                obs.save_state(args.state)
                last_save = now
            if now - last_snapshot >= args.snapshot_interval:
                snap = obs.snapshot()
                with open(args.snapshot_log, "a") as f:
                    f.write(json.dumps(snap) + "\n")
                n_flags = sum(
                    1 for byid in snap["labels"].values() for v in byid.values()
                    if v["classifier_says_bit_flags"]
                )
                print(f"[snapshot t={snap['elapsed_seconds']:.0f}s] ramek={snap['total_frames']} "
                      f"pozycji={snap['n_tracked_positions']} bit_flags_wykryte={n_flags}", flush=True)
                last_snapshot = now
    finally:
        obs.save_state(args.state)
        bus.shutdown()
        print(f"[koniec] zapisano stan: {args.state}", flush=True)


if __name__ == "__main__":
    main()
