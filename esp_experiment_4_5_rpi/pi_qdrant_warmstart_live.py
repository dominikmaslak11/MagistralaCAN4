#!/usr/bin/env python3
"""
Eksperyment 4.5, Faza 2 -- Qdrant warm-start budowany NA BIEZACO z tego
samego, zywego przebiegu na can0 (Raspberry Pi Zero), zamiast z osobnego
korpusu offline (jak w Eksperymencie 4.4, gdzie biblioteka z seed=42 byla
testowana na zywych probkach z seed=999 -> spadek trafnosci do ~70%).

Feature vector i procedura leave-one-out sa 1:1 przeniesione z
esp_experiment_4_4_qdrant/qdrant_warmstart_diverse.py (ta sama definicja
7-wymiarowego wektora cech, ta sama metryka COSINE, to samo query top-6 +
odrzucenie samego siebie).

WAZNE ZALOZENIE (identyczne jak w 4.4, jawnie udokumentowane tam): granice
bajtowe/bitowe sygnalu (schemat: can_id, byte_idx, kind, ...) sa juz znane
-- pochodza z pliku ground-truth (schema BEZ probek wartosci, wygenerowanego
przez generate_traffic_diverse.ground_truth_json). Same WARTOSCI sygnalow
klient buduje wylacznie z ramek faktycznie odebranych na can0 -- nic nie jest
"podglądane" z generatora poza definicja schematu.

Uzycie (na Pi, po uruchomieniu generatora na laptopie z tym samym seedem):
  python3 pi_qdrant_warmstart_live.py --iface can0 \\
      --ground-truth ground_truth_seed999.json \\
      --min-samples 30 --eval-interval 60 --eval-log warmstart_eval.jsonl
"""
import argparse
import json
import statistics
import time
from collections import deque, Counter

from qdrant_client import QdrantClient
from qdrant_client.models import Distance, VectorParams, PointStruct
import can


WINDOW = 60  # rozmiar bufora probek na sygnal (ograniczona pamiec, jak WINDOW_FRAMES w prototypie 4.4)


def extract_value(data: bytes, sig: dict):
    """1:1 logika z extract_raw_series() w qdrant_warmstart_diverse.py,
    ale na POJEDYNCZEJ ramce zamiast na calej liscie."""
    kind = sig["kind"]
    byte_idx = sig["byte_idx"]
    if byte_idx >= len(data):
        return None
    if kind == "bit_flag":
        return (data[byte_idx] >> sig["bit_idx"]) & 1
    if kind == "partial_scalar":
        mask = sig["bit_mask"]
        shift = (mask & -mask).bit_length() - 1
        return (data[byte_idx] & mask) >> shift
    # scalar
    byte_len = sig["byte_len"]
    chunk = data[byte_idx:byte_idx + byte_len]
    if len(chunk) < byte_len:
        return None
    raw = 0
    if sig["little_endian"]:
        for i, b in enumerate(chunk):
            raw |= b << (8 * i)
    else:
        for b in chunk:
            raw = (raw << 8) | b
    return raw


def max_raw_for(sig: dict) -> int:
    if sig["kind"] == "bit_flag":
        return 1
    if sig["kind"] == "partial_scalar":
        mask = sig["bit_mask"]
        return mask >> ((mask & -mask).bit_length() - 1)
    return (1 << (8 * sig["byte_len"])) - 1


def feature_vector(values, max_raw):
    """1:1 z qdrant_warmstart_diverse.feature_vector()."""
    n = len(values)
    deltas = [values[i + 1] - values[i] for i in range(n - 1)]
    abs_deltas = [abs(d) for d in deltas]
    distinct_ratio = len(set(values)) / n
    dwell_fraction = sum(1 for d in deltas if d == 0) / max(1, len(deltas))
    sign_changes = sum(
        1 for i in range(len(deltas) - 1)
        if deltas[i] != 0 and deltas[i + 1] != 0 and (deltas[i] > 0) != (deltas[i + 1] > 0)
    )
    oscillation_rate = sign_changes / max(1, len(deltas) - 1)
    value_range = (max(values) - min(values)) if values else 0
    denom = max(1, max_raw)
    return [
        distinct_ratio,
        dwell_fraction,
        oscillation_rate,
        (statistics.mean(abs_deltas) if abs_deltas else 0.0) / denom,
        (statistics.pstdev(values) if n > 1 else 0.0) / denom,
        value_range / denom,
        (max(abs_deltas) / denom) if abs_deltas else 0.0,
    ]


def leave_one_out_eval(client, collection, instances):
    """1:1 ewaluacja z qdrant_warmstart_diverse.py: top-6 zapytanie,
    odrzucenie samego siebie, macierz pomylek scalar/partial_scalar/bit_flag
    + trafnosc dyskretny/ciagly."""
    confusion = Counter()
    discrete_correct = discrete_total = 0
    for i, inst in enumerate(instances):
        result = client.query_points(collection_name=collection, query=inst["vector"], limit=6)
        neighbor = None
        for p in result.points:
            if p.id != i:
                neighbor = p
                break
        if neighbor is None:
            continue
        confusion[(inst["kind"], neighbor.payload["kind"])] += 1
        discrete_total += 1
        if inst["is_discrete"] == neighbor.payload["is_discrete"]:
            discrete_correct += 1

    kinds = ["scalar", "partial_scalar", "bit_flag"]
    total = sum(confusion.values())
    correct_kind = sum(confusion.get((k, k), 0) for k in kinds)
    return {
        "n": total,
        "kind_accuracy_3way": correct_kind / total if total else None,
        "discrete_accuracy_2way": discrete_correct / discrete_total if discrete_total else None,
        "confusion": {f"{a}->{b}": c for (a, b), c in confusion.items()},
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iface", default="can0")
    ap.add_argument("--ground-truth", required=True, help="JSON schema (can_id/dlc/signals), BEZ probek wartosci")
    ap.add_argument("--min-samples", type=int, default=30, help="min. probek na sygnal zanim wejdzie do ewaluacji")
    ap.add_argument("--eval-interval", type=float, default=60.0, help="co ile sekund uruchamiac ewaluacje leave-one-out")
    ap.add_argument("--eval-log", default="warmstart_eval.jsonl")
    ap.add_argument("--duration", type=float, default=None)
    args = ap.parse_args()

    with open(args.ground_truth) as f:
        configs = json.load(f)

    can_id_to_sigs = {}
    for cfg in configs:
        can_id_to_sigs[cfg["can_id"]] = cfg["signals"]
    total_signals = sum(len(s) for s in can_id_to_sigs.values())
    print(f"[start] wczytano schemat: {len(configs)} CAN ID, {total_signals} sygnalow (bez probek wartosci)", flush=True)

    buffers = {}  # (can_id, sig_name) -> deque
    for can_id, sigs in can_id_to_sigs.items():
        for sig in sigs:
            buffers[(can_id, sig["name"])] = deque(maxlen=WINDOW)

    client = QdrantClient(path="/tmp/qdrant_live_warmstart")
    collection = "live_signatures"

    bus = can.interface.Bus(channel=args.iface, interface="socketcan")
    t_start = time.time()
    last_eval = t_start
    total_frames = 0

    try:
        while True:
            if args.duration is not None and (time.time() - t_start) > args.duration:
                break
            msg = bus.recv(timeout=1.0)
            if msg is not None and not msg.is_error_frame:
                total_frames += 1
                sigs = can_id_to_sigs.get(msg.arbitration_id)
                if sigs:
                    data = bytes(msg.data)
                    for sig in sigs:
                        v = extract_value(data, sig)
                        if v is not None:
                            buffers[(msg.arbitration_id, sig["name"])].append(v)

            now = time.time()
            if now - last_eval >= args.eval_interval:
                last_eval = now
                instances = []
                for can_id, sigs in can_id_to_sigs.items():
                    for sig in sigs:
                        buf = buffers[(can_id, sig["name"])]
                        if len(buf) < args.min_samples:
                            continue
                        vec = feature_vector(list(buf), max_raw_for(sig))
                        instances.append({
                            "kind": sig["kind"], "is_discrete": sig["is_discrete"],
                            "name": sig["name"], "vector": vec,
                        })
                elapsed = now - t_start
                if len(instances) < 2:
                    print(f"[eval t={elapsed:.0f}s] za malo sygnalow z wystarczajaca liczba probek "
                          f"({len(instances)}), pomijam", flush=True)
                    continue

                if client.collection_exists(collection):
                    client.delete_collection(collection)
                client.create_collection(collection, vectors_config=VectorParams(size=7, distance=Distance.COSINE))
                client.upsert(collection, points=[
                    PointStruct(id=i, vector=inst["vector"],
                                payload={"kind": inst["kind"], "is_discrete": inst["is_discrete"], "name": inst["name"]})
                    for i, inst in enumerate(instances)
                ])
                metrics = leave_one_out_eval(client, collection, instances)
                metrics["elapsed_seconds"] = round(elapsed, 1)
                metrics["total_frames"] = total_frames
                with open(args.eval_log, "a") as f:
                    f.write(json.dumps(metrics) + "\n")
                acc3 = metrics["kind_accuracy_3way"]
                acc2 = metrics["discrete_accuracy_2way"]
                print(f"[eval t={elapsed:.0f}s] N={metrics['n']} "
                      f"trafnosc_3way={acc3*100:.1f}% trafnosc_2way={acc2*100:.1f}%", flush=True)
    finally:
        bus.shutdown()
        print(f"[koniec] ramek={total_frames}", flush=True)


if __name__ == "__main__":
    main()
