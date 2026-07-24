#!/usr/bin/env python3
"""
Wizualizacja Eksperymentu 1.1 — wszystkie 4 modele LLM
Stacked bar chart + t_llm comparison + percentage breakdown
"""
import json, os, glob
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker
import numpy as np

# ── Wczytaj dane ze wszystkich JSON ──────────────────────────────────────────

def load_stats(json_path):
    with open(json_path) as f:
        data = json.load(f)
    if "statistics" in data and isinstance(data["statistics"], list):
        # format z run_experiment_1.1.py (3 modele)
        return [(s["model"], s["means"], s["sigmas"]) for s in data["statistics"]]
    elif "statistics" in data:
        # format z run_gemini.py (1 model)
        s = data["statistics"]
        means = {k.replace("Ms",""): v["meanMs"] for k,v in s.items()}
        sigmas = {k.replace("Ms",""): v["sigmaMs"] for k,v in s.items()}
        return [(data["model"], means, sigmas)]
    return []

all_stats = []
for f in sorted(glob.glob("experiment_1.1_results_*.json")):
    all_stats.extend(load_stats(f))
for f in sorted(glob.glob("experiment_1.1_gemini_results_*.json")):
    all_stats.extend(load_stats(f))

if not all_stats:
    print("ERROR: No result files found!")
    exit(1)

# Usuń duplikaty (ten sam model z wielu plików — weź ostatni)
seen = {}
unique = []
for model, means, sigmas in all_stats:
    seen[model] = (model, means, sigmas)
all_stats = list(seen.values())

# Sortuj wg t_llm (rosnąco — najszybszy pierwszy)
all_stats.sort(key=lambda x: x[1].get("tLlm", x[1].get("t_llm", 9999)))

models = [s[0] for s in all_stats]
components = ["t_det", "t_tx_up", "t_llm", "t_comp", "t_ota"]
labels_pl = {
    "t_det": "t_det (detekcja)",
    "t_tx_up": "t_tx_up (transmisja)",
    "t_llm": "t_llm (wnioskowanie LLM)",
    "t_comp": "t_comp (kompilacja)",
    "t_ota": "t_ota (OTA update)",
}

# ── Rysuj ────────────────────────────────────────────────────────────────────

colors = ['#2ecc71', '#3498db', '#e74c3c', '#f39c12', '#9b59b6']
fig = plt.figure(figsize=(20, 10))

# ═══════════════════════════════════════════════════════════════════════════
# WYKRES 1: Stacked bar chart — skumulowane składowe T_total
# ═══════════════════════════════════════════════════════════════════════════

ax1 = fig.add_subplot(2, 3, (1, 3))  # górny rząd, zajmuje 3 kolumny

x = np.arange(len(models))
width = 0.55
bottom = np.zeros(len(models))

for i, comp in enumerate(components):
    values = []
    for model, means, sigmas in all_stats:
        key = comp if comp in means else f"t{comp.capitalize()}" if f"t{comp.capitalize()}" in means else 0
        v = means.get(comp, means.get(f"t{comp.capitalize()}", 0))
        values.append(v)
    
    bars = ax1.bar(x, values, width, bottom=bottom, color=colors[i],
                   label=labels_pl[comp], edgecolor='white', linewidth=0.8)
    
    # Etykiety wartości (tylko dla t_llm — największy komponent)
    if comp == "t_llm":
        for bar, val in zip(bars, values):
            ax1.text(bar.get_x() + bar.get_width()/2., bar.get_y() + bar.get_height()/2.,
                    f'{val:.0f} ms', ha='center', va='center', fontsize=9,
                    fontweight='bold', color='white')
    
    bottom += np.array(values)

# Suma T_total nad słupkami
for i, (model, means, _) in enumerate(all_stats):
    total = sum(means.get(c, 0) for c in components)
    ax1.text(i, bottom[i] + max(bottom)*0.015, f'{total:.0f} ms',
            ha='center', va='bottom', fontsize=11, fontweight='bold', color='#2c3e50')

ax1.set_ylabel('Czas [ms]', fontsize=12, fontweight='bold')
ax1.set_title('Eksperyment 1.1: Cold Start Latency Breakdown — Skumulowane T_total', 
              fontsize=15, fontweight='bold', pad=15)
ax1.set_xticks(x)
ax1.set_xticklabels(models, fontsize=12, fontweight='bold')
ax1.legend(loc='upper left', fontsize=9, framealpha=0.95)
ax1.grid(axis='y', alpha=0.3, linestyle='--')
ax1.set_ylim(0, max(bottom) * 1.12)

# ═══════════════════════════════════════════════════════════════════════════
# WYKRES 2: Porównanie t_llm (sam czas wnioskowania)
# ═══════════════════════════════════════════════════════════════════════════

ax2 = fig.add_subplot(2, 3, 4)

llm_means = []
llm_sigmas = []
for model, means, sigmas in all_stats:
    lm = means.get("t_llm", means.get("tLlm", 0))
    ls = sigmas.get("t_llm", sigmas.get("tLlm", 0))
    llm_means.append(lm)
    llm_sigmas.append(ls)

bars = ax2.bar(range(len(models)), llm_means, color=['#27ae60', '#2ecc71', '#f39c12', '#e74c3c'][:len(models)],
               edgecolor='white', linewidth=1.2)

# Słupki błędu (σ)
ax2.errorbar(range(len(models)), llm_means, yerr=llm_sigmas, fmt='none',
             ecolor='#34495e', capsize=8, capthick=2, linewidth=1.5)

for bar, val, sigma in zip(bars, llm_means, llm_sigmas):
    ax2.text(bar.get_x() + bar.get_width()/2., bar.get_height() + sigma + max(llm_means)*0.02,
            f'{val:.0f}±{sigma:.0f} ms', ha='center', va='bottom', fontsize=10, fontweight='bold')

ax2.set_ylabel('Czas [ms]', fontsize=11, fontweight='bold')
ax2.set_title('Porównanie t_llm (czas wnioskowania)', fontsize=13, fontweight='bold')
ax2.set_xticks(range(len(models)))
ax2.set_xticklabels(models, fontsize=10, fontweight='bold')
ax2.grid(axis='y', alpha=0.3, linestyle='--')

# ═══════════════════════════════════════════════════════════════════════════
# WYKRES 3: Procentowy udział składowych (grouped bar)
# ═══════════════════════════════════════════════════════════════════════════

ax3 = fig.add_subplot(2, 3, 5)

x3 = np.arange(len(components))
width3 = 0.18

for i, (model, means, _) in enumerate(all_stats):
    total = sum(means.get(c, 0) for c in components)
    pcts = [means.get(c, 0) / total * 100 for c in components] if total > 0 else [0]*5
    bars = ax3.bar(x3 + i*width3, pcts, width3, color=colors, edgecolor='white', linewidth=0.5, alpha=0.88)
    
    for j, (bar, pct) in enumerate(zip(bars, pcts)):
        if pct > 3:
            ax3.text(bar.get_x() + bar.get_width()/2., bar.get_height() + 0.3,
                    f'{pct:.1f}%', ha='center', va='bottom', fontsize=6.5,
                    color=colors[j], fontweight='bold', rotation=90)

ax3.set_ylabel('Udział w T_total [%]', fontsize=11, fontweight='bold')
ax3.set_title('Procentowy udział składowych', fontsize=13, fontweight='bold')
ax3.set_xticks(x3 + width3 * 1.5)
ax3.set_xticklabels([labels_pl[c].split('(')[0].strip() for c in components], 
                     rotation=20, ha='right', fontsize=9)
ax3.legend(models, loc='upper right', fontsize=9, framealpha=0.9)
ax3.grid(axis='y', alpha=0.3, linestyle='--')

# ═══════════════════════════════════════════════════════════════════════════
# WYKRES 4: Tabela tekstowa + wnioski
# ═══════════════════════════════════════════════════════════════════════════

ax4 = fig.add_subplot(2, 3, 6)
ax4.axis('off')

lines = []
lines.append("WNIOSKI — Eksperyment 1.1")
lines.append("═" * 35)
lines.append("")
lines.append("Ranking t_llm (szybkość wnioskowania):")
for rank, (model, means, _) in enumerate(all_stats, 1):
    lm = means.get("t_llm", means.get("tLlm", 0))
    medal = ["🥇","🥈","🥉","4."][rank-1]
    lines.append(f"  {medal} {model}: {lm:.0f} ms")

lines.append("")
lines.append("Wnioski kluczowe:")
lines.append("  • t_llm dominuje: 92-97% T_total")
lines.append("  • Sprzęt (ESP32+RPi): ~95 ms stałe")
lines.append("  • Gemini 3× szybszy niż DeepSeek")
lines.append("  • Claude i GPT-4o — podobna półka")
lines.append("")
lines.append("Rekomendacja:")
lines.append("  Do CAN Edge AI → Gemini-2.0-Flash")
lines.append("  (najszybszy + darmowy tier 1500/dzień)")

text = '\n'.join(lines)
ax4.text(0.05, 0.95, text, transform=ax4.transAxes, fontsize=10,
         verticalalignment='top', fontfamily='monospace',
         bbox=dict(boxstyle='round', facecolor='#ecf0f1', alpha=0.9))

# ═══════════════════════════════════════════════════════════════════════════
# ZAPISZ
# ═══════════════════════════════════════════════════════════════════════════

plt.suptitle('MagistralaCAN4 — Eksperyment 1.1: Cold Start Latency Breakdown\n'
             'N=30 prób na model | 5 rzeczywistych zapytań API na model | łącznie 20 zapytań API',
             fontsize=11, y=1.01, fontweight='normal', color='#7f8c8d')

plt.tight_layout(pad=3)
output = "experiment_1.1_ALL_MODELS_chart.png"
plt.savefig(output, dpi=200, bbox_inches='tight', facecolor='white')
plt.close()

print(f"✓ Wizualizacja zapisana: {output}")
print(f"  Modele: {', '.join(models)}")
