#!/usr/bin/env python3
"""
Eksperyment 4.5, Faza 3 -- przechwytywanie "prob" (Cold Start windows)
bezposrednio z SocketCAN (can0), zamiast przez WebSocket+ESP32 jak w
capture_live_trials.py (Eksperyment 4.4). Ta sama logika okna (WINDOW_SIZE=30,
round-robin po CAN ID, ten sam ksztalt JSON wyjscia), zeby
evaluate_with_llms.py dzialalo bez zmian.

Uzycie:
  1. python3 capture_live_trials_socketcan.py --iface can0 --n-trials 100 \\
         --seed 999 --n-configs 20 --out live_trials_seed999.json
  2. Rownolegle na drugim terminalu: generate_traffic_diverse.py --iface can0 \\
         --seed 999 --n-configs 20 --duration 3600
"""
import argparse
import json
import time
from collections import defaultdict, deque

import can

import sys
import os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "esp_experiment_4_3"))
from generate_traffic_diverse import make_diverse_configs, ground_truth_json  # noqa: E402

WINDOW_SIZE = 30


class TrialCapture:
    def __init__(self, live_can_ids, n_trials_target):
        self.live_can_ids = set(live_can_ids)
        self.history = defaultdict(lambda: deque(maxlen=WINDOW_SIZE))
        self.frames_since_last_trial = defaultdict(int)
        self.trials = []
        self.n_trials_target = n_trials_target
        self.rr_order = sorted(self.live_can_ids)
        self.rr_idx = 0

    def on_frame(self, can_id, dlc, data_hex, timestamp_us):
        if can_id not in self.live_can_ids:
            return
        frame = {"id": can_id, "dlc": dlc, "data": data_hex, "timestamp": timestamp_us}
        self.history[can_id].append(frame)
        self.frames_since_last_trial[can_id] += 1

    def maybe_collect_trial(self):
        if len(self.trials) >= self.n_trials_target:
            return
        for _ in range(len(self.rr_order)):
            can_id = self.rr_order[self.rr_idx]
            self.rr_idx = (self.rr_idx + 1) % len(self.rr_order)
            if (self.frames_since_last_trial[can_id] >= WINDOW_SIZE
                    and len(self.history[can_id]) >= WINDOW_SIZE):
                frames = list(self.history[can_id])
                trial = {
                    "trialIdx": len(self.trials),
                    "canId": can_id,
                    "triggerFrame": frames[-1],
                    "recentFrames": frames[:-1],
                }
                self.trials.append(trial)
                self.frames_since_last_trial[can_id] = 0
                print(f"  [trial {len(self.trials)}/{self.n_trials_target}] canId=0x{can_id:03x} przechwycony", flush=True)
                return


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iface", default="can0")
    ap.add_argument("--n-trials", type=int, default=100)
    ap.add_argument("--seed", type=int, default=999)
    ap.add_argument("--n-configs", type=int, default=20)
    ap.add_argument("--out", default="live_trials_seed999.json")
    ap.add_argument("--timeout", type=float, default=1800.0, help="max czas oczekiwania [s]")
    args = ap.parse_args()

    configs = make_diverse_configs(seed=args.seed, n_configs=args.n_configs)
    live_can_ids = [cfg.can_id for cfg in configs]
    print(f"Oczekiwane live CAN ID (seed={args.seed}, n_configs={args.n_configs}): "
          f"{[hex(c) for c in live_can_ids]}", flush=True)

    capture = TrialCapture(live_can_ids, args.n_trials)
    bus = can.interface.Bus(channel=args.iface, interface="socketcan")

    t_start = time.time()
    try:
        while len(capture.trials) < args.n_trials:
            if time.time() - t_start > args.timeout:
                print(f"UWAGA: timeout ({args.timeout}s) - zapisuje co udalo sie zebrac "
                      f"({len(capture.trials)}/{args.n_trials})", flush=True)
                break
            msg = bus.recv(timeout=1.0)
            if msg is None or msg.is_error_frame:
                continue
            data_hex = bytes(msg.data).hex()
            capture.on_frame(msg.arbitration_id, msg.dlc, data_hex, int(time.time() * 1e6))
            capture.maybe_collect_trial()
    finally:
        bus.shutdown()

    out = {
        "seed": args.seed,
        "n_configs": args.n_configs,
        "ground_truth": ground_truth_json(configs),
        "trials": capture.trials,
    }
    with open(args.out, "w") as f:
        json.dump(out, f, indent=2)
    print(f"\nZapisano {len(capture.trials)} prob do {args.out}", flush=True)


if __name__ == "__main__":
    main()
