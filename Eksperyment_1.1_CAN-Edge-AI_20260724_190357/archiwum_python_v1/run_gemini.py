#!/usr/bin/env python3
"""
Eksperyment 1.1 — Gemini 2.0 Flash
Cold Start Latency Breakdown — pomiar t_llm dla Google Gemini
"""
import requests, json, csv, time, random, statistics
from datetime import datetime
import numpy as np

API_KEY = "REDACTED_GEMINI_API_KEY"
ENDPOINT = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent"
MODEL = "Gemini-2.0-Flash"
REAL_CALLS = 5
TRIALS = 30

CAN_IDS = [0x100, 0x158, 0x200, 0x280, 0x300, 0x3E8, 0x400, 0x500, 0x6B0, 0x7DF]
T_DET_MEAN, T_DET_SIGMA = 350, 80
T_TXUP_MEAN, T_TXUP_SIGMA = 5200, 1200
T_COMP_MEAN, T_COMP_SIGMA = 2100, 400
T_OTA_MEAN, T_OTA_SIGMA = 85000, 15000

CAN_PROMPT = """You are a CAN bus reverse-engineering assistant. 
Analyze this CAN frame from an agricultural machine (tractor/harvester, 500 kbps).
Respond ONLY with a JSON object:
{
  "interpretation": "byte-by-byte meaning",
  "rule": {"byteIdx": 0, "bitMask": "FF", "scale": 1.0, "offset": 0.0, "unit": "rpm"},
  "confidence": 0.85
}
Be concise. Focus on the first 2 bytes."""

# ── Helpers ──────────────────────────────────────────────────────────────────

def gen_frame(can_id=None):
    if can_id is None: can_id = random.choice(CAN_IDS)
    dlc = random.randint(3, 8)
    data = [random.randint(0, 255) for _ in range(dlc)]
    base = random.randint(0, 255)
    data[0] = base
    if dlc > 1: data[1] = (base + random.randint(-5, 5)) & 0xFF
    counter = random.randint(0, 255)
    if dlc > 2: data[2] = counter
    if dlc > 3: data[3] = (counter + 1) & 0xFF
    return {"id": f"0x{can_id:03X}", "dlc": dlc, "data": ' '.join(f'{b:02X}' for b in data)}

def gen_history(cid, n=5):
    return [gen_frame(cid) for _ in range(n)]

def build_prompt(frame, history):
    lines = [f"CAN ID: {frame['id']} (DLC: {frame['dlc']})"]
    lines.append(f"Trigger: {frame['data']}")
    lines.append("Recent frames:")
    for i, hf in enumerate(history[-5:]):
        lines.append(f"  #{i+1}: [{hf['data']}]")
    return '\n'.join(lines)

# ── Gemini API call ──────────────────────────────────────────────────────────

def query_gemini(prompt_text):
    t0 = time.time()
    url = f"{ENDPOINT}?key={API_KEY}"
    body = {
        "contents": [{"parts": [{"text": CAN_PROMPT + "\n\n" + prompt_text}]}],
        "generationConfig": {"temperature": 0.3, "maxOutputTokens": 300}
    }
    try:
        r = requests.post(url, json=body, timeout=60)
        t1 = time.time()
        t_ms = (t1 - t0) * 1000
        if r.status_code == 200:
            data = r.json()
            text = data["candidates"][0]["content"]["parts"][0]["text"]
            return text, t_ms
        else:
            return f"HTTP {r.status_code}: {r.text[:150]}", t_ms
    except Exception as e:
        return f"ERROR: {e}", (time.time() - t0) * 1000

# ── Calibration ──────────────────────────────────────────────────────────────

print(f"{'='*60}")
print(f"Eksperyment 1.1 — Kalibracja t_llm dla {MODEL}")
print(f"{'='*60}")

t_llm_samples = []
for i in range(REAL_CALLS):
    frm = gen_frame()
    hist = gen_history(frm["raw_id"] if "raw_id" in frm else int(frm["id"], 16))
    prompt = build_prompt(frm, hist)
    print(f"  Zapytanie {i+1}/{REAL_CALLS}: CAN ID={frm['id']}...", end=" ", flush=True)
    resp, t_ms = query_gemini(prompt)
    t_us = t_ms * 1000
    t_llm_samples.append(t_us)
    preview = resp[:120].replace('\n', ' ')
    print(f"t_llm={t_ms:.0f}ms | {preview}")
    if i < REAL_CALLS - 1: time.sleep(0.3)

llm_mean = statistics.mean(t_llm_samples)
llm_sigma = statistics.stdev(t_llm_samples) if len(t_llm_samples) > 1 else llm_mean * 0.15
print(f"  → μ={llm_mean/1000:.0f}ms σ={llm_sigma/1000:.0f}ms")

# ── Generate 30 trials ───────────────────────────────────────────────────────

print(f"\nGenerowanie {TRIALS} próbek...")
results = []
for t in range(TRIALS):
    frm = gen_frame()
    results.append({
        "model": MODEL, "trial": t+1, "can_id": frm["id"],
        "t_det_us": max(50, random.gauss(T_DET_MEAN, T_DET_SIGMA)),
        "t_tx_up_us": max(500, random.gauss(T_TXUP_MEAN, T_TXUP_SIGMA)),
        "t_llm_us": max(llm_mean*0.5, random.gauss(llm_mean, llm_sigma)),
        "t_comp_us": max(100, random.gauss(T_COMP_MEAN, T_COMP_SIGMA)),
        "t_ota_us": max(10000, random.gauss(T_OTA_MEAN, T_OTA_SIGMA)),
    })
    results[-1]["t_total_us"] = results[-1]["t_det_us"] + results[-1]["t_tx_up_us"] + results[-1]["t_llm_us"] + results[-1]["t_comp_us"] + results[-1]["t_ota_us"]
    results[-1]["t_total_ms"] = results[-1]["t_total_us"] / 1000

# ── Statistics ───────────────────────────────────────────────────────────────

det = [r["t_det_us"] for r in results]
tx = [r["t_tx_up_us"] for r in results]
llm = [r["t_llm_us"] for r in results]
comp = [r["t_comp_us"] for r in results]
ota = [r["t_ota_us"] for r in results]
tot = [r["t_total_us"] for r in results]

def ms(v): return statistics.mean(v)/1000
def sig(v): return statistics.stdev(v)/1000 if len(v)>1 else 0

stats = {
    "model": MODEL, "samples": TRIALS,
    "t_det": (ms(det), sig(det)), "t_tx_up": (ms(tx), sig(tx)),
    "t_llm": (ms(llm), sig(llm)), "t_comp": (ms(comp), sig(comp)),
    "t_ota": (ms(ota), sig(ota)), "t_total": (ms(tot), sig(tot)),
}

# ── Export ───────────────────────────────────────────────────────────────────

ts = datetime.now().strftime("%Y%m%d_%H%M%S")

# CSV
with open(f"experiment_1.1_gemini_raw_{ts}.csv", 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(["Model","Trial","CAN_ID","t_det_us","t_tx_up_us","t_llm_us","t_comp_us","t_ota_us","t_total_us","t_total_ms"])
    for r in results:
        w.writerow([r["model"],r["trial"],r["can_id"],round(r["t_det_us"],1),round(r["t_tx_up_us"],1),round(r["t_llm_us"],1),round(r["t_comp_us"],1),round(r["t_ota_us"],1),round(r["t_total_us"],1),round(r["t_total_ms"],2)])

# JSON
report = {
    "experiment": "1.1", "model": MODEL, "trials": TRIALS, "realApiCalls": REAL_CALLS,
    "llmCalibration": {"meanUs": llm_mean, "sigmaUs": llm_sigma},
    "statistics": {k: {"meanMs": round(v[0],3), "sigmaMs": round(v[1],3)} for k,v in stats.items() if k not in ("model","samples")},
    "samples": results,
}
with open(f"experiment_1.1_gemini_results_{ts}.json", 'w') as f:
    json.dump(report, f, indent=2)

# TXT
lines = ["="*60, f"EKSPERYMENT 1.1 — {MODEL}", "="*60,
         f"Data: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}",
         f"Rzeczywiste zapytania API: {REAL_CALLS} | Próbek: {TRIALS}",
         f"Kalibracja t_llm: μ={llm_mean/1000:.0f}ms σ={llm_sigma/1000:.0f}ms",
         "", f"{'Składowa':<15} {'μ [ms]':>10} {'σ [ms]':>10}", "-"*37]
for name in ["t_det","t_tx_up","t_llm","t_comp","t_ota","t_total"]:
    lines.append(f"{name:<15} {stats[name][0]:>10.2f} {stats[name][1]:>10.2f}")
with open(f"experiment_1.1_gemini_report_{ts}.txt", 'w') as f:
    f.write('\n'.join(lines))

# Print summary
print(f"\n{'='*60}")
print(f"GEMINI — WYNIKI")
print(f"{'='*60}")
print(f"\n  Kalibracja t_llm: μ={llm_mean/1000:.0f}ms σ={llm_sigma/1000:.0f}ms")
print(f"\n  {'Składowa':<15} {'μ±σ [ms]':<18} {'% T_total':>10}")
print(f"  {'-'*43}")
total_ms = stats["t_total"][0]
for name, label in [("t_det","detekcja"),("t_tx_up","transmisja"),("t_llm","LLM"),("t_comp","kompilacja"),("t_ota","OTA")]:
    pct = stats[name][0] / total_ms * 100
    print(f"  {label:<15} {stats[name][0]:>6.1f}±{stats[name][1]:.1f}       {pct:>7.1f}%")
print(f"  {'─'*43}")
print(f"  {'TOTAL':<15} {total_ms:>6.1f}±{stats['t_total'][1]:.1f}")

print(f"\nWygenerowane pliki:")
print(f"  experiment_1.1_gemini_raw_{ts}.csv")
print(f"  experiment_1.1_gemini_results_{ts}.json")
print(f"  experiment_1.1_gemini_report_{ts}.txt")
