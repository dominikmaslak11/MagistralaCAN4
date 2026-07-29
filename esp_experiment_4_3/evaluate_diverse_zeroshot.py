#!/usr/bin/env python3
"""
Eksperyment 4.3, Etap B (rozszerzony) — ewaluacja zero-shot LLM + hybrydowego
override'u (Kierunek B, przeportowany z C++) na ZROZNICOWANYM korpusie
(esp_experiment_4_3/generate_traffic_diverse.py), NIEZALEZNIE OD SPRZETU
(bez ESP32/PEAK PCAN-USB — historia ramek symulowana offline, zapytania LLM
wysylane bezposrednio do API tym samym schematem promptu co
DecodingAccuracyRunner w C++).

CEL: sprawdzic, czy (a) slaba skutecznosc zero-shot LLM na flagach bitowych
i (b) skutecznosc hybrydowego klasyfikatora GENERALIZUJA na wiele roznych
konfiguracji bit-packingu (rozne pozycje bajtu, liczba flag 2-6, bajty
mieszane flaga+podskalar, flagi rozproszone) — czy Eksperyment 4.1 (Claude
0% na dokladnie JEDNYM przypadku 5 flag w bajcie 0) byl reprezentatywny, czy
specyficzny dla tego jednego ukladu.

Uzycie:
  python3 evaluate_diverse_zeroshot.py <plik_kluczy_api> <katalog_wyjsciowy> \
      [--models claude-sonnet-5,gpt-5.6-sol] [--n-configs 30] [--repeats 10] \
      [--seed 42]

Wyniki zapisywane INKREMENTALNIE (co probe) do JSON w katalogu wyjsciowym -
bezpieczne dla dlugich przebiegow (np. na noc) - przerwanie w dowolnym
momencie zostawia uzyteczne, czesciowe dane.
"""
import argparse
import json
import random
import re
import sys
import time
import traceback
from pathlib import Path

import requests

sys.path.insert(0, str(Path(__file__).parent))
from generate_traffic_diverse import (  # noqa: E402
    make_diverse_configs, DiverseSim, CAN_FRAME_FMT,
)
import struct

ZERO_SHOT_SYSTEM_PROMPT = (
    "You are a CAN bus reverse-engineering assistant. A CAN message (fixed ID) "
    "typically packs SEVERAL distinct signals into its payload bytes (e.g. one "
    "message might contain both engine RPM as a 2-byte value AND a temperature "
    "as a 1-byte value). Analyze the trigger frame and the recent frames for this "
    "ID (which show how values change over time) and identify ALL signals you can "
    "confidently distinguish, then propose a decoding rule for each.\n\n"
    "Respond with ONLY a JSON object (no markdown fences, no prose outside the "
    "JSON) with this exact shape:\n"
    "{\n"
    "  \"interpretation\": \"short description of the message\",\n"
    "  \"signals\": [\n"
    "    {\n"
    "      \"name\": \"short_signal_name\",\n"
    "      \"byteIdx\": <int, 0-based starting byte offset>,\n"
    "      \"byteLen\": <1 or 2, number of bytes the signal spans>,\n"
    "      \"littleEndian\": <bool, true if multi-byte signal is little-endian>,\n"
    "      \"isSigned\": <bool, true if the value can be negative (two's complement)>,\n"
    "      \"bitMask\": <hex string like \"0x01\" if this signal is a SINGLE BIT "
    "within byteIdx (discrete on/off state), or null if it uses the full byte(s)>,\n"
    "      \"scale\": <float, physical_value = raw * scale + offset>,\n"
    "      \"offset\": <float>\n"
    "    }\n"
    "  ],\n"
    "  \"confidence\": <float 0.0-1.0>\n"
    "}\n\n"
    "Discrete on/off states (lights, doors, indicators) are single bits within one "
    "byte — use bitMask to isolate the bit, byteLen=1, scale=1, offset=0. Continuous "
    "measurements (RPM, temperature, angle, speed) use byteLen=1 or 2 depending on "
    "the value range you observe, bitMask=null, and scale/offset chosen so the "
    "decoded physical value matches plausible real-world magnitudes for that quantity.")

MODEL_ENDPOINTS = {
    "claude-sonnet-5": ("anthropic", "https://api.anthropic.com/v1/messages"),
    "gpt-5.6-sol": ("openai", "https://api.openai.com/v1/chat/completions"),
    "deepseek-v4-pro": ("deepseek", "https://api.deepseek.com/v1/chat/completions"),
    "gemini-3.6-flash": ("gemini", "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.6-flash:generateContent"),
}

MODEL_TO_KEYNAME = {
    "claude-sonnet-5": "CLAUDE",
    "gpt-5.6-sol": "CODEX",
    "deepseek-v4-pro": "DeepSeek",
    "gemini-3.6-flash": "Gemini",
}


def load_api_keys(path):
    keys = {}
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if ":" not in line:
            continue
        label, val = line.split(":", 1)
        label = label.replace("API Key", "").strip()
        keys[label] = val.strip()
    return keys


# ── Formatowanie ramek (musi byc identyczne z LlmQueryClient::formatFrameList) ─

def format_frame(can_id, dlc, data_bytes, ts):
    data_str = "".join(f"{b:02x}" for b in data_bytes[:dlc])
    return f"  ID=0x{can_id:03x} DLC={dlc} data=[{data_str}] ts={ts}"


def format_frame_list(frames_meta, max_frames=None):
    if max_frames is None:
        max_frames = len(frames_meta)
    lines = []
    for i, (cid, dlc, data, ts) in enumerate(frames_meta[:max_frames]):
        lines.append(format_frame(cid, dlc, data, ts))
    result = "\n".join(lines)
    if len(frames_meta) > max_frames:
        result += f"\n  ... and {len(frames_meta) - max_frames} more frames"
    return result


def build_user_content(can_id, dlc, trigger_data, recent_frames_meta):
    trigger_str = format_frame_list([(can_id, dlc, trigger_data, recent_frames_meta[-1][3] if recent_frames_meta else 0)], 1)
    recent_str = format_frame_list(recent_frames_meta)
    return (f"Analyze the following CAN frame and suggest an interpretation:\n\n"
            f"CAN ID: 0x{can_id:03x} (DLC: {dlc})\n"
            f"Data bytes: {trigger_str}\n\n"
            f"Recent frames for this ID:\n{recent_str}")


# ── Zapytania API (replikuja dokladnie LlmQueryClient.cpp) ──────────────────

def query_llm(model, backend, endpoint, api_key, system_prompt, user_content, max_tokens=8192, timeout_s=60):
    if backend == "anthropic":
        headers = {"x-api-key": api_key, "anthropic-version": "2023-06-01",
                   "content-type": "application/json"}
        body = {"model": model, "max_tokens": max_tokens,
                "messages": [{"role": "user", "content": user_content}],
                "system": system_prompt}
        r = requests.post(endpoint, headers=headers, json=body, timeout=timeout_s)
        r.raise_for_status()
        j = r.json()
        return "".join(block.get("text", "") for block in j.get("content", []))

    if backend in ("openai", "deepseek"):
        headers = {"Authorization": f"Bearer {api_key}", "content-type": "application/json"}
        messages = [{"role": "system", "content": system_prompt},
                    {"role": "user", "content": user_content}]
        body = {"model": model, "messages": messages}
        if backend == "openai":
            body["max_completion_tokens"] = max_tokens
        else:
            body["max_tokens"] = max_tokens
            body["temperature"] = 0.7
        r = requests.post(endpoint, headers=headers, json=body, timeout=timeout_s)
        r.raise_for_status()
        j = r.json()
        return j["choices"][0]["message"]["content"]

    if backend == "gemini":
        full_text = system_prompt + "\n\n" + user_content
        body = {"contents": [{"parts": [{"text": full_text}]}],
                "generationConfig": {"maxOutputTokens": max_tokens}}
        r = requests.post(endpoint, params={"key": api_key}, json=body, timeout=timeout_s)
        r.raise_for_status()
        j = r.json()
        return j["candidates"][0]["content"]["parts"][0]["text"]

    raise ValueError(f"nieznany backend: {backend}")


# ── Parsowanie odpowiedzi ────────────────────────────────────────────────────

def strip_code_fences(text):
    t = text.strip()
    if t.startswith("```"):
        first_nl = t.find("\n")
        if first_nl >= 0:
            t = t[first_nl + 1:]
        if t.endswith("```"):
            t = t[:-3]
    return t.strip()


def extract_outer_json(text):
    start = text.find("{")
    end = text.rfind("}")
    if start < 0 or end < 0 or end <= start:
        return None
    return text[start:end + 1]


def parse_bit_mask(v):
    if v is None:
        return 0xFFFFFFFF
    if isinstance(v, str):
        v = v.strip()
        try:
            return int(v, 16) if v.lower().startswith("0x") else int(v, 10)
        except ValueError:
            return 0xFFFFFFFF
    if isinstance(v, (int, float)):
        return int(v)
    return 0xFFFFFFFF


def parse_rules(text):
    candidate = extract_outer_json(strip_code_fences(text))
    if not candidate:
        return []
    try:
        obj = json.loads(candidate)
    except json.JSONDecodeError:
        return []
    out = []
    for s in obj.get("signals", []):
        if "byteIdx" not in s:
            continue
        out.append({
            "name": s.get("name", ""),
            "byteIdx": int(s.get("byteIdx", -1)),
            "byteLen": max(1, int(s.get("byteLen", 1))),
            "littleEndian": bool(s.get("littleEndian", True)),
            "isSigned": bool(s.get("isSigned", False)),
            "bitMask": parse_bit_mask(s.get("bitMask")),
            "scale": float(s.get("scale", 1.0)),
            "offset": float(s.get("offset", 0.0)),
        })
    return out


# ── Klasyfikator Kierunku B (przeportowany z DecodingAccuracyRunner.cpp v2) ──

def independent_bit_mask(byte_values):
    seen0 = seen1 = 0
    for v in byte_values:
        for b in range(8):
            if v & (1 << b):
                seen1 |= (1 << b)
            else:
                seen0 |= (1 << b)
    return seen0 & seen1


def looks_like_bit_flags(byte_values):
    if len(byte_values) < 2:
        return False
    mask = independent_bit_mask(byte_values)
    bit_count = bin(mask).count("1")
    if bit_count < 2 or bit_count > 6:
        return False
    big_jumps = changed_pairs = 0
    for i in range(1, len(byte_values)):
        a, b = byte_values[i - 1], byte_values[i]
        if a == b:
            continue
        changed_pairs += 1
        if abs(a - b) > 3:
            big_jumps += 1
    if changed_pairs == 0:
        return False
    return (big_jumps / changed_pairs) >= 0.5


def apply_bit_flag_override(rules, frames_bytes_by_idx):
    """frames_bytes_by_idx: dict byte_idx -> list of byte values across history."""
    out = []
    for r in rules:
        is_plain_byte_scalar = (r["byteLen"] == 1 and r["bitMask"] == 0xFFFFFFFF)
        if is_plain_byte_scalar and r["byteIdx"] in frames_bytes_by_idx:
            values = frames_bytes_by_idx[r["byteIdx"]]
            if looks_like_bit_flags(values):
                mask = independent_bit_mask(values)
                for b in range(8):
                    if not (mask & (1 << b)):
                        continue
                    out.append({
                        "name": f"{r['name']}_bit{b}_override", "byteIdx": r["byteIdx"],
                        "byteLen": 1, "littleEndian": True, "isSigned": False,
                        "bitMask": (1 << b), "scale": 1.0, "offset": 0.0,
                    })
                continue
        out.append(r)
    return out


# ── Dopasowanie regul do ground truth (odpowiednik findMatchingRule) ────────

def single_bit_position(mask):
    pos = -1
    for b in range(32):
        if mask & (1 << b):
            if pos != -1:
                return -2
            pos = b
    return pos


def find_matching_rule(gt, rules):
    if gt["is_discrete"]:
        for r in rules:
            if r["byteIdx"] != gt["byte_idx"]:
                continue
            if r["bitMask"] in (0xFFFFFFFF, 0):
                continue
            if single_bit_position(r["bitMask"]) == gt["bit_idx"]:
                return r
        return None
    fallback = None
    for r in rules:
        if r["byteIdx"] != gt["byte_idx"]:
            continue
        if r["bitMask"] == 0xFFFFFFFF:
            return r
        if fallback is None:
            fallback = r
    return fallback


def decode_rule(rule, data_bytes):
    raw = 0
    byte_order = range(rule["byteLen"]) if rule["littleEndian"] else range(rule["byteLen"] - 1, -1, -1)
    for shift_i, i in enumerate(byte_order):
        idx = rule["byteIdx"] + i
        if idx >= len(data_bytes):
            return 0.0
        raw |= data_bytes[idx] << (8 * shift_i)
    if rule["bitMask"] != 0xFFFFFFFF:
        raw &= rule["bitMask"]
        if rule["bitMask"] != 0:
            m = rule["bitMask"]
            shift = 0
            while not (m & 1):
                m >>= 1
                shift += 1
            raw >>= shift
    if rule["isSigned"]:
        bits = 8 * rule["byteLen"]
        sign_bit = 1 << (bits - 1)
        if raw & sign_bit:
            raw -= (1 << bits)
    return raw * rule["scale"] + rule["offset"]


def ground_truth_list(cfg):
    gts = []
    for s in cfg.scalars:
        gts.append({"name": s.name, "byte_idx": s.byte_idx, "byte_len": s.byte_len,
                     "little_endian": s.little_endian, "is_signed": s.is_signed,
                     "scale": s.scale, "offset": s.offset, "is_discrete": False,
                     "bit_mask": 0xFFFFFFFF, "bit_idx": -1})
    for ps in cfg.partial_scalars:
        gts.append({"name": ps.name, "byte_idx": ps.byte_idx, "byte_len": 1,
                     "little_endian": True, "is_signed": False,
                     "scale": ps.scale, "offset": ps.offset, "is_discrete": False,
                     "bit_mask": ps.bit_mask, "bit_idx": -1})
    for f in cfg.flags:
        gts.append({"name": f.name, "byte_idx": f.byte_idx, "byte_len": 1,
                     "little_endian": True, "is_signed": False,
                     "scale": 1.0, "offset": 0.0, "is_discrete": True,
                     "bit_mask": (1 << f.bit_idx), "bit_idx": f.bit_idx})
    return gts


def decode_gt(gt, data_bytes):
    fake_rule = {"byteIdx": gt["byte_idx"], "byteLen": gt["byte_len"],
                 "littleEndian": gt["little_endian"], "isSigned": gt["is_signed"],
                 "bitMask": gt["bit_mask"] if gt["is_discrete"] or gt["bit_mask"] != 0xFFFFFFFF else 0xFFFFFFFF,
                 "scale": gt["scale"], "offset": gt["offset"]}
    return decode_rule(fake_rule, data_bytes)


# ── Generowanie historii ramek dla jednej konfiguracji (bez sprzetu) ────────

def build_history(cfg, sim, n_history=30, sample_spacing_fake_s=25.0):
    """Symuluje n_history probek tej JEDNEJ konfiguracji, z odstepem
    czasowym symulujacym RZADKIE probkowanie (jak w prawdziwym round-robin
    Eksperymentu 4.1, gdzie odstep miedzy kolejnymi Cold Start dla tego
    samego ID wynosil rzedu dziesiatek sekund) - kluczowe dla poprawnego
    zachowania klasyfikatora Kierunku B (patrz naprawa heurystyki v1->v2,
    Eksperyment_4.1_Hybrydowy_Override_Infografika)."""
    history = []
    ts = 0
    for _ in range(n_history):
        sim.t0 -= sample_spacing_fake_s
        for key in list(sim.next_toggle.keys()):
            sim.next_toggle[key] -= sample_spacing_fake_s
        sim.step()
        frame_bytes = sim.build_frame_for(cfg)
        _, _, payload = struct.unpack(CAN_FRAME_FMT, frame_bytes)
        data = list(payload[:cfg.dlc])
        history.append((cfg.can_id, cfg.dlc, data, ts))
        ts += int(sample_spacing_fake_s * 1000)
    return history


# ── Glowna petla ewaluacyjna ─────────────────────────────────────────────────

def evaluate_one_trial(cfg, sim, model, backend, endpoint, api_key, trial_idx):
    history = build_history(cfg, sim)
    trigger_data = history[-1][2]
    user_content = build_user_content(cfg.can_id, cfg.dlc, trigger_data, history)

    entry = {"can_id": cfg.can_id, "trial": trial_idx, "model": model}
    try:
        text = query_llm(model, backend, endpoint, api_key, ZERO_SHOT_SYSTEM_PROMPT, user_content)
        entry["success"] = True
        entry["ruleText"] = text
    except Exception as e:
        entry["success"] = False
        entry["error"] = str(e)
        return entry

    rules = parse_rules(text)
    entry["parsedSignals"] = [{"name": r["name"], "byteIdx": r["byteIdx"]} for r in rules]

    byte_history = {}
    for b in range(cfg.dlc):
        byte_history[b] = [h[2][b] for h in history]
    override_rules = apply_bit_flag_override(rules, byte_history)
    entry["overrideChangedRules"] = (len(override_rules) != len(rules))

    gts = ground_truth_list(cfg)
    per_signal = []
    for gt in gts:
        raw_match = find_matching_rule(gt, rules)
        ov_match = find_matching_rule(gt, override_rules)
        result = {"name": gt["name"], "is_discrete": gt["is_discrete"],
                  "raw_detected": raw_match is not None,
                  "override_detected": ov_match is not None}
        if raw_match is not None:
            truth = decode_gt(gt, trigger_data)
            pred = decode_rule(raw_match, trigger_data)
            result["raw_error"] = abs(truth - pred)
            if gt["is_discrete"]:
                result["raw_correct_class"] = (truth >= 0.5) == (pred >= 0.5)
        if ov_match is not None:
            truth = decode_gt(gt, trigger_data)
            pred = decode_rule(ov_match, trigger_data)
            result["override_error"] = abs(truth - pred)
            if gt["is_discrete"]:
                result["override_correct_class"] = (truth >= 0.5) == (pred >= 0.5)
        per_signal.append(result)
    entry["per_signal"] = per_signal
    return entry


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("api_keys_path")
    ap.add_argument("out_dir")
    ap.add_argument("--models", default="claude-sonnet-5")
    ap.add_argument("--n-configs", type=int, default=30)
    ap.add_argument("--repeats", type=int, default=10)
    ap.add_argument("--seed", type=int, default=42)
    args = ap.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    keys = load_api_keys(args.api_keys_path)
    models = [m.strip() for m in args.models.split(",")]

    configs = make_diverse_configs(seed=args.seed, n_configs=args.n_configs)
    print(f"Zaladowano {len(configs)} zroznicowanych konfiguracji (ziarno={args.seed}).")
    print(f"Modele: {models}, powtorzen na konfiguracje: {args.repeats}")
    print(f"Razem prob: {len(models) * len(configs) * args.repeats}")

    for model in models:
        backend, endpoint = MODEL_ENDPOINTS[model]
        api_key = keys.get(MODEL_TO_KEYNAME[model])
        if not api_key:
            print(f"[POMINIETO] Brak klucza API dla {model}")
            continue

        result_path = out_dir / f"results_{model.replace('.', '_')}.jsonl"
        print(f"\n=== Model: {model} ({backend}) -> {result_path} ===")
        sim = DiverseSim(configs, seed=args.seed + 1000)

        n_done = 0
        n_total = len(configs) * args.repeats
        with open(result_path, "a", encoding="utf-8") as fout:
            for rep in range(args.repeats):
                for cfg in configs:
                    for attempt in range(3):
                        try:
                            entry = evaluate_one_trial(cfg, sim, model, backend, endpoint, api_key, rep)
                            break
                        except Exception:
                            traceback.print_exc()
                            time.sleep(5)
                    else:
                        entry = {"can_id": cfg.can_id, "trial": rep, "model": model,
                                  "success": False, "error": "max retries exceeded"}
                    fout.write(json.dumps(entry) + "\n")
                    fout.flush()
                    n_done += 1
                    status = "OK" if entry.get("success") else "BLAD"
                    if n_done % 10 == 0 or not entry.get("success"):
                        print(f"  [{n_done}/{n_total}] rep={rep} can_id=0x{cfg.can_id:03x} {status}")
                    time.sleep(1.0)  # lagodne tempo, unikanie limitow API

        print(f"Zakonczono model {model}: {n_done} prob zapisanych w {result_path}")

    print("\nGotowe. Uruchom analyze_diverse_results.py, zeby podsumowac wyniki.")


if __name__ == "__main__":
    main()
