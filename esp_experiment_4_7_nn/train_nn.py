#!/usr/bin/env python3
"""
Eksperyment 4.7, krok 2 -- uczenie malej sieci neuronowej (MLP) zastepujacej
regule reczna "Kierunku B" w wykrywaniu bajtow zawierajacych flagi bitowe.

PROBLEM DO ROZWIAZANIA (zmierzony w Eksperymencie 4.6, przebieg godzinny):
regula reczna ma SUFIT KONSTRUKCYJNY na poziomie Recall 85%. Trzy flagi
z 7-8 niezaleznie przelaczajacymi sie bitami sa odrzucane przez warunek
`2 <= bit_count <= 6`, mimo ze ich wspolczynniki skokow (0.97/0.84/0.82)
przeszlyby kazdy prog. Poluzowanie limitu do 8 daje Recall 100%, ale
Precision spada do 51.3% -- prosta regula nie odroznia "osmiu flag" od
szybkiego skalara. Granica jest nieliniowa, wiec to zadanie dla klasyfikatora
uczonego, nie dla progu.

PROTOKOL (rygor cross-corpus, wyciagniety z porazki Eksperymentu 4.4):
  - uczenie:   ziarna 1-24   (wirtualna magistrala vcan, realne taktowanie)
  - walidacja: ziarna 25-30  (dobor epoki/progu, NIGDY do raportowania)
  - TEST:      ziarno 999, dane z PRAWDZIWEGO sprzetu (MCP2515 na Orange Pi,
               godzinny przebieg) -- rozklad nigdy nie widziany w uczeniu

Uzycie:
  python3 train_nn.py --data-dir nn_data --live-test state_1h.json \\
      --ground-truth ground_truth_seed999.json --out model.pt
"""
import argparse
import glob
import json
import os
import random

import torch
import torch.nn as nn

RULE_THRESHOLD = 0.3
RULE_MAX_BITS = 6


def rule_verdict(st, thr=RULE_THRESHOLD, maxbits=RULE_MAX_BITS):
    """Regula reczna -- punkt odniesienia, liczona na TYCH SAMYCH danych."""
    mask = st["seen0"] & st["seen1"]
    bit_count = bin(mask).count("1")
    if st["n_samples"] < 2 or bit_count < 2 or bit_count > maxbits:
        return False
    if st["changed_pairs"] == 0:
        return False
    return (st["big_jumps"] / st["changed_pairs"]) >= thr


class MLP(nn.Module):
    """Swiadomie maly: 10 -> 24 -> 12 -> 1, ~430 parametrow. Ma sie zmiescic
    w demonie na urzadzeniu brzegowym i liczyc w mikrosekundach na bajt."""

    def __init__(self, n_features):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(n_features, 24), nn.ReLU(),
            nn.Linear(24, 12), nn.ReLU(),
            nn.Linear(12, 1),
        )

    def forward(self, x):
        return self.net(x).squeeze(-1)


def load_seeds(data_dir, seeds):
    X, y, states = [], [], []
    for s in seeds:
        path = os.path.join(data_dir, f"seed_{s}.json")
        if not os.path.exists(path):
            continue
        d = json.load(open(path))
        for r in d["rows"]:
            X.append(r["features"])
            y.append(float(r["label"]))
            states.append(r["state"])
    return X, y, states


def metrics(pred, truth):
    tp = sum(1 for p, t in zip(pred, truth) if p and t)
    fp = sum(1 for p, t in zip(pred, truth) if p and not t)
    fn = sum(1 for p, t in zip(pred, truth) if t and not p)
    rec = tp / (tp + fn) if (tp + fn) else 0.0
    prec = tp / (tp + fp) if (tp + fp) else 0.0
    f1 = 2 * prec * rec / (prec + rec) if (prec + rec) else 0.0
    return tp, fp, fn, rec, prec, f1


def load_collect_file(path):
    """Wczytuje plik w formacie collect_live.py (cechy + etykieta + stan reguly).
    Uwaga metodologiczna: stan zapisywany przez pi_continuous_observer.py
    zawiera WYLACZNIE liczniki przyrostowe (seen0/seen1/changed_pairs/
    big_jumps/n_samples), z ktorych 7 z 10 cech tej sieci NIE jest odtwarzalne.
    Dlatego test na sprzecie prowadzi sie przez collect_live.py uruchomiony
    bezposrednio na urzadzeniu -- co i tak wierniej odwzorowuje wdrozenie."""
    d = json.load(open(path))
    X = [r["features"] for r in d["rows"]]
    y = [float(r["label"]) for r in d["rows"]]
    states = [r["state"] for r in d["rows"]]
    return X, y, states


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-dir", required=True)
    ap.add_argument("--train-seeds", default="1-24")
    ap.add_argument("--val-seeds", default="25-30")
    ap.add_argument("--epochs", type=int, default=400)
    ap.add_argument("--lr", type=float, default=0.01)
    ap.add_argument("--seed", type=int, default=0)
    ap.add_argument("--out", default="model.pt")
    ap.add_argument("--test-file", default=None,
                    help="plik w formacie collect_live.py -- TEST koncowy "
                         "(najlepiej zebrany na prawdziwym sprzecie)")
    args = ap.parse_args()

    torch.manual_seed(args.seed)
    random.seed(args.seed)

    def parse(spec):
        a, b = spec.split("-")
        return list(range(int(a), int(b) + 1))

    tr_seeds, va_seeds = parse(args.train_seeds), parse(args.val_seeds)
    Xtr, ytr, str_ = load_seeds(args.data_dir, tr_seeds)
    Xva, yva, sva = load_seeds(args.data_dir, va_seeds)

    print(f"uczenie:   {len(Xtr)} pozycji z {len(tr_seeds)} ziaren "
          f"({int(sum(ytr))} z flagami, {sum(ytr)/len(ytr):.1%})")
    print(f"walidacja: {len(Xva)} pozycji z {len(va_seeds)} ziaren "
          f"({int(sum(yva))} z flagami)")

    Xtr_t = torch.tensor(Xtr, dtype=torch.float32)
    ytr_t = torch.tensor(ytr, dtype=torch.float32)
    Xva_t = torch.tensor(Xva, dtype=torch.float32)

    n_feat = Xtr_t.shape[1]
    model = MLP(n_feat)
    n_params = sum(p.numel() for p in model.parameters())
    print(f"siec: {n_feat} -> 24 -> 12 -> 1, {n_params} parametrow\n")

    # wazenie klasy dodatniej -- flagi to ~17% zbioru
    pos_weight = torch.tensor([(len(ytr) - sum(ytr)) / max(sum(ytr), 1)])
    lossf = nn.BCEWithLogitsLoss(pos_weight=pos_weight)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)

    best_f1, best_state, best_epoch = -1.0, None, -1
    for ep in range(1, args.epochs + 1):
        model.train()
        opt.zero_grad()
        loss = lossf(model(Xtr_t), ytr_t)
        loss.backward()
        opt.step()

        if ep % 20 == 0 or ep == args.epochs:
            model.eval()
            with torch.no_grad():
                pv = (torch.sigmoid(model(Xva_t)) >= 0.5).tolist()
            _, _, _, rec, prec, f1 = metrics(pv, yva)
            if f1 > best_f1:
                best_f1, best_epoch = f1, ep
                best_state = {k: v.clone() for k, v in model.state_dict().items()}
            if ep % 100 == 0:
                print(f"  epoka {ep:4d}  strata={loss.item():.4f}  "
                      f"walidacja: R={rec:.1%} P={prec:.1%} F1={f1:.1%}")

    model.load_state_dict(best_state)
    print(f"\nwybrano epoke {best_epoch} (najlepsze F1 na walidacji: {best_f1:.1%})")

    # --- porownanie na WALIDACJI ---
    model.eval()
    with torch.no_grad():
        pv = (torch.sigmoid(model(Xva_t)) >= 0.5).tolist()
    rule_v = [rule_verdict(s) for s in sva]
    print("\n" + "=" * 68)
    print("WALIDACJA (ziarna 25-30, vcan)")
    print("=" * 68)
    for name, pred in (("regula reczna", rule_v), ("siec neuronowa", pv)):
        tp, fp, fn, rec, prec, f1 = metrics(pred, yva)
        print(f"  {name:16s} TP={tp:3d} FP={fp:3d} FN={fn:3d}  "
              f"R={rec:6.1%} P={prec:6.1%} F1={f1:6.1%}")

    # --- TEST koncowy: dane nigdy nie widziane, najlepiej z prawdziwego sprzetu ---
    if args.test_file:
        Xte, yte, ste = load_collect_file(args.test_file)
        Xte_t = torch.tensor(Xte, dtype=torch.float32)
        with torch.no_grad():
            pte = (torch.sigmoid(model(Xte_t)) >= 0.5).tolist()
        rule_t = [rule_verdict(s) for s in ste]
        print("\n" + "=" * 68)
        print(f"TEST KONCOWY -- {os.path.basename(args.test_file)} ({len(yte)} pozycji)")
        print("=" * 68)
        for name, pred in (("regula reczna", rule_t), ("siec neuronowa", pte)):
            tp, fp, fn, rec, prec, f1 = metrics(pred, yte)
            print(f"  {name:16s} TP={tp:3d} FP={fp:3d} FN={fn:3d}  "
                  f"R={rec:6.1%} P={prec:6.1%} F1={f1:6.1%}")
        _, _, _, _, _, f1r = metrics(rule_t, yte)
        _, _, _, _, _, f1n = metrics(pte, yte)
        print(f"\n  roznica F1: {(f1n - f1r) * 100:+.1f} pp")

    torch.save({"state_dict": model.state_dict(), "n_features": n_feat}, args.out)
    print(f"\nzapisano model: {args.out}")


if __name__ == "__main__":
    main()
