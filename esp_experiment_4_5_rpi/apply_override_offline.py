#!/usr/bin/env python3
"""
Eksperyment 4.5, Faza 4 -- test "hybrydowego override" (Kierunek B,
Eksperyment 4.1: DecodingAccuracyRunner::applyBitFlagOverride, C++) na
JUZ ZEBRANYCH odpowiedziach baseline z Fazy 3 -- BEZ zadnych nowych,
platnych wywolan API.

Mechanizm (1:1 port C++, nie miekka podpowiedz do promptu jak Qdrant
warmstart): jesli LLM zaproponowal pojedynczy skalar (byteLen=1, bitMask=null)
dla bajtu, ktory nasz (przestrojony w Eksperymencie 4.5, prog=0.3) klasyfikator
`looks_like_bit_flags` klasyfikuje jako flagi bitowe -- TWARDO zastepuje ta
regule zestawem regul per-bit (wg wykrytej maski), niezaleznie od tego, co
"mysli" model. To odtwarza dokladnie mechanizm, ktory w Eksperymencie 4.1
dal 0%->90-97% detekcji flag -- w przeciwienstwie do miekkich podpowiedzi
Qdrant (Eksperyment 4.4/4.5), ktore konsekwentnie SZKODZILY.

Uzycie:
  python3 apply_override_offline.py --trials live_trials_seed999.json \\
      --results-dir results_llm --out results_override
"""
import argparse
import json
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "esp_experiment_4_3"))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "esp_experiment_4_4_qdrant"))

from etap_b_autolabel import independent_bit_mask, looks_like_bit_flags  # noqa: E402
from evaluate_with_llms import (  # noqa: E402
    strip_code_fences, extract_json_object, parse_bitmask, evaluate_response,
)

OVERRIDE_THRESHOLD = 0.3  # patrz Eksperyment_4.5_Strojenie_Progu_Klasyfikatora


def build_byte_series_per_trial(trial):
    all_frames_hex = [f["data"] for f in trial["recentFrames"]] + [trial["triggerFrame"]["data"]]
    return [bytes.fromhex(h) for h in all_frames_hex]


def apply_bit_flag_override(signals, all_bytes, threshold=OVERRIDE_THRESHOLD):
    """1:1 rownowaznik applyBitFlagOverride() z DecodingAccuracyRunner.cpp.
    Klasyfikator liczony NA NOWO z 30-klatkowego okna tej pojedynczej proby
    (jak w oryginalnym mechanizmie C++/Eksperyment 4.1)."""
    out = []
    for sig in signals:
        byte_idx = sig.get("byteIdx")
        byte_len = sig.get("byteLen")
        bit_mask = parse_bitmask(sig.get("bitMask"))
        is_plain_byte_scalar = (byte_len == 1 and bit_mask is None and byte_idx is not None)
        if is_plain_byte_scalar:
            series = [frame[byte_idx] for frame in all_bytes if byte_idx < len(frame)]
            if series and looks_like_bit_flags(series, threshold):
                mask = independent_bit_mask(series)
                for b in range(8):
                    if not (mask & (1 << b)):
                        continue
                    out.append({
                        "name": f"{sig.get('name', 'sig')}_bit{b}_override",
                        "byteIdx": byte_idx, "byteLen": 1,
                        "littleEndian": True, "isSigned": False,
                        "bitMask": 1 << b, "scale": 1.0, "offset": 0.0,
                    })
                continue
        out.append(sig)
    return out


def load_phase1_verdicts(state_path, threshold=OVERRIDE_THRESHOLD):
    """Wczytuje observer_state.json (Faza 1, cala godzina, ~1.6mln ramek) i
    zwraca {(can_id, byte_idx): mask|None} - None jesli klasyfikator (na
    pelnych, ciaglych statystykach) NIE uznaje pozycji za bit_flag."""
    with open(state_path) as f:
        state = json.load(f)
    verdicts = {}
    for key, st in state["stats"].items():
        cid_s, idx_s = key.split(":")
        cid, idx = int(cid_s), int(idx_s)
        mask = st["seen0"] & st["seen1"]
        bit_count = bin(mask).count("1")
        is_flags = (
            st["n_samples"] >= 2
            and 2 <= bit_count <= 6
            and st["changed_pairs"] > 0
            and (st["big_jumps"] / st["changed_pairs"]) >= threshold
        )
        verdicts[(cid, idx)] = mask if is_flags else None
    return verdicts


def apply_bit_flag_override_phase1(signals, can_id, phase1_verdicts):
    """Wariant applyBitFlagOverride() zasilany NIE 30-klatkowym oknem proby,
    tylko juz gotowym werdyktem z ciaglej, godzinnej obserwacji Fazy 1 (ten
    sam CAN ID musial byc obecny w tamtym przebiegu)."""
    out = []
    for sig in signals:
        byte_idx = sig.get("byteIdx")
        byte_len = sig.get("byteLen")
        bit_mask = parse_bitmask(sig.get("bitMask"))
        is_plain_byte_scalar = (byte_len == 1 and bit_mask is None and byte_idx is not None)
        if is_plain_byte_scalar:
            mask = phase1_verdicts.get((can_id, byte_idx))
            if mask:
                for b in range(8):
                    if not (mask & (1 << b)):
                        continue
                    out.append({
                        "name": f"{sig.get('name', 'sig')}_bit{b}_override_p1",
                        "byteIdx": byte_idx, "byteLen": 1,
                        "littleEndian": True, "isSigned": False,
                        "bitMask": 1 << b, "scale": 1.0, "offset": 0.0,
                    })
                continue
        out.append(sig)
    return out


def summarize(trial_results):
    per_signal_name_stats = {}
    for tr in trial_results:
        if "error" in tr:
            continue
        for name, res in tr["perSignal"].items():
            s = per_signal_name_stats.setdefault(name, {"detected": 0, "total": 0})
            s["total"] += 1
            s["detected"] += int(res["detected"])
    total_detected = sum(s["detected"] for s in per_signal_name_stats.values())
    total = sum(s["total"] for s in per_signal_name_stats.values())
    return {
        "overallDetectionRatePct": 100.0 * total_detected / total if total else 0.0,
        "nTrialsTotal": len(trial_results),
        "perSignal": {name: 100.0 * s["detected"] / s["total"] if s["total"] else 0.0
                      for name, s in per_signal_name_stats.items()},
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trials", default="live_trials_seed999.json")
    ap.add_argument("--results-dir", default="results_llm")
    ap.add_argument("--out", default="results_override")
    ap.add_argument("--models", default="claude,gpt,deepseek,gemini")
    ap.add_argument("--source", choices=["trial-window", "phase1"], default="trial-window",
                     help="trial-window: klasyfikator liczony na 30 klatkach tej proby (jak w 4.1); "
                          "phase1: werdykt z ciaglej, godzinnej obserwacji observer_state.json")
    ap.add_argument("--phase1-state", default="observer_state.json")
    args = ap.parse_args()

    with open(args.trials) as f:
        data = json.load(f)
    trials_by_idx = {t["trialIdx"]: t for t in data["trials"]}
    ground_truth_by_can_id = {cfg["can_id"]: cfg["signals"] for cfg in data["ground_truth"]}

    os.makedirs(args.out, exist_ok=True)
    kind_by_name = {}
    for sigs in ground_truth_by_can_id.values():
        for s in sigs:
            kind_by_name[s["name"]] = s["kind"]

    phase1_verdicts = load_phase1_verdicts(args.phase1_state) if args.source == "phase1" else None

    print(f"zrodlo override: {args.source}")
    print(f"{'model':10} {'baseline':>10} {'override':>10} {'delta':>8}   "
          f"{'bit_flag base':>14} {'bit_flag ovr':>13} {'delta bf':>9}")

    for model in args.models.split(","):
        path = os.path.join(args.results_dir, f"{model}_baseline.json")
        with open(path) as f:
            base_data = json.load(f)

        trial_results = []
        for tr in base_data["trialLog"]:
            if "error" in tr:
                trial_results.append(tr)
                continue
            trial = trials_by_idx[tr["trialIdx"]]
            all_bytes = build_byte_series_per_trial(trial)
            parsed = extract_json_object(strip_code_fences(tr["rawText"]))
            proposed = parsed.get("signals", []) if parsed else []
            if args.source == "phase1":
                overridden = apply_bit_flag_override_phase1(proposed, trial["canId"], phase1_verdicts)
            else:
                overridden = apply_bit_flag_override(proposed, all_bytes)
            gt_signals = ground_truth_by_can_id[trial["canId"]]
            per_signal = evaluate_response({"signals": overridden}, gt_signals)
            trial_results.append({"trialIdx": tr["trialIdx"], "canId": trial["canId"], "perSignal": per_signal})

        summary = summarize(trial_results)
        out_path = os.path.join(args.out, f"{model}_override_{args.source}.json")
        with open(out_path, "w") as f:
            json.dump({"model": model, "condition": f"override_{args.source}", "summary": summary, "trialLog": trial_results}, f, indent=2)

        base_summary = summarize(base_data["trialLog"])

        # bit_flag agregat
        def bf_rate(summary_obj):
            names = [n for n in summary_obj["perSignal"] if kind_by_name.get(n) == "bit_flag"]
            if not names:
                return 0.0
            return sum(summary_obj["perSignal"][n] for n in names) / len(names)

        bf_base = bf_rate(base_summary)
        bf_ovr = bf_rate(summary)

        print(f"{model:10} {base_summary['overallDetectionRatePct']:>9.1f}% "
              f"{summary['overallDetectionRatePct']:>9.1f}% "
              f"{summary['overallDetectionRatePct']-base_summary['overallDetectionRatePct']:>+7.1f}pp   "
              f"{bf_base:>13.1f}% {bf_ovr:>12.1f}% {bf_ovr-bf_base:>+8.1f}pp")


if __name__ == "__main__":
    main()
