#!/usr/bin/env python3
"""
Eksperyment 4.4 (prototyp) — Retrieval-Augmented Warm-Start Decoding przez Qdrant.

Pomysl: zamiast kazdorazowego "Cold Start" LLM od zera (jak w Eksperymencie 4.1),
zbudowac biblioteke znanych "sygnatur zachowania" sygnalow CAN (jak wartosc
zmienia sie w czasie: skoki, rozklad, dyskretnosc) w bazie wektorowej Qdrant.
Dla NOWEGO, nieznanego sygnalu, wyszukujemy najbardziej podobna znana
sygnature i sprawdzamy, czy jej metadane (isDiscrete/scale/offset) trafnie
przewiduja strukture nowego sygnalu - "warm start" zamiast slepego zgadywania.

WAZNE ZALOZENIE UPRASZCZAJACE (celowe, jawnie udokumentowane): zakladamy, ze
granice bajtowe sygnalu (byteIdx/byteLen) sa JUZ poprawnie zgadniete (np. przez
LLM lub czlowieka) - ten prototyp NIE probuje znalezc tych granic, tylko
odpowiada na pytanie "jaki to typ sygnalu (dyskretny/ciagly) i jaka skala/offset
sa prawdopodobne", na podstawie samych surowych wartosci w oknie czasowym.
To WEZSZY problem niz pelne Zero-Shot Cold Start z Eksperymentu 4.1, ale
odpowiada realnemu, uzytecznemu krokowi posredniemu.

Dane: reuzywa VehicleSim z esp_experiment_4_1/generate_traffic.py (czysta
symulacja Python, bez potrzeby ESP32/magistrali CAN) - dokladnie ten sam kod,
ktory generowal ground truth dla Eksperymentu 4.1.
"""
import sys
import os
import random
import statistics

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "esp_experiment_4_1"))
from generate_traffic import VehicleSim  # noqa: E402

from qdrant_client import QdrantClient
from qdrant_client.models import Distance, VectorParams, PointStruct


# Definicja 10 znanych sygnalow (ground truth, identyczna z
# DecodingAccuracyRunner.cpp / generate_traffic.py naglowek).
SIGNAL_DEFS = [
    # (name, can_id_frame_method, byteIdx, byteLen, littleEndian, isDiscrete, scale, offset, bitMask)
    dict(name="engine_rpm", frame="0x100", byteIdx=0, byteLen=2, le=True, discrete=False, scale=0.25, offset=0, bitmask=None),
    dict(name="coolant_temp", frame="0x100", byteIdx=2, byteLen=1, le=False, discrete=False, scale=1.0, offset=-40, bitmask=None),
    dict(name="throttle_position", frame="0x100", byteIdx=3, byteLen=1, le=False, discrete=False, scale=0.4, offset=0, bitmask=None),
    dict(name="steering_angle", frame="0x150", byteIdx=0, byteLen=2, le=True, discrete=False, scale=0.1, offset=0, bitmask=None),
    dict(name="vehicle_speed", frame="0x150", byteIdx=2, byteLen=2, le=True, discrete=False, scale=0.01, offset=0, bitmask=None),
    dict(name="left_indicator", frame="0x200", byteIdx=0, byteLen=1, le=False, discrete=True, scale=1, offset=0, bitmask=0x01),
    dict(name="right_indicator", frame="0x200", byteIdx=0, byteLen=1, le=False, discrete=True, scale=1, offset=0, bitmask=0x02),
    dict(name="headlights", frame="0x200", byteIdx=0, byteLen=1, le=False, discrete=True, scale=1, offset=0, bitmask=0x04),
    dict(name="driver_door", frame="0x200", byteIdx=0, byteLen=1, le=False, discrete=True, scale=1, offset=0, bitmask=0x08),
    dict(name="handbrake", frame="0x200", byteIdx=0, byteLen=1, le=False, discrete=True, scale=1, offset=0, bitmask=0x10),
]

WINDOW_FRAMES = 60  # ok. kilkanascie sekund symulowanego czasu na sygnal


def raw_value_series(sim: VehicleSim, sig: dict, n_frames: int, steps_between: int = 3):
    """Generuje surowa (bajtowa) serie czasowa wartosci danego sygnalu,
    krokujac symulacje. To dokladnie to, co widzialby dekoder BEZ znajomosci
    scale/offset - surowe bajty, nie fizyczna wartosc."""
    values = []
    frame_fn = {"0x100": sim.frame_0x100, "0x150": sim.frame_0x150, "0x200": sim.frame_0x200}[sig["frame"]]
    for _ in range(n_frames):
        for _ in range(steps_between):
            sim.step()
        frame = frame_fn()
        data = frame[8:8 + 8]  # struct "=IB3x8s" -> can_id(4)+dlc(1)+pad(3)+data(8)
        raw = 0
        byte_slice = data[sig["byteIdx"]:sig["byteIdx"] + sig["byteLen"]]
        if sig["le"]:
            for i, b in enumerate(byte_slice):
                raw |= b << (8 * i)
        else:
            for b in byte_slice:
                raw = (raw << 8) | b
        if sig["byteLen"] == 1 and sig["bitmask"] is not None:
            raw = 1 if (raw & sig["bitmask"]) else 0
        values.append(raw)
    return values


def feature_vector(values):
    """Cechy reczne (nie neuronowe) opisujace ZACHOWANIE surowej serii w
    czasie - Qdrant nie ma znaczenia skad pochodzi wektor, wazne zeby podobne
    zachowania mialy podobne wektory."""
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
    value_range = max(values) - min(values)
    return [
        distinct_ratio,
        dwell_fraction,
        oscillation_rate,
        (statistics.mean(abs_deltas) if abs_deltas else 0.0) / 256.0,
        (statistics.pstdev(values) if n > 1 else 0.0) / 256.0,
        value_range / 65536.0,
        max(abs_deltas) / 256.0 if abs_deltas else 0.0,
    ]


def build_index(client: QdrantClient, exclude_name: str | None, seed: int):
    """Buduje kolekcje Qdrant ze wszystkich sygnalow OPROCZ exclude_name
    (leave-one-out) - kazdy z WLASNA, niezalezna symulacja (seed)."""
    random.seed(seed)
    sim = VehicleSim()
    client.recreate_collection(
        collection_name="can_signal_signatures",
        vectors_config=VectorParams(size=7, distance=Distance.COSINE),
    )
    points = []
    for i, sig in enumerate(SIGNAL_DEFS):
        if sig["name"] == exclude_name:
            continue
        values = raw_value_series(sim, sig, WINDOW_FRAMES)
        vec = feature_vector(values)
        points.append(PointStruct(id=i, vector=vec, payload=sig))
    client.upsert(collection_name="can_signal_signatures", points=points)


def query_holdout(client: QdrantClient, sig: dict, seed: int):
    random.seed(seed)
    sim = VehicleSim()
    values = raw_value_series(sim, sig, WINDOW_FRAMES)
    vec = feature_vector(values)
    result = client.query_points(collection_name="can_signal_signatures", query=vec, limit=1)
    return result.points[0] if result.points else None


def main():
    client = QdrantClient(":memory:")  # tryb embedded, bez serwera/Dockera

    print(f"{'sygnal':<20} {'prawda: dyskretny?':<20} {'retrieval: dyskretny?':<22} {'match':<8} {'najblizszy sasiad'}")
    print("-" * 100)

    correct = 0
    for sig in SIGNAL_DEFS:
        build_index(client, exclude_name=sig["name"], seed=42)
        hit = query_holdout(client, sig, seed=1234)
        neighbor = hit.payload
        is_match = neighbor["discrete"] == sig["discrete"]
        correct += int(is_match)
        print(
            f"{sig['name']:<20} {str(sig['discrete']):<20} {str(neighbor['discrete']):<22} "
            f"{'OK' if is_match else 'BLAD':<8} {neighbor['name']} (score={hit.score:.3f})"
        )

    print("-" * 100)
    print(f"Trafnosc retrieval (dyskretny vs ciagly), leave-one-out, N={len(SIGNAL_DEFS)}: "
          f"{correct}/{len(SIGNAL_DEFS)} = {100.0*correct/len(SIGNAL_DEFS):.1f}%")


if __name__ == "__main__":
    main()
