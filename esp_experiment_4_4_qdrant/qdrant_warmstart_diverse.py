#!/usr/bin/env python3
"""
Eksperyment 4.4 (prototyp, etap 2) — Retrieval-Augmented Warm-Start na
ZROZNICOWANYM korpusie z esp_experiment_4_3/generate_traffic_diverse.py
(40 konfiguracji CAN ID, wzorce: pure_scalars, pure_flags_one_byte,
flags_spread_multi_byte, mixed_byte, scalars_plus_flags).

Rozszerza qdrant_warmstart_prototype.py (10 sygnalow) na duzo wiekszy,
bardziej zroznicowany zbior i klasyfikacje 3-drogowa (scalar / partial_scalar
/ bit_flag), nie tylko dyskretny/ciagly.

Wejscie: corpus_diverse.json (wygenerowany przez
`generate_traffic_diverse.py --dump-json`, patrz README w esp_experiment_4_3/).
"""
import json
import statistics
import sys
from collections import Counter

from qdrant_client import QdrantClient
from qdrant_client.models import Distance, VectorParams, PointStruct


def extract_raw_series(samples_for_id, sig):
    """Wyciaga surowa (bajtowa/bitowa) serie czasowa danego sygnalu z probek
    calej ramki - dokladnie to, co widzialby dekoder BEZ znajomosci
    scale/offset/kind."""
    kind = sig["kind"]
    values = []
    if kind == "bit_flag":
        byte_idx = sig["byte_idx"]
        bit = sig["bit_idx"]
        for frame in samples_for_id:
            values.append((frame[byte_idx] >> bit) & 1)
        return values

    byte_idx = sig["byte_idx"]
    if kind == "partial_scalar":
        mask = sig["bit_mask"]
        shift = (mask & -mask).bit_length() - 1  # pozycja najnizszego ustawionego bitu
        for frame in samples_for_id:
            values.append((frame[byte_idx] & mask) >> shift)
        return values

    # kind == "scalar"
    byte_len = sig["byte_len"]
    le = sig["little_endian"]
    for frame in samples_for_id:
        chunk = frame[byte_idx:byte_idx + byte_len]
        raw = 0
        if le:
            for i, b in enumerate(chunk):
                raw |= b << (8 * i)
        else:
            for b in chunk:
                raw = (raw << 8) | b
        values.append(raw)
    return values


def feature_vector(values, max_raw):
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


def main(corpus_path):
    with open(corpus_path) as f:
        corpus = json.load(f)

    instances = []  # list of dicts: id, name, kind, is_discrete, vector
    for cfg in corpus["ground_truth"]:
        can_id = cfg["can_id"]
        samples = corpus["samples"][str(can_id)]
        for sig in cfg["signals"]:
            values = extract_raw_series(samples, sig)
            if sig["kind"] == "bit_flag":
                max_raw = 1
            elif sig["kind"] == "partial_scalar":
                max_raw = sig["bit_mask"] >> ((sig["bit_mask"] & -sig["bit_mask"]).bit_length() - 1)
            else:
                max_raw = (1 << (8 * sig["byte_len"])) - 1
            vec = feature_vector(values, max_raw)
            instances.append({
                "can_id": can_id,
                "name": sig["name"],
                "kind": sig["kind"],
                "is_discrete": sig["is_discrete"],
                "vector": vec,
            })

    print(f"Zaladowano {len(instances)} instancji sygnalow z {corpus['n_configs']} konfiguracji CAN ID.")
    kind_counts = Counter(inst["kind"] for inst in instances)
    print("Rozklad rodzajow:", dict(kind_counts))

    client = QdrantClient(":memory:")
    client.create_collection(
        collection_name="diverse_signatures",
        vectors_config=VectorParams(size=7, distance=Distance.COSINE),
    )
    points = [
        PointStruct(id=i, vector=inst["vector"], payload={"kind": inst["kind"], "is_discrete": inst["is_discrete"], "name": inst["name"]})
        for i, inst in enumerate(instances)
    ]
    client.upsert(collection_name="diverse_signatures", points=points)

    # Leave-one-out przez samo-wykluczenie: pytamy o top-5, bierzemy
    # pierwszy wynik o INNYM id niz zapytanie.
    confusion = Counter()  # (true_kind, predicted_kind) -> count
    discrete_correct = 0
    discrete_total = 0

    for i, inst in enumerate(instances):
        result = client.query_points(
            collection_name="diverse_signatures", query=inst["vector"], limit=6,
        )
        neighbor = None
        for p in result.points:
            if p.id != i:
                neighbor = p
                break
        if neighbor is None:
            continue
        true_kind = inst["kind"]
        pred_kind = neighbor.payload["kind"]
        confusion[(true_kind, pred_kind)] += 1

        true_disc = inst["is_discrete"]
        pred_disc = neighbor.payload["is_discrete"]
        discrete_total += 1
        if true_disc == pred_disc:
            discrete_correct += 1

    kinds = ["scalar", "partial_scalar", "bit_flag"]
    print("\nMacierz pomylek (wiersze = prawda, kolumny = predykcja z retrieval):")
    header = "true\\pred".ljust(16) + "".join(k.ljust(16) for k in kinds)
    print(header)
    for tk in kinds:
        row = tk.ljust(16)
        for pk in kinds:
            row += str(confusion.get((tk, pk), 0)).ljust(16)
        print(row)

    total = sum(confusion.values())
    correct_kind = sum(confusion.get((k, k), 0) for k in kinds)
    print(f"\nTrafnosc 3-drogowa (scalar/partial_scalar/bit_flag), leave-one-out, N={total}: "
          f"{correct_kind}/{total} = {100.0*correct_kind/total:.1f}%")
    print(f"Trafnosc 2-drogowa (dyskretny vs ciagly), N={discrete_total}: "
          f"{discrete_correct}/{discrete_total} = {100.0*discrete_correct/discrete_total:.1f}%")

    per_kind_recall = {}
    for tk in kinds:
        row_total = sum(confusion.get((tk, pk), 0) for pk in kinds)
        row_correct = confusion.get((tk, tk), 0)
        per_kind_recall[tk] = (row_correct, row_total, 100.0 * row_correct / row_total if row_total else 0.0)
    print("\nRecall per rodzaj sygnalu:")
    for tk, (c, t, pct) in per_kind_recall.items():
        print(f"  {tk:<16} {c}/{t} = {pct:.1f}%")


if __name__ == "__main__":
    corpus_path = sys.argv[1] if len(sys.argv) > 1 else "corpus_diverse.json"
    main(corpus_path)
