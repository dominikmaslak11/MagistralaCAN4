#!/usr/bin/env python3
"""
Eksperyment 1.2 - Hot Execution Latency, pomiar REALNYM OSCYLOSKOPEM (Hantek 1008C).

Roznica wzgledem run_experiment_1_2.py (metoda zapasowa, timer wewnetrzny ESP32):
tutaj czas reakcji mierzony jest NIEZALEZNIE od ESP32, oscyloskopem, na podstawie
dwoch sond:
  CH1 (channel index 0) -> ESP32 GPIO4  (MCP_INT_PIN, przerwanie MCP2515, FALLING)
  CH2 (channel index 1) -> ESP32 GPIO13 (REACTION_PIN, RISING po wykryciu ramki)
t_resp = (indeks probki CH2 rising) - (indeks probki CH1 falling), razy okres probki.

Tryb burst + wyzwalacz sprzetowy (trigger na CH1 falling), okno 400us,
rozdzielczosc 200ns/probke - z duzym zapasem wobec t_resp ~110us (zmierzone
wczesniej metoda ESP32-timer).

Generuje w katalogu wyjsciowym: raw_data.csv, statistics.csv, report.txt,
histogram.png - ten sam format co run_experiment_1_2.py.
"""
import sys
import csv
import subprocess
import threading
import time
import statistics as stats_mod
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "hantek1008_driver"))
from hantek1008 import Hantek1008Raw  # noqa: E402

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

CH_CAN_RX = 0       # "CH1" na obudowie -> ESP32 GPIO4 (MCP_INT_PIN)
CH_GPIO_RESP = 1    # "CH2" na obudowie -> ESP32 GPIO13 (REACTION_PIN)
NS_PER_DIV = 5000   # 5us/div * 80div = 400us okno, 200ns/probke
SAMPLES_PER_DIV = 25
SAMPLE_PERIOD_US = (NS_PER_DIV / SAMPLES_PER_DIV) / 1000.0

TRIGGER_LEVEL = 2048
CAN_IFACE = "can0"
TRIGGER_FRAME = "123#DEADBEEF11223344"
SEND_DELAY_S = 0.05      # opoznienie wyslania ramki wzgledem uzbrojenia wyzwalacza
FRAME_INTERVAL_S = 0.05  # przerwa miedzy probami


def find_edges(values, threshold, edge):
    edges = []
    for i in range(1, len(values)):
        if edge == "falling" and values[i - 1] >= threshold > values[i]:
            edges.append(i)
        elif edge == "rising" and values[i - 1] < threshold <= values[i]:
            edges.append(i)
    return edges


def run_trial(dev):
    def send_frame_delayed():
        time.sleep(SEND_DELAY_S)
        subprocess.run(["cansend", CAN_IFACE, TRIGGER_FRAME], check=False)

    t = threading.Thread(target=send_frame_delayed)
    t.start()
    data = dev.request_samples_burst_mode()
    t.join()

    ch0 = data[CH_CAN_RX]
    ch1 = data[CH_GPIO_RESP]
    mid0 = (min(ch0) + max(ch0)) / 2
    mid1 = (min(ch1) + max(ch1)) / 2
    ch0_falling = find_edges(ch0, mid0, "falling")
    ch1_rising = find_edges(ch1, mid1, "rising")

    if not ch0_falling or not ch1_rising:
        return None
    d_samples = ch1_rising[0] - ch0_falling[0]
    if d_samples <= 0:
        return None
    return d_samples * SAMPLE_PERIOD_US


def main():
    if len(sys.argv) < 3:
        print("Uzycie: run_experiment_1_2_hantek.py <katalog_wyjsciowy> <N_probek>")
        sys.exit(1)
    out_dir = Path(sys.argv[1])
    n_trials = int(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)

    dev = Hantek1008Raw(ns_per_div=NS_PER_DIV, active_channels=[CH_CAN_RX, CH_GPIO_RESP],
                         trigger_channel=CH_CAN_RX, trigger_slope="falling",
                         trigger_level=TRIGGER_LEVEL)
    dev.connect()
    dev.init()

    samples = []  # (trial_idx, t_resp_us)
    failures = 0
    print(f"Startuje Eksperyment 1.2 (Hantek 1008C, oscyloskop) - N={n_trials} probek...")
    try:
        for i in range(n_trials):
            t_resp_us = run_trial(dev)
            if t_resp_us is not None:
                samples.append((i + 1, t_resp_us))
            else:
                failures += 1
                print(f"  [UWAGA] Brak wykrytych zboczy w probie {i+1}")

            if (i + 1) % 100 == 0:
                print(f"  {i+1}/{n_trials} probek ({len(samples)} OK, {failures} pominiete)...")

            time.sleep(FRAME_INTERVAL_S)
    finally:
        dev.close()

    print(f"Zebrano {len(samples)}/{n_trials} probek ({failures} pominietych).")

    raw_path = out_dir / "raw_data.csv"
    with open(raw_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["Trial", "t_resp_us"])
        for trial_idx, delta_us in samples:
            w.writerow([trial_idx, round(delta_us, 3)])
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

    stats_path = out_dir / "statistics.csv"
    with open(stats_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["N", "mean_us", "stdev_us", "median_us", "min_us", "max_us"])
        w.writerow([len(deltas), round(mean_us, 2), round(stdev_us, 2),
                    round(median_us, 2), round(min_us, 2), round(max_us, 2)])
    print(f"  OK {stats_path}")

    COLOR_HIST = "#2a78d6"
    INK = "#0b0b0b"
    INK_SECONDARY = "#52514e"
    GRIDLINE = "#e1e0d9"
    SURFACE = "#fcfcfb"

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
        f"Eksperyment 1.2 - Hot Execution Latency, pomiar oscyloskopem (N={len(deltas)})\n"
        f"μ={mean_us:.2f}μs, σ={stdev_us:.2f}μs, min={min_us:.2f}μs, max={max_us:.2f}μs",
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

    lines = [
        "=" * 60,
        "EKSPERYMENT 1.2 - Hot Execution Latency (pomiar oscyloskopem)",
        "MagistralaCAN4 + esp_experiment_1_2.ino (ESP32 + MCP2515) + Hantek 1008C",
        "=" * 60,
        f"Liczba probek: {len(deltas)} (cel: {n_trials}, pominietych: {failures})",
        "",
        "METODOLOGIA:",
        "  Pomiar ZEWNETRZNY, niezalezny od ESP32: oscyloskop Hantek 1008C,",
        "  tryb burst + wyzwalacz sprzetowy (CH1 falling = przerwanie MCP2515",
        f"  INT), okno {NS_PER_DIV * 80 / 1000:.0f}us, rozdzielczosc {SAMPLE_PERIOD_US*1000:.0f}ns/probke.",
        "  CH1 (GPIO4/MCP_INT_PIN) = moment nadejscia ramki CAN (zbocze opadajace),",
        "  CH2 (GPIO13/REACTION_PIN) = moment reakcji ESP32 (zbocze rosnace).",
        "  t_resp = czas miedzy tymi dwoma zboczami, mierzony bezposrednio przez",
        "  sprzet pomiarowy - nie przez timer wewnetrzny ESP32 (metoda zapasowa",
        "  z poprzedniej sesji, patrz Eksperyment_1.2_Hot_Execution_20260725_163438/).",
        "",
        f"  Statystyki t_resp [us]:",
        f"    srednia (mu):      {mean_us:.2f}",
        f"    odchylenie (sigma): {stdev_us:.2f}",
        f"    mediana:            {median_us:.2f}",
        f"    min:                {min_us:.2f}",
        f"    max:                {max_us:.2f}",
        "",
        "WNIOSKI:",
        f"  Czas reakcji Hot Execution (~{mean_us:.0f}us), zmierzony niezaleznie",
        "  oscyloskopem, jest o ~4-5 rzedow wielkosci mniejszy niz t_llm z",
        "  Eksperymentu 1.1 (sekundy), co potwierdza teze: koszt fazy adaptacji",
        "  (Cold Start, zapytanie LLM) dominuje calkowity czas systemu, a raz",
        "  wdrozona regula dziala praktycznie w czasie rzeczywistym.",
    ]
    report_path = out_dir / "report.txt"
    with open(report_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"  OK {report_path}")
    print("Gotowe.")


if __name__ == "__main__":
    main()
