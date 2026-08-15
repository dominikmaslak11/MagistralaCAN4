#!/usr/bin/env python3
"""
Eksperyment 4.7, krok 3 -- eksport wag sieci do JSON.

PO CO: demon obserwacyjny nie powinien ciagnac PyTorcha (1.3 GB instalacji,
kilka sekund samego importu) po to, zeby policzyc 577 parametrow. Wagi ida do
JSON, a inferencje robi ~15 linii czystego Pythona (patrz pi_observer_nn.py).
Dzieki temu wdrozenie nie zwieksza wymagan srodowiskowych demona ANI o jedna
zaleznosc -- dziala na golym Pythonie 3.

Uzycie:
  python3 export_model.py --model model_47.pt --out model_47.json
"""
import argparse
import json

import torch

from train_nn import MLP


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    ck = torch.load(args.model, weights_only=True)
    model = MLP(ck["n_features"])
    model.load_state_dict(ck["state_dict"])
    model.eval()

    layers = []
    for module in model.net:
        if isinstance(module, torch.nn.Linear):
            layers.append({
                "W": module.weight.detach().tolist(),   # [out][in]
                "b": module.bias.detach().tolist(),     # [out]
            })

    payload = {
        "n_features": ck["n_features"],
        "layers": layers,
        "activation": "relu",       # miedzy warstwami; ostatnia bez aktywacji
        "output": "logit",          # decyzja: sigmoid(logit) >= 0.5  <=>  logit >= 0
        "feature_names": [
            "bit_count", "jump_ratio", "change_ratio", "distinct_ratio", "entropy",
            "mean_abs_delta", "max_abs_delta", "std_value", "extremes_frac", "popcount_std",
        ],
    }
    with open(args.out, "w") as f:
        json.dump(payload, f)

    n_params = sum(len(l["b"]) + sum(len(r) for r in l["W"]) for l in layers)
    print(f"zapisano {args.out}: {len(layers)} warstw, {n_params} parametrow")
    print("ksztalty:", " -> ".join(
        [str(ck["n_features"])] + [str(len(l["b"])) for l in layers]))


if __name__ == "__main__":
    main()
