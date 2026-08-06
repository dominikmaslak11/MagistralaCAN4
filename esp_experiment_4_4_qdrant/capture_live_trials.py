#!/usr/bin/env python3
"""
Eksperyment 4.4 (Faza A) — przechwytywanie zywych "okien" Cold Start z
prawdziwego ESP32+MCP2515, sniffujacego prawdziwa magistrale CAN (PEAK
PCAN-USB jako generator ruchu). Serwer WebSocket zgodny z protokolem
esp_experiment_1_1.ino (NIEZMIENIONE firmware, dokladnie jak w Eksperymencie
4.1 - patrz naglowek tego pliku .ino).

WAZNE (celowa decyzja metodologiczna): sygnaly generowane na ZYWO uzywaja
INNEGO ziarna (seed=999) niz korpus offline (seed=42) uzyty do zbudowania
biblioteki Qdrant w qdrant_warmstart_diverse.py - dzieki temu test
"warm-start" mierzy GENERALIZACJE do podobnych, ale nie identycznych
sygnalow, a nie trywialne dopasowanie 1:1.

Kazda zebrana "proba" (trial) to: canId, trigger frame (ostatnia odebrana
ramka tego ID), lista recentFrames (poprzednie ramki TEGO SAMEGO ID - bufor
IZOLOWANY per-ID, zgodnie z poprawka z Eksperyment_4.1_Naprawa_Kontekstu),
oraz ground truth (z lokalnie wygenerowanej konfiguracji, tym samym seedem
co live traffic).

Uzycie:
  1. W jednym terminalu: python3 capture_live_trials.py --n-trials 100
     (czeka na polaczenie ESP32 przez WebSocket na porcie 9000)
  2. W drugim terminalu (po starcie serwera): uruchom generator na CAN0:
     python3 ../esp_experiment_4_3/generate_traffic_diverse.py --iface can0 \\
         --seed 999 --n-configs 8 --duration 3600
  3. ESP32 (esp_experiment_1_1.ino, sekrety zaktualizowane) laczy sie przez
     WiFi i zaczyna przesylac ramki.
"""
import argparse
import asyncio
import json
import sys
import os
import time
from collections import defaultdict, deque

import websockets

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "esp_experiment_4_3"))
from generate_traffic_diverse import make_diverse_configs, ground_truth_json  # noqa: E402

WINDOW_SIZE = 30  # rozmiar bufora "recentFrames" per CAN ID (jak w 4.1 po poprawce)


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
        """Round-robin: sprawdza kolejne CAN ID, czy nazbieralo sie dosc
        nowych ramek od ostatniej proby (imitujac ponowny Cold Start dla
        tego ID), i jesli tak - zapisuje probe (trigger + recentFrames)."""
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
                print(f"  [trial {len(self.trials)}/{self.n_trials_target}] canId=0x{can_id:03x} przechwycony")
                return


async def handle_client(websocket, capture: TrialCapture, done_event):
    print(f"[WS] ESP32 polaczony: {websocket.remote_address}")
    async for message in websocket:
        try:
            msg = json.loads(message)
        except json.JSONDecodeError:
            continue

        mtype = msg.get("type")
        if mtype == "time_sync":
            reply = {"type": "time_sync_ack", "espTime": msg.get("espTime"),
                     "serverTime": int(time.time() * 1e6)}
            await websocket.send(json.dumps(reply))
        elif mtype == "send_frame":
            can_id = msg["id"]
            capture.on_frame(can_id, msg["dlc"], msg["data"], msg["timestamp"])
            await websocket.send(json.dumps({"type": "frame_ack", "id": can_id, "status": "ok"}))
            capture.maybe_collect_trial()
            if len(capture.trials) >= capture.n_trials_target and not done_event.is_set():
                done_event.set()
        elif mtype == "rule_ack":
            pass


async def main_async(args):
    configs = make_diverse_configs(seed=args.seed, n_configs=args.n_configs)
    live_can_ids = [cfg.can_id for cfg in configs]
    print(f"Oczekiwane live CAN ID (seed={args.seed}, n_configs={args.n_configs}): "
          f"{[hex(c) for c in live_can_ids]}")

    capture = TrialCapture(live_can_ids, args.n_trials)
    done_event = asyncio.Event()

    async def handler(ws):
        await handle_client(ws, capture, done_event)

    print(f"[WS] Serwer nasluchuje na 0.0.0.0:{args.port} - czekam na ESP32...")
    async with websockets.serve(handler, "0.0.0.0", args.port):
        await done_event.wait()

    out = {
        "seed": args.seed,
        "n_configs": args.n_configs,
        "ground_truth": ground_truth_json(configs),
        "trials": capture.trials,
    }
    with open(args.out, "w") as f:
        json.dump(out, f, indent=2)
    print(f"\nZapisano {len(capture.trials)} prob do {args.out}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=9000)
    ap.add_argument("--n-trials", type=int, default=100)
    ap.add_argument("--seed", type=int, default=999, help="INNY niz korpus offline (42) - test generalizacji")
    ap.add_argument("--n-configs", type=int, default=8, help="ile CAN ID nadawac na zywo (male, zeby nie przeciazyc magistrali)")
    ap.add_argument("--out", default="live_trials_captured.json")
    args = ap.parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
