#!/usr/bin/env python3
"""
Eksperyment 1.2 — Hot Execution Latency.
Wysyla N ramek CAN o znanym ID (0x123, zaszyte w firmware esp_experiment_1_2.ino)
przez PCAN-USB (SocketCAN can0) i zbiera czasy reakcji zmierzone WEWNETRZNIE
przez ESP32 (esp_timer_get_time(), miedzy przerwaniem MCP2515 INT a reakcja
GPIO), raportowane przez USB Serial jako "trial,delta_us".

Generuje w katalogu wyjsciowym: raw_data.csv, statistics.csv, report.txt,
histogram.png (Format prezentacji z metodyki: rozklad gestosci prawdopodobienstwa).
"""
import sys
import csv
import subprocess
import time
import statistics as stats_mod
from pathlib import Path

import serial
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

SERIAL_PORT = "/dev/ttyUSB0"
BAUD = 115200
CAN_IFACE = "can0"
TRIGGER_FRAME = "123#DEADBEEF11223344"
FRAME_INTERVAL_S = 0.08

COLOR_HIST = "#2a78d6"
INK = "#0b0b0b"
INK_SECONDARY = "#52514e"
GRIDLINE = "#e1e0d9"
SURFACE = "#fcfcfb"


def main():
    if len(sys.argv) < 3:
        print("Uzycie: run_experiment_1_2.py <katalog_wyjsciowy> <N_probek>")
        sys.exit(1)
    out_dir = Path(sys.argv[1])
    n_trials = int(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)

    ser = serial.Serial(SERIAL_PORT, BAUD, timeout=1)
    time.sleep(0.5)
    ser.reset_input_buffer()

    samples = []  # (trial_idx, delta_us)
    ser.timeout = 0.3  # per-readline timeout - nie caly budzet proby

    print(f"Startuje Eksperyment 1.2 — N={n_trials} probek reakcji Hot Execution...")
    for i in range(n_trials):
        subprocess.run(["cansend", CAN_IFACE, TRIGGER_FRAME], check=False)

        deadline = time.time() + 1.0
        line = None
        while time.time() < deadline:
            raw = ser.readline()  # blokuje do '\n' LUB do ser.timeout (0.3s) - nie dluzej
            if not raw:
                continue
            l = raw.decode(errors="replace").strip()
            if "," in l and l.split(",")[0].isdigit():
                line = l
                break
        if line:
            trial_idx, delta_us = line.split(",")
            samples.append((int(trial_idx), int(delta_us)))
        else:
            print(f"  [UWAGA] Brak odpowiedzi dla proby {i+1}")

        if (i + 1) % 100 == 0:
            print(f"  {i+1}/{n_trials} probek zebranych...")

        time.sleep(FRAME_INTERVAL_S)

    ser.close()
    print(f"Zebrano {len(samples)}/{n_trials} probek.")

    # ── raw_data.csv ──────────────────────────────────────────────────
    raw_path = out_dir / "raw_data.csv"
    with open(raw_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["Trial", "t_resp_us"])
        for trial_idx, delta_us in samples:
            w.writerow([trial_idx, delta_us])
    print(f"  OK {raw_path}")

    deltas = [d for _, d in samples]
    if not deltas:
        print("Brak danych - przerywam.")
        sys.exit(1)

    mean_us = stats_mod.mean(deltas)
    stdev_us = stats_mod.stdev(deltas) if len(deltas) > 1 else 0.0
    median_us = stats_mod.median(deltas)
    min_us = min(deltas)
    max_us = max(deltas)

    # ── statistics.csv ────────────────────────────────────────────────
    stats_path = out_dir / "statistics.csv"
    with open(stats_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["N", "mean_us", "stdev_us", "median_us", "min_us", "max_us"])
        w.writerow([len(deltas), round(mean_us, 2), round(stdev_us, 2),
                    round(median_us, 2), min_us, max_us])
    print(f"  OK {stats_path}")

    # ── histogram.png (Format prezentacji: rozklad gestosci prawdopodobienstwa) ──
    fig, ax = plt.subplots(figsize=(10, 6), facecolor=SURFACE)
    ax.set_facecolor(SURFACE)
    n_bins = min(40, max(10, len(set(deltas))))
    ax.hist(deltas, bins=n_bins, density=True, color=COLOR_HIST,
             edgecolor=SURFACE, linewidth=0.6, alpha=0.9)
    ax.axvline(mean_us, color="#e34948", linewidth=1.5, linestyle="--",
                label=f"μ = {mean_us:.1f} μs")
    ax.set_xlabel("Czas reakcji t_resp [μs]", fontsize=11, color=INK_SECONDARY)
    ax.set_ylabel("Gestosc prawdopodobienstwa", fontsize=11, color=INK_SECONDARY)
    ax.set_title(
        f"Eksperyment 1.2 — Hot Execution Latency (N={len(deltas)})\n"
        f"μ={mean_us:.1f}μs, σ={stdev_us:.1f}μs, min={min_us}μs, max={max_us}μs",
        fontsize=12, fontweight="bold", color=INK, pad=12)
    ax.legend(loc="upper right", fontsize=10, facecolor=SURFACE, edgecolor=GRIDLINE)
    ax.grid(axis="y", color=GRIDLINE, linewidth=0.8, zorder=0)
    ax.set_axisbelow(True)
    for spine in ["top", "right"]:
        ax.spines[spine].set_visible(False)
    for spine in ["left", "bottom"]:
        ax.spines[spine].set_color(GRIDLINE)
    ax.tick_params(colors=INK_SECONDARY)
    fig.tight_layout()
    hist_path = out_dir / "histogram.png"
    fig.savefig(hist_path, dpi=200, facecolor=SURFACE)
    plt.close(fig)
    print(f"  OK {hist_path}")

    # ── report.txt ────────────────────────────────────────────────────
    lines = [
        "=" * 60,
        "EKSPERYMENT 1.2 — Hot Execution Latency",
        "MagistralaCAN4 + esp_experiment_1_2.ino (ESP32 + MCP2515)",
        "=" * 60,
        f"Liczba probek: {len(deltas)} (cel: {n_trials})",
        "",
        "METODOLOGIA:",
        "  Pomiar WEWNETRZNY na ESP32 (esp_timer_get_time(), rozdzielczosc",
        "  mikrosekundowa) - czas miedzy przerwaniem sprzetowym MCP2515 INT",
        "  (nadejscie ramki CAN) a reakcja GPIO (przelaczenie pinu po",
        "  potwierdzeniu ID znanej ramki). Zewnetrzny analizator stanow",
        "  logicznych (ATK-Logic, USB 1a86:ffcc) okazal sie trwale niesprawny",
        "  (brak odpowiedzi na jakiekolwiek zapytanie, zweryfikowane",
        "  niezaleznym testem C++/libusb odtwarzajacym doslownie oficjalny",
        "  protokol producenta) - metodologia zastepcza, nie oscyloskopowa.",
        "",
        f"  Statystyki t_resp [us]:",
        f"    srednia (mu):      {mean_us:.2f}",
        f"    odchylenie (sigma): {stdev_us:.2f}",
        f"    mediana:            {median_us:.2f}",
        f"    min:                {min_us}",
        f"    max:                {max_us}",
        "",
        "WNIOSKI:",
        f"  Czas reakcji Hot Execution (~{mean_us:.0f}us) jest o ~4-5 rzedow",
        "  wielkosci mniejszy niz t_llm z Eksperymentu 1.1 (sekundy), co",
        "  potwierdza teze: koszt fazy adaptacji (Cold Start, zapytanie LLM)",
        "  dominuje calkowity czas systemu, a raz wdrozona regula dziala",
        "  praktycznie w czasie rzeczywistym.",
    ]
    report_path = out_dir / "report.txt"
    with open(report_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"  OK {report_path}")
    print("Gotowe.")


if __name__ == "__main__":
    main()
