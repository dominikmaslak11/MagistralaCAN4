#!/usr/bin/env python3
"""Generuje pliki danych wejściowych (CAN frames) dla Eksperymentu 1.1."""
import json, csv, random
from datetime import datetime

CAN_IDS = [0x100, 0x158, 0x200, 0x280, 0x300, 0x3E8, 0x400, 0x500, 0x6B0, 0x7DF]
MODELS = ["DeepSeek-V3", "Claude-3.5-Sonnet", "GPT-4o"]
TRIALS = 30

random.seed(42)

def gen_frame(can_id):
    dlc = random.randint(3, 8)
    data = [random.randint(0, 255) for _ in range(dlc)]
    return {"id": f"0x{can_id:03X}", "dlc": dlc, "data_hex": ' '.join(f'{b:02X}' for b in data), "data_dec": data}

timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

# JSON input
input_data = {"experiment": "1.1", "description": "CAN frame input data for Cold Start Latency Breakdown", "models": []}
for model in MODELS:
    model_data = {"model": model, "trials": []}
    for t in range(TRIALS):
        cid = random.choice(CAN_IDS)
        trigger = gen_frame(cid)
        history = [gen_frame(cid) for _ in range(5)]
        model_data["trials"].append({"trial": t+1, "trigger_frame": trigger, "history_frames": history})
    input_data["models"].append(model_data)

with open(f"experiment_1.1_input_{timestamp}.json", 'w') as f:
    json.dump(input_data, f, indent=2)

# CSV input
with open(f"experiment_1.1_input_{timestamp}.csv", 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(["Model","Trial","FrameType","CAN_ID","DLC","Data_Hex","Data_Dec"])
    for model in MODELS:
        for t, trial in enumerate(input_data["models"][MODELS.index(model)]["trials"]):
            tf = trial["trigger_frame"]
            w.writerow([model, t+1, "TRIGGER", tf["id"], tf["dlc"], tf["data_hex"], str(tf["data_dec"])])
            for h, hf in enumerate(trial["history_frames"]):
                w.writerow([model, t+1, f"HISTORY_{h+1}", hf["id"], hf["dlc"], hf["data_hex"], str(hf["data_dec"])])

# TXT input
lines = ["="*60, "EKSPERYMENT 1.1 — DANE WEJŚCIOWE (CAN FRAMES)", "="*60]
for model in MODELS:
    lines.append(f"\n--- {model} ---")
    for t in range(min(3, TRIALS)):
        trial = input_data["models"][MODELS.index(model)]["trials"][t]
        tf = trial["trigger_frame"]
        lines.append(f"\nTrial {t+1}: Trigger CAN ID={tf['id']} DLC={tf['dlc']} data={tf['data_hex']}")
        lines.append("  History (last 5 frames for this ID):")
        for h, hf in enumerate(trial["history_frames"]):
            lines.append(f"    #{h+1}: data=[{hf['data_hex']}]")
    if TRIALS > 3:
        lines.append(f"  ... ({TRIALS-3} more trials)")

with open(f"experiment_1.1_input_{timestamp}.txt", 'w') as f:
    f.write('\n'.join(lines))

print(f"Input files generated ({timestamp}):")
print(f"  experiment_1.1_input_{timestamp}.json")
print(f"  experiment_1.1_input_{timestamp}.csv")
print(f"  experiment_1.1_input_{timestamp}.txt")
