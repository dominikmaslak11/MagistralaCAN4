#!/usr/bin/env python3
"""
Eksperyment 4.4 (Faza B) — ewaluacja przechwyconych prob (live_trials_captured.json,
patrz capture_live_trials.py) na 4 modelach LLM (Claude, GPT/"Codex", DeepSeek,
Gemini), w DWOCH warunkach dla KAZDEJ proby (parowane porownanie):
  1. baseline   - dokladnie ten sam prompt bazowy co Eksperyment 4.1 (zero-shot)
  2. warmstart  - ten sam prompt + dopisany blok "hint" z retrieval Qdrant
                  (per-bajt najblizszy znany sygnal z biblioteki offline,
                  seed=42, ROZNY od seedu ruchu live=999 - test generalizacji)

Endpointy/schematy zapytan/parsowanie odpowiedzi 1:1 z src/core/LlmQueryClient.cpp
(ten sam projekt, C++), zeby wyniki byly metodologicznie porownywalne.

Uzycie:
  python3 evaluate_with_llms.py --trials live_trials_captured.json \\
      --corpus corpus_diverse.json --api-keys ../API_Keys.txt --n 100
"""
import argparse
import json
import re
import statistics
import sys
import os
import time
from dataclasses import dataclass

import requests

from qdrant_client import QdrantClient
from qdrant_client.models import Distance, VectorParams, PointStruct

from qdrant_warmstart_diverse import extract_raw_series, feature_vector

BASE_SYSTEM_PROMPT = (
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
    "decoded physical value matches plausible real-world magnitudes for that quantity."
)

MODEL_CONFIG = {
    "claude": dict(
        backend="anthropic", key_label="CLAUDE", model="claude-sonnet-5",
        endpoint="https://api.anthropic.com/v1/messages", max_tokens=8192,
    ),
    "gpt": dict(
        backend="openai", key_label="CODEX", model="gpt-5.6-sol",
        endpoint="https://api.openai.com/v1/chat/completions", max_tokens=8192,
    ),
    "deepseek": dict(
        backend="openai", key_label="DeepSeek", model="deepseek-v4-pro",
        endpoint="https://api.deepseek.com/v1/chat/completions", max_tokens=8192,
    ),
    "gemini": dict(
        backend="gemini", key_label="Gemini", model="gemini-3.6-flash",
        endpoint="https://generativelanguage.googleapis.com/v1beta/models/gemini-3.6-flash:generateContent",
        max_tokens=8192,
    ),
}


def load_api_keys(path):
    keys = {}
    with open(path) as f:
        for line in f:
            m = re.match(r"^\s*(\S.*?)\s+API Key:\s*(\S+)\s*$", line)
            if m:
                keys[m.group(1)] = m.group(2)
    return keys


def format_frame_list(frames, max_frames=None):
    lines = []
    for i, fr in enumerate(frames):
        if max_frames is not None and i >= max_frames:
            break
        lines.append(f"  ID=0x{fr['id']:03x} DLC={fr['dlc']} data=[{fr['data']}] ts={fr['timestamp']}")
    return "\n".join(lines)


def build_user_prompt(trial, warmstart_hints=None):
    trigger = trial["triggerFrame"]
    user = (
        f"Analyze the following CAN frame and suggest an interpretation:\n\n"
        f"CAN ID: 0x{trigger['id']:03x} (DLC: {trigger['dlc']})\n"
        f"Data bytes: {format_frame_list([trigger], 1)}\n\n"
        f"Recent frames for this ID:\n{format_frame_list(trial['recentFrames'])}"
    )
    if warmstart_hints:
        user += "\n\nADDITIONAL CONTEXT (statistical pre-analysis, verify against actual data before trusting):\n"
        user += "\n".join(warmstart_hints)
    return user


MAX_RETRIES = 5
BASE_BACKOFF_S = 5.0
INTER_REQUEST_DELAY_S = 1.0


def call_llm(model_key, api_key, system_prompt, user_prompt):
    """Wrapper z retry+exponential backoff wokol _call_llm_once - lapie
    rate-limity (429) i przejsciowe bledy sieciowe (timeout/polaczenie),
    ktore w pierwszym przebiegu (bez tego) zepsuly ~62% prob gpt_baseline
    i 100% gpt_warmstart."""
    last_exc = None
    for attempt in range(MAX_RETRIES):
        try:
            return _call_llm_once(model_key, api_key, system_prompt, user_prompt)
        except requests.exceptions.HTTPError as e:
            last_exc = e
            status = e.response.status_code if e.response is not None else None
            if status == 429 or (status is not None and status >= 500):
                wait = BASE_BACKOFF_S * (2 ** attempt)
                print(f"    [retry] {model_key}: HTTP {status}, czekam {wait:.0f}s (proba {attempt+1}/{MAX_RETRIES})")
                time.sleep(wait)
                continue
            raise
        except (requests.exceptions.Timeout, requests.exceptions.ConnectionError) as e:
            last_exc = e
            wait = BASE_BACKOFF_S * (2 ** attempt)
            print(f"    [retry] {model_key}: {type(e).__name__}, czekam {wait:.0f}s (proba {attempt+1}/{MAX_RETRIES})")
            time.sleep(wait)
            continue
    raise last_exc


def _call_llm_once(model_key, api_key, system_prompt, user_prompt):
    cfg = MODEL_CONFIG[model_key]
    t0 = time.time()
    if cfg["backend"] in ("openai",):
        headers = {"Authorization": f"Bearer {api_key}", "Content-Type": "application/json"}
        body = {
            "model": cfg["model"],
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": user_prompt},
            ],
        }
        if cfg["model"].startswith("gpt-"):
            body["max_completion_tokens"] = cfg["max_tokens"]
        else:
            body["max_tokens"] = cfg["max_tokens"]
        r = requests.post(cfg["endpoint"], headers=headers, json=body, timeout=90)
        r.raise_for_status()
        data = r.json()
        text = data["choices"][0]["message"]["content"] or ""
        if not text.strip():
            text = data["choices"][0]["message"].get("reasoning_content", "")
    elif cfg["backend"] == "anthropic":
        headers = {"x-api-key": api_key, "anthropic-version": "2023-06-01", "Content-Type": "application/json"}
        body = {
            "model": cfg["model"],
            "max_tokens": cfg["max_tokens"],
            "system": system_prompt,
            "messages": [{"role": "user", "content": user_prompt}],
        }
        r = requests.post(cfg["endpoint"], headers=headers, json=body, timeout=90)
        r.raise_for_status()
        data = r.json()
        text = "".join(b["text"] for b in data.get("content", []) if b.get("type") == "text")
    elif cfg["backend"] == "gemini":
        url = f"{cfg['endpoint']}?key={api_key}"
        body = {
            "contents": [{"parts": [{"text": system_prompt + "\n\n" + user_prompt}]}],
            "generationConfig": {"temperature": 1.0, "maxOutputTokens": cfg["max_tokens"]},
        }
        r = requests.post(url, json=body, timeout=90)
        r.raise_for_status()
        data = r.json()
        parts = data["candidates"][0]["content"]["parts"]
        text = "".join(p.get("text", "") for p in parts)
    else:
        raise ValueError(cfg["backend"])

    return text.strip(), time.time() - t0


def strip_code_fences(text):
    t = text.strip()
    if t.startswith("```"):
        nl = t.find("\n")
        if nl >= 0:
            t = t[nl + 1:]
        if t.endswith("```"):
            t = t[:-3]
    return t.strip()


def extract_json_object(text):
    start = text.find("{")
    end = text.rfind("}")
    if start < 0 or end < 0:
        return None
    try:
        return json.loads(text[start:end + 1])
    except json.JSONDecodeError:
        return None


def parse_bitmask(v):
    if v is None:
        return None
    if isinstance(v, int):
        return v
    if isinstance(v, str):
        return int(v, 16) if v.lower().startswith("0x") else int(v)
    return None


def build_qdrant_index(corpus_path):
    with open(corpus_path) as f:
        corpus = json.load(f)
    client = QdrantClient(":memory:")
    client.create_collection(collection_name="lib", vectors_config=VectorParams(size=7, distance=Distance.COSINE))
    points = []
    idx = 0
    for cfg in corpus["ground_truth"]:
        can_id = cfg["can_id"]
        samples = corpus["samples"][str(can_id)]
        for sig in cfg["signals"]:
            values = extract_raw_series(samples, sig)
            if sig["kind"] == "bit_flag":
                max_raw = 1
            elif sig["kind"] == "partial_scalar":
                mask = sig["bit_mask"]
                max_raw = mask >> ((mask & -mask).bit_length() - 1)
            else:
                max_raw = (1 << (8 * sig["byte_len"])) - 1
            vec = feature_vector(values, max_raw)
            points.append(PointStruct(
                id=idx, vector=vec,
                payload={"kind": sig["kind"], "is_discrete": sig["is_discrete"], "name": sig["name"]},
            ))
            idx += 1
    client.upsert(collection_name="lib", points=points)
    return client


def warmstart_hints_for_trial(qdrant_client, trial):
    """Dla kazdego bajtu w ramce, wyciaga cechy z recentFrames+trigger i pyta
    Qdrant o najbardziej podobny znany sygnal - zwraca liste opisow tekstowych."""
    dlc = trial["triggerFrame"]["dlc"]
    all_frames_hex = [f["data"] for f in trial["recentFrames"]] + [trial["triggerFrame"]["data"]]
    all_bytes = [bytes.fromhex(h) for h in all_frames_hex]
    if len(all_bytes) < 5:
        return []

    hints = []
    for b in range(dlc):
        series = [frame[b] for frame in all_bytes if b < len(frame)]
        if len(series) < 5:
            continue
        if len(set(series)) == 1:
            # Bajt stale nie zmienia wartosci w calym oknie - typowo
            # padding/nieuzywany. Jakikolwiek "dopasowany" sygnal z Qdrant
            # jest tu przypadkowy (staly bajt wyglada jak nieaktywna flaga
            # niezaleznie od tego, czym naprawde jest) - pomijamy zamiast
            # dawac fasadowo pewna, ale bezwartosciowa/mylaca podpowiedz.
            continue
        vec = feature_vector(series, 255)
        result = qdrant_client.query_points(collection_name="lib", query=vec, limit=1)
        if not result.points:
            continue
        hit = result.points[0]
        if hit.score < 0.85:
            continue
        payload = hit.payload
        hints.append(
            f"  Byte {b}: behavioral pattern resembles a previously seen '{payload['kind']}' "
            f"signal (similarity={hit.score:.2f}, is_discrete={payload['is_discrete']})."
        )
    return hints


def evaluate_response(parsed, ground_truth_signals):
    """Dopasowanie po byteIdx+bitMask (poprawka z Naprawa_Kontekstu/4.1),
    zwraca per-sygnal: detected(bool), i dla ciaglych blad |scale_ratio-1|."""
    results = {}
    proposed = parsed.get("signals", []) if parsed else []
    for gt in ground_truth_signals:
        if gt["kind"] == "bit_flag":
            gt_bitmask = 1 << gt["bit_idx"]
        else:
            gt_bitmask = gt.get("bit_mask")  # partial_scalar ma gotowe, scalar -> None
        match = None
        for p in proposed:
            if p.get("byteIdx") != gt["byte_idx"]:
                continue
            p_mask = parse_bitmask(p.get("bitMask"))
            if gt["is_discrete"]:
                if p_mask == gt_bitmask:
                    match = p
                    break
            else:
                if p_mask is None:
                    match = p
                    break
        results[gt["name"]] = {"detected": match is not None, "is_discrete": gt["is_discrete"]}
    return results


@dataclass
class ModelReport:
    model_key: str
    condition: str
    trial_results: list


def run_model_condition(model_key, api_key, trials, ground_truth_by_can_id, qdrant_client, warmstart):
    trial_results = []
    for trial in trials:
        hints = warmstart_hints_for_trial(qdrant_client, trial) if warmstart else None
        user_prompt = build_user_prompt(trial, hints)
        try:
            text, latency_s = call_llm(model_key, api_key, BASE_SYSTEM_PROMPT, user_prompt)
        except Exception as e:
            trial_results.append({"trialIdx": trial["trialIdx"], "error": str(e)})
            print(f"  [{model_key}/{'warmstart' if warmstart else 'baseline'}] "
                  f"trial {trial['trialIdx']} BLAD (po wyczerpaniu retry): {e}")
            time.sleep(INTER_REQUEST_DELAY_S)
            continue
        parsed = extract_json_object(strip_code_fences(text))
        gt_signals = ground_truth_by_can_id[trial["canId"]]
        per_signal = evaluate_response(parsed, gt_signals)
        trial_results.append({
            "trialIdx": trial["trialIdx"], "canId": trial["canId"],
            "latencyS": latency_s, "rawText": text[:2000], "perSignal": per_signal,
        })
        print(f"  [{model_key}/{'warmstart' if warmstart else 'baseline'}] "
              f"trial {trial['trialIdx']} canId=0x{trial['canId']:03x} "
              f"detected={sum(1 for v in per_signal.values() if v['detected'])}/{len(per_signal)}")
        time.sleep(INTER_REQUEST_DELAY_S)
    n_errors = sum(1 for tr in trial_results if "error" in tr)
    if n_errors:
        print(f"  UWAGA: {n_errors}/{len(trial_results)} prob zakonczonych bledem po wyczerpaniu retry.")
    return trial_results


def summarize(trial_results, ground_truth_by_can_id):
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
    n_errors = sum(1 for tr in trial_results if "error" in tr)
    return {
        "overallDetectionRatePct": 100.0 * total_detected / total if total else 0.0,
        "nErrors": n_errors,
        "nTrialsTotal": len(trial_results),
        "perSignal": {
            name: 100.0 * s["detected"] / s["total"] if s["total"] else 0.0
            for name, s in per_signal_name_stats.items()
        },
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trials", default="live_trials_captured.json")
    ap.add_argument("--corpus", default="corpus_diverse.json")
    ap.add_argument("--api-keys", default="../API_Keys.txt")
    ap.add_argument("--n", type=int, default=100)
    ap.add_argument("--models", default="claude,gpt,deepseek,gemini")
    ap.add_argument("--out-dir", default="results")
    ap.add_argument("--conditions", default="baseline,warmstart",
                     help="np. 'warmstart' zeby powtorzyc tylko ten warunek bez ponownego baseline")
    args = ap.parse_args()
    wanted_conditions = set(args.conditions.split(","))

    with open(args.trials) as f:
        data = json.load(f)
    trials = data["trials"][:args.n]
    ground_truth_by_can_id = {cfg["can_id"]: cfg["signals"] for cfg in data["ground_truth"]}

    api_keys = load_api_keys(args.api_keys)
    qdrant_client = build_qdrant_index(args.corpus)

    os.makedirs(args.out_dir, exist_ok=True)
    models = args.models.split(",")

    for model_key in models:
        cfg = MODEL_CONFIG[model_key]
        api_key = api_keys.get(cfg["key_label"])
        if not api_key:
            print(f"BRAK klucza API dla {model_key} (etykieta '{cfg['key_label']}') - pomijam.")
            continue

        for condition, warmstart in [("baseline", False), ("warmstart", True)]:
            if condition not in wanted_conditions:
                continue
            print(f"\n=== {model_key} / {condition} ({len(trials)} prob) ===")
            trial_results = run_model_condition(model_key, api_key, trials, ground_truth_by_can_id, qdrant_client, warmstart)
            summary = summarize(trial_results, ground_truth_by_can_id)
            out = {"model": model_key, "condition": condition, "summary": summary, "trialLog": trial_results}
            out_path = os.path.join(args.out_dir, f"{model_key}_{condition}.json")
            with open(out_path, "w") as f:
                json.dump(out, f, indent=2)
            print(f"Zapisano {out_path} - detekcja ogolna: {summary['overallDetectionRatePct']:.1f}% "
                  f"(bledy: {summary['nErrors']}/{summary['nTrialsTotal']})")


if __name__ == "__main__":
    main()
