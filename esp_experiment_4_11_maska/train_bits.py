#!/usr/bin/env python3
"""
Eksperyment 4.11, krok 2 -- klasyfikator PER BIT: czy dany bit jest niezalezna
flaga bitowa, czy nalezy do skalara.

ODNIESIENIE: obecna maska `seen0 & seen1` ("bit kiedykolwiek sie zmienil").
Zmierzone na korpusie (seed=999, 20 CAN ID):
    bajty z samymi flagami:            17/17 = 100%   <- trywialne
    bajty MIESZANE (flagi + skalar):    0/3  =   0%   <- tu lezy problem
Odniesienie jest z definicji bezradne w bajtach mieszanych, bo bity skalara
tez sie zmieniaja. Cala wartosc tego eksperymentu rozstrzyga sie na TYCH
przypadkach -- dlatego raportujemy je OSOBNO, a nie topimy w sredniej.

Protokol:
  uczenie   ziarna 1-30  (vcan)
  walidacja ziarna 31-40 (vcan, wybor epoki)
  TEST      ziarna 100-105 na prawdziwym MCP2515

Uzycie:
  python3 train_bits.py --data-dir bits_data --test-glob '/root/proj/data/bits_hw_*.json'
"""
import argparse
import glob
import json
import os
from collections import defaultdict

import torch
import torch.nn as nn


class BitMLP(nn.Module):
    """11 -> 32 -> 16 -> 1. Wciaz maly (~1100 parametrow), liczony raz na
    snapshot dla 8 bitow bajtu."""

    def __init__(self, n_features):
        super().__init__()
        self.net = nn.Sequential(
            nn.Linear(n_features, 32), nn.ReLU(),
            nn.Linear(32, 16), nn.ReLU(),
            nn.Linear(16, 1),
        )

    def forward(self, x):
        return self.net(x).squeeze(-1)


def load(paths):
    rows = []
    for p in paths:
        rows.extend(json.load(open(p))["rows"])
    return rows


def bit_metrics(pred, truth):
    tp = sum(1 for p, t in zip(pred, truth) if p and t)
    fp = sum(1 for p, t in zip(pred, truth) if p and not t)
    fn = sum(1 for p, t in zip(pred, truth) if t and not p)
    rec = tp / (tp + fn) if (tp + fn) else 0.0
    prec = tp / (tp + fp) if (tp + fp) else 0.0
    f1 = 2 * prec * rec / (prec + rec) if (prec + rec) else 0.0
    return tp, fp, fn, rec, prec, f1


def mask_accuracy(rows, pred):
    """Trafnosc MASKI DOKLADNEJ, w rozbiciu na typy bajtow.
    Bajt liczy sie jako trafiony, gdy przewidziana maska == prawdziwa maska flag."""
    true_m, pred_m, kind = defaultdict(int), defaultdict(int), defaultdict(
        lambda: {"f": 0, "s": 0})
    for r, p in zip(rows, pred):
        k = (r["seed"], r["can_id"], r["byte"])
        if r["label"]:
            true_m[k] |= 1 << r["bit"]
            kind[k]["f"] += 1
        elif r["is_scalar_bit"]:
            kind[k]["s"] += 1
        if p:
            pred_m[k] |= 1 << r["bit"]
    pure = [k for k, v in kind.items() if v["f"] and not v["s"]]
    mixed = [k for k, v in kind.items() if v["f"] and v["s"]]
    out = {}
    for name, keys in (("czyste", pure), ("MIESZANE", mixed),
                       ("razem", pure + mixed)):
        if not keys:
            out[name] = (0, 0, 0.0)
            continue
        ok = sum(1 for k in keys if true_m[k] == pred_m[k])
        out[name] = (ok, len(keys), ok / len(keys))
    return out


def report(title, rows, pred, baseline):
    print("\n" + "=" * 74)
    print(title)
    print("=" * 74)
    y = [r["label"] for r in rows]
    for name, p in (("maska seen0&seen1", baseline), ("siec per bit", pred)):
        tp, fp, fn, rec, prec, f1 = bit_metrics(p, y)
        print("  %-18s per bit:  TP=%4d FP=%4d FN=%3d  R=%6.1f%% P=%6.1f%% F1=%6.1f%%"
              % (name, tp, fp, fn, rec * 100, prec * 100, f1 * 100))
    print()
    print("  %-18s %-10s %-10s %-10s" % ("TRAFNOSC MASKI", "czyste", "MIESZANE", "razem"))
    for name, p in (("maska seen0&seen1", baseline), ("siec per bit", pred)):
        m = mask_accuracy(rows, p)
        print("  %-18s %-10s %-10s %-10s" % (
            name,
            "%d/%d %.0f%%" % (m["czyste"][0], m["czyste"][1], m["czyste"][2] * 100),
            "%d/%d %.0f%%" % (m["MIESZANE"][0], m["MIESZANE"][1], m["MIESZANE"][2] * 100),
            "%d/%d %.0f%%" % (m["razem"][0], m["razem"][1], m["razem"][2] * 100)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-dir", required=True)
    ap.add_argument("--test-glob", default=None)
    ap.add_argument("--epochs", type=int, default=600)
    ap.add_argument("--lr", type=float, default=0.01)
    ap.add_argument("--out", default="model_bits.pt")
    args = ap.parse_args()

    torch.manual_seed(0)
    tr = load([os.path.join(args.data_dir, "seed_%d.json" % s)
               for s in range(1, 31)
               if os.path.exists(os.path.join(args.data_dir, "seed_%d.json" % s))])
    va = load([os.path.join(args.data_dir, "seed_%d.json" % s)
               for s in range(31, 41)
               if os.path.exists(os.path.join(args.data_dir, "seed_%d.json" % s))])
    print("uczenie:   %d bitow (%d flag, %.1f%%)"
          % (len(tr), sum(r["label"] for r in tr), 100 * sum(r["label"] for r in tr) / len(tr)))
    print("walidacja: %d bitow (%d flag)" % (len(va), sum(r["label"] for r in va)))

    Xtr = torch.tensor([r["features"] for r in tr], dtype=torch.float32)
    ytr = torch.tensor([float(r["label"]) for r in tr], dtype=torch.float32)
    Xva = torch.tensor([r["features"] for r in va], dtype=torch.float32)

    model = BitMLP(Xtr.shape[1])
    print("siec: %d -> 32 -> 16 -> 1, %d parametrow\n"
          % (Xtr.shape[1], sum(p.numel() for p in model.parameters())))

    pw = torch.tensor([(len(ytr) - ytr.sum()) / max(ytr.sum(), 1)])
    lossf = nn.BCEWithLogitsLoss(pos_weight=pw)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)

    best_f1, best_state = -1.0, None
    yva = [r["label"] for r in va]
    for ep in range(1, args.epochs + 1):
        model.train(); opt.zero_grad()
        loss = lossf(model(Xtr), ytr); loss.backward(); opt.step()
        if ep % 25 == 0:
            model.eval()
            with torch.no_grad():
                pv = (torch.sigmoid(model(Xva)) >= 0.5).tolist()
            f1 = bit_metrics(pv, yva)[5]
            if f1 > best_f1:
                best_f1 = f1
                best_state = {k: v.clone() for k, v in model.state_dict().items()}
            if ep % 150 == 0:
                print("  epoka %4d  strata=%.4f  walidacja F1=%.1f%%" % (ep, loss.item(), f1 * 100))
    model.load_state_dict(best_state)
    model.eval()

    with torch.no_grad():
        pv = (torch.sigmoid(model(Xva)) >= 0.5).tolist()
    report("WALIDACJA (ziarna 31-40, vcan)", va, pv, [bool(r["baseline_seen"]) for r in va])

    if args.test_glob:
        te = load(sorted(glob.glob(args.test_glob)))
        if te:
            Xte = torch.tensor([r["features"] for r in te], dtype=torch.float32)
            with torch.no_grad():
                pt = (torch.sigmoid(model(Xte)) >= 0.5).tolist()
            report("TEST -- PRAWDZIWY MCP2515 (ziarna 100-105)", te, pt,
                   [bool(r["baseline_seen"]) for r in te])

    torch.save({"state_dict": model.state_dict(), "n_features": Xtr.shape[1]}, args.out)
    print("\nzapisano model:", args.out)


if __name__ == "__main__":
    main()
