#!/usr/bin/env python3
"""
Post-processing Eksperymentu 1.1 (Cold Start Latency Breakdown).

Wejscie: raporty JSON wygenerowane przez MagistralaCAN4 --run-experiment
(ExperimentRunner/LatencyProfiler, src/core/), NIE symulacja Python.

Generuje w katalogu zbiorczym:
  raw_data.csv        - wszystkie proby (per trial, per model)
  statistics.csv       - srednia/sigma per model
  input_data.csv        - reprezentatywne przyklady zapytan+odpowiedzi LLM
  report.txt              - podsumowanie tekstowe + wnioski
  chart_cold_start.png  - wykres slupkowy skumulowany + porownanie t_llm
"""
import sys
import json
import csv
import statistics as stats_mod
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

# Paleta (dataviz skill, referenced/palette.md) - kolejnosc kategorialna zwalidowana
COLORS = {
    "t_det_us":   "#2a78d6",  # slot 1 blue
    "t_tx_up_us": "#008300",  # slot 2 green
    "t_llm_us":   "#e87ba4",  # slot 3 magenta
    "t_comp_us":  "#eda100",  # slot 4 yellow
    "t_ota_us":   "#1baf7a",  # slot 5 aqua
}
COMP_ORDER = ["t_det_us", "t_tx_up_us", "t_llm_us", "t_comp_us", "t_ota_us"]
COMP_LABELS_PL = {
    "t_det_us": "detekcja (t_det)",
    "t_tx_up_us": "transmisja ESP32→serwer (t_tx_up)",
    "t_llm_us": "wnioskowanie LLM (t_llm)",
    "t_comp_us": "kompilacja reguly (t_comp)",
    "t_ota_us": "aktualizacja OTA (t_ota)",
}
INK = "#0b0b0b"
INK_SECONDARY = "#52514e"
INK_MUTED = "#898781"
GRIDLINE = "#e1e0d9"
SURFACE = "#fcfcfb"


def load_report(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def build_raw_and_stats(report, out_dir, model_order):
    samples = report.get("samples", [])
    trials_per_model = report.get("trialsPerModel", 30)

    # ── raw_data.csv ──────────────────────────────────────────────────────
    raw_path = out_dir / "raw_data.csv"
    with open(raw_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["Model", "Trial", "CAN_ID", "t_det_ms", "t_tx_up_ms",
                    "t_llm_ms", "t_comp_ms", "t_ota_ms", "t_total_ms",
                    "success", "error"])
        for s in samples:
            tdet = s.get("tDetUs", 0) / 1000.0
            ttx = s.get("tTxUpUs", 0) / 1000.0
            tllm = s.get("tLlmUs", 0) / 1000.0
            tcomp = s.get("tCompUs", 0) / 1000.0
            tota = s.get("tOtaUs", 0) / 1000.0
            ttotal = tdet + ttx + tllm + tcomp + tota
            w.writerow([s.get("model"), s.get("trialIndex"), s.get("canId"),
                        round(tdet, 3), round(ttx, 3), round(tllm, 3),
                        round(tcomp, 3), round(tota, 3), round(ttotal, 3),
                        s.get("success"), s.get("error", "")])
    print(f"  OK {raw_path}")

    # ── statistics.csv (tylko proby success=True do statystyk czasowych) ──
    per_model = {}
    for s in samples:
        if not s.get("success"):
            continue
        m = s.get("model")
        per_model.setdefault(m, []).append(s)

    stats_rows = []
    for m in model_order:
        rows = per_model.get(m, [])
        if not rows:
            stats_rows.append({"model": m, "n": 0})
            continue
        comp_vals = {c: [r.get(c, 0) / 1000.0 for r in rows] for c in
                     ["tDetUs", "tTxUpUs", "tLlmUs", "tCompUs", "tOtaUs"]}
        total_vals = [sum(comp_vals[c][i] for c in comp_vals) for i in range(len(rows))]
        row = {"model": m, "n": len(rows)}
        for key, comp_key in zip(COMP_ORDER,
                                  ["tDetUs", "tTxUpUs", "tLlmUs", "tCompUs", "tOtaUs"]):
            vals = comp_vals[comp_key]
            row[f"mean_{key}"] = stats_mod.mean(vals)
            row[f"sigma_{key}"] = stats_mod.stdev(vals) if len(vals) > 1 else 0.0
        row["mean_total"] = stats_mod.mean(total_vals)
        row["sigma_total"] = stats_mod.stdev(total_vals) if len(total_vals) > 1 else 0.0
        stats_rows.append(row)

    stats_path = out_dir / "statistics.csv"
    with open(stats_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        header = ["Model", "N_success"]
        for c in COMP_ORDER:
            header += [f"mean_{c}_ms", f"sigma_{c}_ms"]
        header += ["mean_total_ms", "sigma_total_ms"]
        w.writerow(header)
        for row in stats_rows:
            if row.get("n", 0) == 0:
                w.writerow([row["model"], 0] + ["N/A"] * (len(header) - 2))
                continue
            line = [row["model"], row["n"]]
            for c in COMP_ORDER:
                line += [round(row[f"mean_{c}"], 3), round(row[f"sigma_{c}"], 3)]
            line += [round(row["mean_total"], 3), round(row["sigma_total"], 3)]
            w.writerow(line)
    print(f"  OK {stats_path}")

    return stats_rows, trials_per_model


def build_input_data(examples_report, out_dir):
    """input_data.csv - reprezentatywne zapytania (ramka CAN) + odpowiedzi LLM."""
    if examples_report is None:
        return False
    samples = examples_report.get("samples", [])
    path = out_dir / "input_data.csv"
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["Model", "Trial", "CAN_ID", "Frame_Data_Hex",
                    "LLM_Response", "t_llm_ms", "success"])
        for s in samples:
            resp = (s.get("llmResponseText") or "")[:500]
            w.writerow([s.get("model"), s.get("trialIndex"), s.get("canId"),
                        s.get("frameDataHex", ""), resp,
                        round(s.get("tLlmUs", 0) / 1000.0, 1), s.get("success")])
    print(f"  OK {path}")
    return True


def build_chart(stats_rows, out_dir, trials_per_model, metadata):
    valid_rows = [r for r in stats_rows if r.get("n", 0) > 0]
    valid_rows.sort(key=lambda r: r["mean_t_llm_us"])

    fig = plt.figure(figsize=(17, 10), facecolor=SURFACE)
    gs = fig.add_gridspec(2, 3, height_ratios=[2.2, 1], hspace=0.45, wspace=0.35)

    # ── Wykres 1: stacked bar skumulowany (Format prezentacji z metodyki) ──
    ax1 = fig.add_subplot(gs[0, :3])
    ax1.set_facecolor(SURFACE)
    model_names = [r["model"] for r in valid_rows]
    x = np.arange(len(model_names))
    bottom = np.zeros(len(model_names))

    for comp in COMP_ORDER:
        vals = np.array([r[f"mean_{comp}"] for r in valid_rows])
        bars = ax1.bar(x, vals, 0.5, bottom=bottom, color=COLORS[comp],
                        edgecolor=SURFACE, linewidth=2, label=COMP_LABELS_PL[comp])
        if comp == "t_llm_us":
            for bar, val in zip(bars, vals):
                ax1.text(bar.get_x() + bar.get_width() / 2, bar.get_y() + bar.get_height() / 2,
                         f"{val:.0f} ms", ha="center", va="center", fontsize=9,
                         fontweight="bold", color="white")
        bottom += vals

    for i, r in enumerate(valid_rows):
        ax1.text(i, bottom[i] + max(bottom) * 0.015, f"{r['mean_total']:.0f} ms",
                  ha="center", va="bottom", fontsize=11, fontweight="bold", color=INK)

    ax1.set_ylabel("Czas [ms]", fontsize=11, color=INK_SECONDARY)
    ax1.set_title(
        "Eksperyment 1.1 — Cold Start Latency Breakdown "
        "(MagistralaCAN4, N={} prob/model, realne API)".format(trials_per_model),
        fontsize=13, fontweight="bold", color=INK, pad=14)
    ax1.set_xticks(x)
    ax1.set_xticklabels(model_names, fontsize=10, color=INK)
    ax1.legend(loc="upper left", fontsize=8.5, framealpha=0.95, facecolor=SURFACE,
               edgecolor=GRIDLINE)
    ax1.grid(axis="y", color=GRIDLINE, linewidth=0.8, zorder=0)
    ax1.set_axisbelow(True)
    for spine in ["top", "right"]:
        ax1.spines[spine].set_visible(False)
    for spine in ["left", "bottom"]:
        ax1.spines[spine].set_color(GRIDLINE)
    ax1.tick_params(colors=INK_MUTED)

    # ── Wykres 2b: skladowe poza t_llm (skala logarytmiczna) ─────────────
    # t_llm dominuje >98% T_total, wiec na skali liniowej stosu (wykres 1)
    # pozostale skladowe sa niewidoczne — pokazujemy je osobno w powiekszeniu.
    ax1b = fig.add_subplot(gs[1, 0])
    ax1b.set_facecolor(SURFACE)
    small_comps = ["t_det_us", "t_tx_up_us", "t_comp_us", "t_ota_us"]
    n_models = len(model_names)
    xw = np.arange(n_models)
    bar_w = 0.19
    for i, comp in enumerate(small_comps):
        vals = [max(r[f"mean_{comp}"], 1e-3) for r in valid_rows]
        ax1b.bar(xw + (i - 1.5) * bar_w, vals, bar_w, color=COLORS[comp],
                  edgecolor=SURFACE, linewidth=0.6, label=COMP_LABELS_PL[comp])
    ax1b.set_yscale("log")
    ax1b.set_ylabel("Czas [ms] (log)", fontsize=9, color=INK_SECONDARY)
    ax1b.set_title("Skladowe poza t_llm (powiekszenie, skala log)", fontsize=10.5, color=INK)
    ax1b.set_xticks(xw)
    ax1b.set_xticklabels(model_names, fontsize=8, color=INK, rotation=12, ha="right")
    ax1b.legend(loc="upper right", fontsize=6.5, framealpha=0.95, facecolor=SURFACE,
                edgecolor=GRIDLINE, ncol=2)
    ax1b.grid(axis="y", color=GRIDLINE, linewidth=0.7, which="both", zorder=0)
    ax1b.set_axisbelow(True)
    for spine in ["top", "right"]:
        ax1b.spines[spine].set_visible(False)
    for spine in ["left", "bottom"]:
        ax1b.spines[spine].set_color(GRIDLINE)
    ax1b.tick_params(colors=INK_MUTED, labelsize=7.5)

    # ── Wykres 2: porownanie t_llm (bar + sigma) ─────────────────────────
    ax2 = fig.add_subplot(gs[1, 1])
    ax2.set_facecolor(SURFACE)
    llm_vals = [r["mean_t_llm_us"] for r in valid_rows]
    llm_sig = [r["sigma_t_llm_us"] for r in valid_rows]
    bars = ax2.bar(range(len(model_names)), llm_vals, color=COLORS["t_llm_us"],
                    edgecolor=SURFACE, linewidth=1.2)
    ax2.errorbar(range(len(model_names)), llm_vals, yerr=llm_sig, fmt="none",
                 ecolor=INK_SECONDARY, capsize=6, capthick=1.5, linewidth=1.2)
    for bar, val, sig in zip(bars, llm_vals, llm_sig):
        ax2.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + sig + max(llm_vals) * 0.02,
                  f"{val:.0f}±{sig:.0f} ms", ha="center", va="bottom", fontsize=8.5,
                  fontweight="bold", color=INK)
    ax2.set_ylabel("t_llm [ms]", fontsize=10, color=INK_SECONDARY)
    ax2.set_title("Porownanie t_llm (μ ± σ)", fontsize=11, color=INK)
    ax2.set_xticks(range(len(model_names)))
    ax2.set_xticklabels(model_names, fontsize=8.5, color=INK, rotation=12, ha="right")
    ax2.grid(axis="y", color=GRIDLINE, linewidth=0.8, zorder=0)
    ax2.set_axisbelow(True)
    for spine in ["top", "right"]:
        ax2.spines[spine].set_visible(False)
    for spine in ["left", "bottom"]:
        ax2.spines[spine].set_color(GRIDLINE)
    ax2.tick_params(colors=INK_MUTED)

    # ── Panel wnioskow + adnotacja o symulacji sprzetowej ────────────────
    ax3 = fig.add_subplot(gs[1, 2])
    ax3.axis("off")
    lines = ["WNIOSKI", "─" * 30]
    for rank, r in enumerate(valid_rows, 1):
        lines.append(f"{rank}. {r['model']}: t_llm={r['mean_t_llm_us']:.0f} ms "
                      f"(N={r['n']})")
    lines.append("")
    if valid_rows:
        lines.append(f"Najszybszy: {valid_rows[0]['model']}")
        lines.append(f"Najwolniejszy: {valid_rows[-1]['model']}")
    failed = [r["model"] for r in stats_rows if r.get("n", 0) == 0]
    if failed:
        lines.append("")
        lines.append("Pominiete (blad API): " + ", ".join(failed))
    if metadata.get("hardwareSimulated"):
        lines.append("")
        lines.append("Uwaga: t_det/t_tx_up/t_ota symulowane")
        lines.append("(brak fiz. ESP32) — t_llm/t_comp realne.")
    ax3.text(0.0, 0.98, "\n".join(lines), transform=ax3.transAxes, fontsize=7.8,
              va="top", ha="left", color=INK, fontfamily="monospace",
              bbox=dict(boxstyle="round", facecolor="#f2f2ef", edgecolor=GRIDLINE))

    chart_path = out_dir / "chart_cold_start.png"
    fig.savefig(chart_path, dpi=200, bbox_inches="tight", facecolor=SURFACE)
    plt.close(fig)
    print(f"  OK {chart_path}")
    return chart_path


def build_text_report(stats_rows, out_dir, trials_per_model, metadata, model_details):
    valid_rows = sorted([r for r in stats_rows if r.get("n", 0) > 0],
                         key=lambda r: r["mean_t_llm_us"])
    lines = [
        "=" * 70,
        "EKSPERYMENT 1.1 — Cold Start Latency Breakdown",
        "MagistralaCAN4 (realna infrastruktura C++/Qt6: ColdStartDetector,",
        "LlmQueryClient, LatencyProfiler, ExperimentRunner)",
        "=" * 70,
        f"Prob na model: {trials_per_model}",
        "",
        "Modele testowane:",
    ]
    for name, mid in model_details.items():
        lines.append(f"  - {name} (model_id={mid})")
    lines += ["", f"{'Model':<20}{'N':>5}{'t_llm [ms]':>18}{'T_total [ms]':>18}", "-" * 61]
    for r in valid_rows:
        lines.append(f"{r['model']:<20}{r['n']:>5}"
                      f"{r['mean_t_llm_us']:>10.0f}±{r['sigma_t_llm_us']:<6.0f}"
                      f"{r['mean_total']:>10.0f}±{r['sigma_total']:<6.0f}")
    failed = [r["model"] for r in stats_rows if r.get("n", 0) == 0]
    if failed:
        lines += ["", f"Pominiete z powodu bledow API: {', '.join(failed)}"]

    lines += ["", "WNIOSKI:"]
    if valid_rows:
        lines.append(f"  Najszybszy model (t_llm): {valid_rows[0]['model']} "
                      f"({valid_rows[0]['mean_t_llm_us']:.0f} ms)")
        lines.append(f"  Najwolniejszy model (t_llm): {valid_rows[-1]['model']} "
                      f"({valid_rows[-1]['mean_t_llm_us']:.0f} ms)")
        dominant = all(r["mean_t_llm_us"] / r["mean_total"] > 0.5 for r in valid_rows if r["mean_total"] > 0)
        if dominant:
            lines.append("  t_llm dominuje T_total dla wszystkich modeli (>50%),")
            lines.append("  co potwierdza teze, ze czas wnioskowania LLM jest")
            lines.append("  glownym waskim gardlem fazy Cold Start.")

    if metadata.get("hardwareSimulated"):
        lines += ["", "METODOLOGIA / OGRANICZENIA:",
                  "  Brak fizycznego ESP32 i magistrali CAN w srodowisku pomiarowym.",
                  "  t_llm i t_comp sa realnymi pomiarami (zegar systemowy, rzeczywiste",
                  "  zapytania HTTP do API dostawcow LLM przez Qt QNetworkAccessManager).",
                  "  t_det, t_tx_up i t_ota sa symulowane rozkladem normalnym opartym",
                  "  o wartosci literaturowe dla ESP32+WiFi (patrz kod ExperimentRunner",
                  "  ::onColdStartDetected / ::onLlmResponse) - NIE sa to realne pomiary",
                  "  sprzetowe i wymagaja walidacji na fizycznym stanowisku przed obrona."]

    report_path = out_dir / "report.txt"
    with open(report_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"  OK {report_path}")


def main():
    if len(sys.argv) < 3:
        print("Uzycie: process_experiment_1_1.py <katalog_zbiorczy> <raw/latency_report_full.json> [raw/latency_report_examples.json]")
        sys.exit(1)

    out_dir = Path(sys.argv[1])
    stats_json_path = Path(sys.argv[2])
    examples_json_path = Path(sys.argv[3]) if len(sys.argv) > 3 else None

    out_dir.mkdir(parents=True, exist_ok=True)

    report = load_report(stats_json_path)
    metadata = report.get("metadata", {})
    model_order = metadata.get("modelsTestedInOrder", [])
    if not model_order:
        model_order = sorted({s.get("model") for s in report.get("samples", [])})

    model_details = {m: m for m in model_order}

    print("Przetwarzanie danych...")
    stats_rows, trials_per_model = build_raw_and_stats(report, out_dir, model_order)

    examples_report = load_report(examples_json_path) if (examples_json_path and examples_json_path.exists()) else None
    build_input_data(examples_report, out_dir)

    build_text_report(stats_rows, out_dir, trials_per_model, metadata, model_details)
    build_chart(stats_rows, out_dir, trials_per_model, metadata)
    print("Gotowe.")


if __name__ == "__main__":
    main()
