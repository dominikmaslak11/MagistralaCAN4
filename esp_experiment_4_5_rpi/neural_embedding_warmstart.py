#!/usr/bin/env python3
"""
Eksperyment 4.5, Faza 5 -- czy embedding neuronowy (sentence-transformers,
model all-MiniLM-L6-v2) zamiast recznie zaprojektowanego 7-wymiarowego
wektora cech (qdrant_warmstart_diverse.py, Eksperyment 4.4) poprawia
trafnosc retrievalu, zwlaszcza GENERALIZACJE miedzy roznymi przebiegami
(problem z 4.4: ~70% cross-corpus vs ~89-91% within-corpus).

WAZNE ZASTRZEZENIE (sprawdzone empirycznie na prawdziwym Pi Zero W,
2026-08-08): PyTorch/ONNX Runtime/TFLite NIE MAJA zadnych zbudowanych
pakietow dla ARMv6 (nawet przez piwheels.org) - ten eksperyment jest
WYLACZNIE badawczy/offline (laptop), NIE jest wdrazalny na obecnym
Raspberry Pi Zero W. Wymagaloby to Pi Zero 2 W (ARMv7/aarch64) lub innego
sprzetu do realnego wdrozenia.

Serializacja: kazda seria wartosci bajtowych/bitowych zamieniana na tekst
(wartosci oddzielone spacja), embedowana przez model sentence-transformers
(wytrenowany na jezyku naturalnym, NIE na sekwencjach liczbowych - to
mocno "out of distribution" zastosowanie, testowane tu wprost empirycznie,
bez zakladania z gory ze zadziala).

Uzycie:
  python3 neural_embedding_warmstart.py --corpus-lib /path/corpus_diverse.json \\
      --corpus-test /tmp/corpus_seed999.json
"""
import argparse
import json
import sys
import os
import time
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "esp_experiment_4_4_qdrant"))
from qdrant_warmstart_diverse import extract_raw_series, feature_vector  # noqa: E402

from qdrant_client import QdrantClient
from qdrant_client.models import Distance, VectorParams, PointStruct
from sentence_transformers import SentenceTransformer


def load_instances(corpus_path, model, embed_dim):
    with open(corpus_path) as f:
        corpus = json.load(f)
    instances = []
    for cfg in corpus["ground_truth"]:
        can_id = cfg["can_id"]
        samples = corpus["samples"][str(can_id)]
        for sig in cfg["signals"]:
            values = extract_raw_series(samples, sig)
            text = " ".join(str(v) for v in values)
            vec_neural = model.encode(text, show_progress_bar=False).tolist()

            if sig["kind"] == "bit_flag":
                max_raw = 1
            elif sig["kind"] == "partial_scalar":
                mask = sig["bit_mask"]
                max_raw = mask >> ((mask & -mask).bit_length() - 1)
            else:
                max_raw = (1 << (8 * sig["byte_len"])) - 1
            vec_handcrafted = feature_vector(values, max_raw)

            instances.append({
                "can_id": can_id, "name": sig["name"], "kind": sig["kind"],
                "is_discrete": sig["is_discrete"],
                "vec_neural": vec_neural, "vec_handcrafted": vec_handcrafted,
            })
    return instances


def build_collection(client, name, instances, vec_key, size):
    if client.collection_exists(name):
        client.delete_collection(name)
    client.create_collection(name, vectors_config=VectorParams(size=size, distance=Distance.COSINE))
    client.upsert(name, points=[
        PointStruct(id=i, vector=inst[vec_key],
                    payload={"kind": inst["kind"], "is_discrete": inst["is_discrete"], "name": inst["name"]})
        for i, inst in enumerate(instances)
    ])


def leave_one_out_eval(client, collection, instances, vec_key):
    confusion = Counter()
    discrete_correct = discrete_total = 0
    for i, inst in enumerate(instances):
        result = client.query_points(collection_name=collection, query=inst[vec_key], limit=6)
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
    correct = sum(confusion.get((k, k), 0) for k in kinds)
    return {
        "n": total, "kind_accuracy_3way": correct / total if total else None,
        "discrete_accuracy_2way": discrete_correct / discrete_total if discrete_total else None,
    }


def cross_corpus_eval(client, collection, lib_instances, test_instances, vec_key):
    """Biblioteka = lib_instances (juz w kolekcji), zapytania = test_instances
    (INNY korpus) - bez samo-wykluczenia, bo to rozne zbiory."""
    confusion = Counter()
    discrete_correct = discrete_total = 0
    for inst in test_instances:
        result = client.query_points(collection_name=collection, query=inst[vec_key], limit=1)
        if not result.points:
            continue
        neighbor = result.points[0]
        confusion[(inst["kind"], neighbor.payload["kind"])] += 1
        discrete_total += 1
        if inst["is_discrete"] == neighbor.payload["is_discrete"]:
            discrete_correct += 1
    kinds = ["scalar", "partial_scalar", "bit_flag"]
    total = sum(confusion.values())
    correct = sum(confusion.get((k, k), 0) for k in kinds)
    return {
        "n": total, "kind_accuracy_3way": correct / total if total else None,
        "discrete_accuracy_2way": discrete_correct / discrete_total if discrete_total else None,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus-lib", default="corpus_diverse.json", help="seed=42, biblioteka")
    ap.add_argument("--corpus-test", default="/tmp/corpus_seed999.json", help="seed=999, INNY przebieg")
    args = ap.parse_args()

    print("Ladowanie modelu all-MiniLM-L6-v2...")
    t0 = time.time()
    model = SentenceTransformer("all-MiniLM-L6-v2")
    embed_dim = model.get_sentence_embedding_dimension()
    print(f"  gotowe w {time.time()-t0:.1f}s, wymiar embeddingu={embed_dim}")

    print("Generowanie instancji (embedding neuronowy + rowniez cechy reczne dla porownania)...")
    t0 = time.time()
    lib_instances = load_instances(args.corpus_lib, model, embed_dim)
    test_instances = load_instances(args.corpus_test, model, embed_dim)
    print(f"  biblioteka (seed=42): {len(lib_instances)} instancji")
    print(f"  test (seed=999): {len(test_instances)} instancji")
    print(f"  czas: {time.time()-t0:.1f}s")

    client = QdrantClient(":memory:")

    print("\n=== TEST A: w obrebie jednego korpusu (seed=42), leave-one-out ===")
    build_collection(client, "lib_neural", lib_instances, "vec_neural", embed_dim)
    build_collection(client, "lib_hand", lib_instances, "vec_handcrafted", 7)
    res_a_neural = leave_one_out_eval(client, "lib_neural", lib_instances, "vec_neural")
    res_a_hand = leave_one_out_eval(client, "lib_hand", lib_instances, "vec_handcrafted")
    print(f"  reczne cechy (7-dim):     trafnosc_3way={res_a_hand['kind_accuracy_3way']*100:.1f}%  "
          f"trafnosc_2way={res_a_hand['discrete_accuracy_2way']*100:.1f}%  N={res_a_hand['n']}")
    print(f"  embedding neuronowy ({embed_dim}-dim): trafnosc_3way={res_a_neural['kind_accuracy_3way']*100:.1f}%  "
          f"trafnosc_2way={res_a_neural['discrete_accuracy_2way']*100:.1f}%  N={res_a_neural['n']}")

    print("\n=== TEST B: cross-corpus (biblioteka=seed42, zapytania=seed999) - PROBLEM Z 4.4 ===")
    res_b_neural = cross_corpus_eval(client, "lib_neural", lib_instances, test_instances, "vec_neural")
    res_b_hand = cross_corpus_eval(client, "lib_hand", lib_instances, test_instances, "vec_handcrafted")
    print(f"  reczne cechy (7-dim):     trafnosc_3way={res_b_hand['kind_accuracy_3way']*100:.1f}%  "
          f"trafnosc_2way={res_b_hand['discrete_accuracy_2way']*100:.1f}%  N={res_b_hand['n']}")
    print(f"  embedding neuronowy ({embed_dim}-dim): trafnosc_3way={res_b_neural['kind_accuracy_3way']*100:.1f}%  "
          f"trafnosc_2way={res_b_neural['discrete_accuracy_2way']*100:.1f}%  N={res_b_neural['n']}")

    out = {
        "embed_dim": embed_dim,
        "test_a_within_corpus": {"handcrafted": res_a_hand, "neural": res_a_neural},
        "test_b_cross_corpus": {"handcrafted": res_b_hand, "neural": res_b_neural},
    }
    with open("neural_embedding_results.json", "w") as f:
        json.dump(out, f, indent=2)
    print("\nZapisano: neural_embedding_results.json")


if __name__ == "__main__":
    main()
