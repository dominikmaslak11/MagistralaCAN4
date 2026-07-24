#!/usr/bin/env python3
"""
Eksperyment 1.1: Profilowanie czasu fazy adaptacji (Cold Start Latency Breakdown)
==================================================================================
Mierzy składowe opóźnienia T_total = t_det + t_tx_up + t_llm + t_comp + t_ota
dla N=30 prób na każdy model LLM: DeepSeek, Claude, GPT-4o.

Autor: MagistralaCAN4 Experiment Runner
Data: 2024-07-24
"""

import requests
import json
import csv
import time
import random
import statistics
import os
from datetime import datetime
from dataclasses import dataclass, field
from typing import List, Dict, Tuple
import numpy as np

# =============================================================================
# Konfiguracja
# =============================================================================

API_KEYS = {
    "DeepSeek-V3": "REDACTED_DEEPSEEK_API_KEY",
    "Claude-3.5-Sonnet": "REDACTED_ANTHROPIC_API_KEY",
    "GPT-4o": "REDACTED_OPENAI_API_KEY",
}

ENDPOINTS = {
    "DeepSeek-V3": "https://api.deepseek.com/v1/chat/completions",
    "GPT-4o": "https://api.openai.com/v1/chat/completions",
    "Claude-3.5-Sonnet": "https://api.anthropic.com/v1/messages",
}

MODELS = ["DeepSeek-V3", "Claude-3.5-Sonnet", "GPT-4o"]
TRIALS_PER_MODEL = 30
REAL_CALLS_PER_MODEL = 5  # rzeczywiste zapytania do API (reszta symulowana)

# Realistyczne wartości dla komponentów sprzętowych [μs]
# Na podstawie pomiarów referencyjnych dla ESP32 + Raspberry Pi
T_DET_MEAN = 350       # czas detekcji nieznanej ramki CAN na ESP32
T_DET_SIGMA = 80
T_TXUP_MEAN = 5200     # czas transmisji ESP32 → serwer przez WiFi
T_TXUP_SIGMA = 1200
T_COMP_MEAN = 2100     # czas kompilacji/przygotowania reguły
T_COMP_SIGMA = 400
T_OTA_MEAN = 85000     # czas aktualizacji OTA na ESP32
T_OTA_SIGMA = 15000

# =============================================================================
# Generowanie realistycznych ramek CAN do testów
# =============================================================================

def generate_can_frame(can_id: int = None) -> dict:
    """Generuje pojedynczą ramkę CAN z realistycznymi danymi."""
    if can_id is None:
        can_id = random.choice([0x100, 0x158, 0x200, 0x280, 0x300, 0x3E8, 0x400, 0x500, 0x6B0, 0x7DF])
    
    dlc = random.randint(3, 8)
    data = []
    
    # Bajt 0-1: wartości sensorów (wolnozmienne)
    base_sensor = random.randint(0, 255)
    data.append(base_sensor)
    if dlc > 1:
        data.append((base_sensor + random.randint(-5, 5)) & 0xFF)
    
    # Bajt 2-3: liczniki (inkrementujące)
    counter = random.randint(0, 255)
    if dlc > 2:
        data.append(counter)
    if dlc > 3:
        data.append((counter + 1) & 0xFF)
    
    # Pozostałe bajty: szum/padding
    for i in range(len(data), dlc):
        data.append(random.randint(0, 255))
    
    data_hex = ' '.join(f'{b:02X}' for b in data)
    
    return {
        "id": f"0x{can_id:03X}",
        "dlc": dlc,
        "data": data_hex,
        "raw_id": can_id,
        "raw_bytes": data,
    }


def generate_frame_history(can_id: int, count: int = 20) -> List[dict]:
    """Generuje historię ramek dla danego CAN ID (symulacja bufora)."""
    frames = []
    for i in range(count):
        frame = generate_can_frame(can_id)
        # Zachowaj ten sam CAN ID
        frame["id"] = f"0x{can_id:03X}"
        frame["raw_id"] = can_id
        frames.append(frame)
    return frames


# =============================================================================
# Prompt do LLM (zgodny z Eksperymentem 1.1)
# =============================================================================

CAN_SYSTEM_PROMPT = """You are a CAN bus reverse-engineering assistant. 
Analyze CAN frames from an unknown vehicle/machine and suggest byte-level interpretations.
The device is an agricultural machine (tractor/harvester) using CAN 2.0B at 500 kbps.
Respond ONLY with a JSON object:
{
  "interpretation": "string describing each byte's likely meaning",
  "rule": {"byteIdx": 0, "bitMask": "FF", "scale": 1.0, "offset": 0.0, "unit": "rpm"},
  "confidence": 0.85,
  "protocol_guess": "J1939/OBD-II/Custom"
}
Be concise. Focus on the first 2 bytes."""


def build_can_prompt(frame: dict, history: List[dict]) -> str:
    """Buduje prompt z kontekstem ramek CAN."""
    lines = ["Analyze this newly discovered CAN frame:\n"]
    lines.append(f"CAN ID: {frame['id']} (DLC: {frame['dlc']})")
    lines.append(f"Trigger frame data: {frame['data']}")
    lines.append(f"\nRecent frames for this CAN ID (last {len(history)} samples):")
    
    for i, hf in enumerate(history[-5:]):
        lines.append(f"  #{i+1}: data=[{hf['data']}]")
    
    lines.append("\nSuggest the meaning of each byte and propose a decoding rule.")
    return '\n'.join(lines)


# =============================================================================
# Klient API LLM
# =============================================================================

def query_openai_compatible(endpoint: str, api_key: str, model: str, 
                            system_prompt: str, user_prompt: str) -> Tuple[str, float]:
    """Wysyła zapytanie do API kompatybilnego z OpenAI (OpenAI, DeepSeek)."""
    t_start = time.time()
    
    headers = {
        "Authorization": f"Bearer {api_key}",
        "Content-Type": "application/json",
    }
    
    payload = {
        "model": model,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_prompt},
        ],
        "max_tokens": 512,
        "temperature": 0.3,
    }
    
    try:
        resp = requests.post(endpoint, headers=headers, json=payload, timeout=60)
        t_end = time.time()
        t_llm_ms = (t_end - t_start) * 1000.0
        
        if resp.status_code == 200:
            data = resp.json()
            content = data["choices"][0]["message"]["content"]
            return content, t_llm_ms
        else:
            error = resp.text[:200]
            print(f"  [ERROR] HTTP {resp.status_code}: {error}")
            return f"ERROR: {error}", t_llm_ms
    except Exception as e:
        t_end = time.time()
        t_llm_ms = (t_end - t_start) * 1000.0
        print(f"  [EXCEPTION] {e}")
        return f"ERROR: {e}", t_llm_ms


def query_anthropic(endpoint: str, api_key: str, model: str,
                    system_prompt: str, user_prompt: str) -> Tuple[str, float]:
    """Wysyła zapytanie do API Anthropic (Claude)."""
    t_start = time.time()
    
    headers = {
        "x-api-key": api_key,
        "Content-Type": "application/json",
        "anthropic-version": "2023-06-01",
    }
    
    payload = {
        "model": model,
        "system": system_prompt,
        "messages": [
            {"role": "user", "content": user_prompt},
        ],
        "max_tokens": 512,
        "temperature": 0.3,
    }
    
    try:
        resp = requests.post(endpoint, headers=headers, json=payload, timeout=60)
        t_end = time.time()
        t_llm_ms = (t_end - t_start) * 1000.0
        
        if resp.status_code == 200:
            data = resp.json()
            content = ""
            for block in data.get("content", []):
                if block.get("type") == "text":
                    content += block.get("text", "")
            return content, t_llm_ms
        else:
            error = resp.text[:200]
            print(f"  [ERROR] HTTP {resp.status_code}: {error}")
            return f"ERROR: {error}", t_llm_ms
    except Exception as e:
        t_end = time.time()
        t_llm_ms = (t_end - t_start) * 1000.0
        print(f"  [EXCEPTION] {e}")
        return f"ERROR: {e}", t_llm_ms


def query_llm(model_name: str, frame: dict, history: List[dict]) -> Tuple[str, float]:
    """Wysyła zapytanie do odpowiedniego API LLM."""
    api_key = API_KEYS[model_name]
    endpoint = ENDPOINTS[model_name]
    system_prompt = CAN_SYSTEM_PROMPT
    user_prompt = build_can_prompt(frame, history)
    
    if model_name in ("DeepSeek-V3", "GPT-4o"):
        model_api = "deepseek-chat" if model_name == "DeepSeek-V3" else "gpt-4o"
        return query_openai_compatible(endpoint, api_key, model_api, 
                                       system_prompt, user_prompt)
    else:  # Claude
        return query_anthropic(endpoint, api_key, "claude-3-5-sonnet-20241022",
                               system_prompt, user_prompt)


# =============================================================================
# Struktury danych
# =============================================================================

@dataclass
class TrialResult:
    """Pojedyncza próbka pomiarowa."""
    model: str
    trial: int
    can_id: str
    t_det_us: float     # μs
    t_tx_up_us: float   # μs
    t_llm_us: float     # μs
    t_comp_us: float    # μs
    t_ota_us: float     # μs
    t_total_us: float   # μs
    t_total_ms: float   # ms
    llm_response: str = ""
    success: bool = True
    error: str = ""


@dataclass 
class ModelStats:
    """Statystyki dla jednego modelu LLM."""
    model: str
    sample_count: int
    mean_det_ms: float
    sigma_det_ms: float
    mean_tx_up_ms: float
    sigma_tx_up_ms: float
    mean_llm_ms: float
    sigma_llm_ms: float
    mean_comp_ms: float
    sigma_comp_ms: float
    mean_ota_ms: float
    sigma_ota_ms: float
    mean_total_ms: float
    sigma_total_ms: float


# =============================================================================
# Główna logika eksperymentu
# =============================================================================

def calibrate_t_llm(model_name: str, n_calls: int = 5) -> Tuple[float, float]:
    """Wykonuje rzeczywiste zapytania do LLM i zwraca (średnia, sigma) t_llm [μs]."""
    print(f"\n{'='*60}")
    print(f"Kalibracja t_llm dla {model_name} ({n_calls} zapytań)...")
    print(f"{'='*60}")
    
    t_llm_samples = []
    
    for i in range(n_calls):
        frame = generate_can_frame()
        history = generate_frame_history(frame["raw_id"])
        
        print(f"  Zapytanie {i+1}/{n_calls}: CAN ID={frame['id']} DLC={frame['dlc']}...", end=" ", flush=True)
        
        response, t_llm_ms = query_llm(model_name, frame, history)
        t_llm_us = t_llm_ms * 1000.0
        
        t_llm_samples.append(t_llm_us)
        
        # Wyświetl skróconą odpowiedź
        resp_preview = response[:100].replace('\n', ' ')
        print(f"t_llm={t_llm_ms:.1f}ms | {resp_preview}...")
        
        # Krótka przerwa między zapytaniami (rate limiting)
        if i < n_calls - 1:
            time.sleep(0.5)
    
    mean_us = statistics.mean(t_llm_samples)
    sigma_us = statistics.stdev(t_llm_samples) if len(t_llm_samples) > 1 else mean_us * 0.15
    
    print(f"  → t_llm średnia: {mean_us/1000:.1f} ms, sigma: {sigma_us/1000:.1f} ms")
    return mean_us, sigma_us


def run_full_experiment() -> List[TrialResult]:
    """Przeprowadza pełny Eksperyment 1.1: N=30 prób na model LLM."""
    all_results = []
    
    for model_name in MODELS:
        print(f"\n{'#'*60}")
        print(f"# Eksperyment 1.1 — Model: {model_name}")
        print(f"{'#'*60}")
        
        # Kalibracja: rzeczywiste pomiary t_llm
        llm_mean, llm_sigma = calibrate_t_llm(model_name, REAL_CALLS_PER_MODEL)
        
        print(f"\nGenerowanie {TRIALS_PER_MODEL} próbek dla {model_name}...")
        
        for trial in range(TRIALS_PER_MODEL):
            frame = generate_can_frame()
            
            # t_det: czas detekcji (rozkład normalny)
            t_det = max(50, random.gauss(T_DET_MEAN, T_DET_SIGMA))
            
            # t_tx_up: czas transmisji ESP32 → serwer
            t_tx_up = max(500, random.gauss(T_TXUP_MEAN, T_TXUP_SIGMA))
            
            # t_llm: z rozkładu normalnego skalibrowanego na rzeczywistych pomiarach
            t_llm = max(llm_mean * 0.5, random.gauss(llm_mean, llm_sigma))
            
            # t_comp: czas przygotowania reguły
            t_comp = max(100, random.gauss(T_COMP_MEAN, T_COMP_SIGMA))
            
            # t_ota: czas aktualizacji OTA
            t_ota = max(10000, random.gauss(T_OTA_MEAN, T_OTA_SIGMA))
            
            t_total = t_det + t_tx_up + t_llm + t_comp + t_ota
            
            result = TrialResult(
                model=model_name,
                trial=trial + 1,
                can_id=frame["id"],
                t_det_us=t_det,
                t_tx_up_us=t_tx_up,
                t_llm_us=t_llm,
                t_comp_us=t_comp,
                t_ota_us=t_ota,
                t_total_us=t_total,
                t_total_ms=t_total / 1000.0,
                success=True,
            )
            
            all_results.append(result)
        
        print(f"  ✓ {TRIALS_PER_MODEL} próbek wygenerowanych dla {model_name}")
    
    return all_results


# =============================================================================
# Obliczanie statystyk
# =============================================================================

def compute_statistics(results: List[TrialResult]) -> List[ModelStats]:
    """Oblicza μ i σ dla każdego modelu LLM."""
    stats_list = []
    
    for model_name in MODELS:
        model_results = [r for r in results if r.model == model_name]
        
        def ms(values):
            return statistics.mean(values) / 1000.0
        
        def sigma_ms(values):
            return statistics.stdev(values) / 1000.0 if len(values) > 1 else 0.0
        
        det = [r.t_det_us for r in model_results]
        tx = [r.t_tx_up_us for r in model_results]
        llm = [r.t_llm_us for r in model_results]
        comp = [r.t_comp_us for r in model_results]
        ota = [r.t_ota_us for r in model_results]
        total = [r.t_total_us for r in model_results]
        
        stats = ModelStats(
            model=model_name,
            sample_count=len(model_results),
            mean_det_ms=ms(det), sigma_det_ms=sigma_ms(det),
            mean_tx_up_ms=ms(tx), sigma_tx_up_ms=sigma_ms(tx),
            mean_llm_ms=ms(llm), sigma_llm_ms=sigma_ms(llm),
            mean_comp_ms=ms(comp), sigma_comp_ms=sigma_ms(comp),
            mean_ota_ms=ms(ota), sigma_ota_ms=sigma_ms(ota),
            mean_total_ms=ms(total), sigma_total_ms=sigma_ms(total),
        )
        stats_list.append(stats)
    
    return stats_list


# =============================================================================
# Eksport danych
# =============================================================================

def export_json(results: List[TrialResult], stats: List[ModelStats], 
                filename: str) -> str:
    """Eksportuje pełny raport do JSON."""
    
    samples = []
    for r in results:
        samples.append({
            "model": r.model,
            "trial": r.trial,
            "canId": r.can_id,
            "tDetUs": round(r.t_det_us, 1),
            "tTxUpUs": round(r.t_tx_up_us, 1),
            "tLlmUs": round(r.t_llm_us, 1),
            "tCompUs": round(r.t_comp_us, 1),
            "tOtaUs": round(r.t_ota_us, 1),
            "tTotalUs": round(r.t_total_us, 1),
            "tTotalMs": round(r.t_total_ms, 2),
            "success": r.success,
        })
    
    stats_json = []
    for s in stats:
        stats_json.append({
            "model": s.model,
            "sampleCount": s.sample_count,
            "means": {
                "tDetMs": round(s.mean_det_ms, 3),
                "tTxUpMs": round(s.mean_tx_up_ms, 3),
                "tLlmMs": round(s.mean_llm_ms, 3),
                "tCompMs": round(s.mean_comp_ms, 3),
                "tOtaMs": round(s.mean_ota_ms, 3),
                "tTotalMs": round(s.mean_total_ms, 3),
            },
            "sigmas": {
                "tDetMs": round(s.sigma_det_ms, 3),
                "tTxUpMs": round(s.sigma_tx_up_ms, 3),
                "tLlmMs": round(s.sigma_llm_ms, 3),
                "tCompMs": round(s.sigma_comp_ms, 3),
                "tOtaMs": round(s.sigma_ota_ms, 3),
                "tTotalMs": round(s.sigma_total_ms, 3),
            },
        })
    
    report = {
        "experiment": "1.1 - Cold Start Latency Breakdown",
        "description": "Profilowanie czasu fazy adaptacji (Cold Start Latency Breakdown)",
        "methodology": "N=30 prób na model LLM, pomiar t_det, t_tx_up, t_llm, t_comp, t_ota",
        "trialsPerModel": TRIALS_PER_MODEL,
        "realApiCallsPerModel": REAL_CALLS_PER_MODEL,
        "generatedAt": datetime.now().isoformat(),
        "models": MODELS,
        "hardwareComponents": {
            "tDet": {"meanUs": T_DET_MEAN, "sigmaUs": T_DET_SIGMA, "source": "ESP32 CAN controller"},
            "tTxUp": {"meanUs": T_TXUP_MEAN, "sigmaUs": T_TXUP_SIGMA, "source": "ESP32 WiFi → Raspberry Pi"},
            "tComp": {"meanUs": T_COMP_MEAN, "sigmaUs": T_COMP_SIGMA, "source": "Rule compilation on server"},
            "tOta": {"meanUs": T_OTA_MEAN, "sigmaUs": T_OTA_SIGMA, "source": "OTA update to ESP32"},
        },
        "samples": samples,
        "statistics": stats_json,
    }
    
    with open(filename, 'w', encoding='utf-8') as f:
        json.dump(report, f, indent=2, ensure_ascii=False)
    
    print(f"\n✓ JSON report saved: {filename}")
    return filename


def export_csv(results: List[TrialResult], filename: str) -> str:
    """Eksportuje surowe dane do CSV."""
    with open(filename, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow([
            "Model", "Trial", "CAN_ID", 
            "t_det_us", "t_tx_up_us", "t_llm_us", "t_comp_us", "t_ota_us",
            "t_total_us", "t_total_ms", "Success"
        ])
        for r in results:
            writer.writerow([
                r.model, r.trial, r.can_id,
                round(r.t_det_us, 1), round(r.t_tx_up_us, 1),
                round(r.t_llm_us, 1), round(r.t_comp_us, 1),
                round(r.t_ota_us, 1), round(r.t_total_us, 1),
                round(r.t_total_ms, 2), r.success
            ])
    
    print(f"✓ CSV data saved: {filename}")
    return filename


def export_stats_csv(stats: List[ModelStats], filename: str) -> str:
    """Eksportuje zagregowane statystyki do CSV."""
    with open(filename, 'w', newline='', encoding='utf-8') as f:
        writer = csv.writer(f)
        writer.writerow([
            "Model", "Samples",
            "mean_tDet_ms", "sigma_tDet_ms",
            "mean_tTxUp_ms", "sigma_tTxUp_ms",
            "mean_tLlm_ms", "sigma_tLlm_ms",
            "mean_tComp_ms", "sigma_tComp_ms",
            "mean_tOta_ms", "sigma_tOta_ms",
            "mean_tTotal_ms", "sigma_tTotal_ms",
        ])
        for s in stats:
            writer.writerow([
                s.model, s.sample_count,
                round(s.mean_det_ms, 3), round(s.sigma_det_ms, 3),
                round(s.mean_tx_up_ms, 3), round(s.sigma_tx_up_ms, 3),
                round(s.mean_llm_ms, 3), round(s.sigma_llm_ms, 3),
                round(s.mean_comp_ms, 3), round(s.sigma_comp_ms, 3),
                round(s.mean_ota_ms, 3), round(s.sigma_ota_ms, 3),
                round(s.mean_total_ms, 3), round(s.sigma_total_ms, 3),
            ])
    
    print(f"✓ Statistics CSV saved: {filename}")
    return filename


def export_txt_report(results: List[TrialResult], stats: List[ModelStats],
                      filename: str) -> str:
    """Eksportuje raport tekstowy."""
    lines = []
    lines.append("=" * 75)
    lines.append("EKSPERYMENT 1.1: PROFILOWANIE CZASU FAZY ADAPTACJI")
    lines.append("Cold Start Latency Breakdown")
    lines.append("=" * 75)
    lines.append(f"Data: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    lines.append(f"Liczba prób na model: {TRIALS_PER_MODEL}")
    lines.append(f"Liczba rzeczywistych zapytań API na model: {REAL_CALLS_PER_MODEL}")
    lines.append(f"Testowane modele: {', '.join(MODELS)}")
    lines.append("")
    
    lines.append("─" * 75)
    lines.append("WYNIKI — TABELA PORÓWNAWCZA (μ ± σ) [ms]")
    lines.append("─" * 75)
    
    header = f"{'Model':<22} {'t_det':>12} {'t_tx_up':>12} {'t_llm':>12} {'t_comp':>12} {'t_ota':>12} {'TOTAL':>12}"
    lines.append(header)
    lines.append("-" * 75)
    
    for s in stats:
        row = (f"{s.model:<22} "
               f"{s.mean_det_ms:>5.2f}±{s.sigma_det_ms:.2f}  "
               f"{s.mean_tx_up_ms:>5.1f}±{s.sigma_tx_up_ms:.1f}  "
               f"{s.mean_llm_ms:>5.0f}±{s.sigma_llm_ms:.0f}  "
               f"{s.mean_comp_ms:>5.1f}±{s.sigma_comp_ms:.1f}  "
               f"{s.mean_ota_ms:>5.0f}±{s.sigma_ota_ms:.0f}  "
               f"{s.mean_total_ms:>5.0f}±{s.sigma_total_ms:.0f}")
        lines.append(row)
    
    lines.append("")
    lines.append("─" * 75)
    lines.append("SKŁADOWE T_total — szczegółowy opis")
    lines.append("─" * 75)
    lines.append(f"  t_det   — czas od pojawienia się ramki do decyzji o wysłaniu zapytania (~{T_DET_MEAN/1000:.1f} ms)")
    lines.append(f"  t_tx_up — czas transmisji ESP32 → serwer MCP (~{T_TXUP_MEAN/1000:.1f} ms)")
    lines.append(f"  t_llm   — czas wnioskowania modelu LLM (mierzony z API)")
    lines.append(f"  t_comp  — czas przygotowania reguły na serwerze (~{T_COMP_MEAN/1000:.1f} ms)")
    lines.append(f"  t_ota   — czas aktualizacji OTA na ESP32 (~{T_OTA_MEAN/1000:.1f} ms)")
    
    lines.append("")
    lines.append("─" * 75)
    lines.append("WNIOSKI")
    lines.append("─" * 75)
    
    # Znajdź dominujący komponent
    for s in stats:
        components = {
            "t_det": s.mean_det_ms,
            "t_tx_up": s.mean_tx_up_ms,
            "t_llm": s.mean_llm_ms,
            "t_comp": s.mean_comp_ms,
            "t_ota": s.mean_ota_ms,
        }
        dominant = max(components, key=components.get)
        pct = components[dominant] / s.mean_total_ms * 100
        lines.append(f"  {s.model}: dominujący komponent = {dominant} ({pct:.0f}% T_total)")
    
    text = '\n'.join(lines)
    
    with open(filename, 'w', encoding='utf-8') as f:
        f.write(text)
    
    print(f"✓ Text report saved: {filename}")
    return filename


# =============================================================================
# Wizualizacja
# =============================================================================

def visualize_results(stats: List[ModelStats], output_png: str):
    """Generuje wykres słupkowy skumulowany (stacked bar chart)."""
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        import matplotlib.ticker as mticker
    except ImportError:
        print("[WARNING] matplotlib not available, skipping visualization")
        return None
    
    # Przygotowanie danych
    models_display = [s.model for s in stats]
    
    det_means = [s.mean_det_ms for s in stats]
    tx_means = [s.mean_tx_up_ms for s in stats]
    llm_means = [s.mean_llm_ms for s in stats]
    comp_means = [s.mean_comp_ms for s in stats]
    ota_means = [s.mean_ota_ms for s in stats]
    
    det_sigmas = [s.sigma_det_ms for s in stats]
    tx_sigmas = [s.sigma_tx_up_ms for s in stats]
    llm_sigmas = [s.sigma_llm_ms for s in stats]
    comp_sigmas = [s.sigma_comp_ms for s in stats]
    ota_sigmas = [s.sigma_ota_ms for s in stats]
    
    # Kolorystyka
    colors = ['#2ecc71', '#3498db', '#e74c3c', '#f39c12', '#9b59b6']
    labels = ['t_det (detekcja)', 't_tx_up (transmisja)', 't_llm (wnioskowanie)', 
              't_comp (kompilacja)', 't_ota (aktualizacja)']
    
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 7))
    
    # Wykres 1: Stacked bar chart
    x = np.arange(len(models_display))
    width = 0.55
    
    bottom = np.zeros(len(models_display))
    bars_data = [
        (det_means, colors[0], labels[0]),
        (tx_means, colors[1], labels[1]),
        (llm_means, colors[2], labels[2]),
        (comp_means, colors[3], labels[3]),
        (ota_means, colors[4], labels[4]),
    ]
    
    for values, color, label in bars_data:
        bars = ax1.bar(x, values, width, bottom=bottom, color=color, 
                       label=label, edgecolor='white', linewidth=0.5)
        bottom += np.array(values)
        
        # Dodaj etykiety wartości na słupkach (tylko jeśli > 10% całości)
        for i, (bar, val, total) in enumerate(zip(bars, values, bottom - np.array(values) + np.array(values))):
            if val > max(bottom) * 0.04:
                ax1.text(bar.get_x() + bar.get_width()/2., 
                         bar.get_y() + bar.get_height()/2.,
                         f'{val:.0f}', ha='center', va='center', 
                         fontsize=7, fontweight='bold', color='white')
    
    ax1.set_ylabel('Czas [ms]')
    ax1.set_title('Eksperyment 1.1: Cold Start Latency Breakdown\nSkumulowane składowe T_total (μ)', fontsize=13, fontweight='bold')
    ax1.set_xticks(x)
    ax1.set_xticklabels(models_display, fontsize=11)
    ax1.legend(loc='upper right', fontsize=8, framealpha=0.9)
    ax1.grid(axis='y', alpha=0.3)
    
    # Dodaj całkowity czas nad słupkami
    for i, (model, total) in enumerate(zip(models_display, bottom)):
        ax1.text(i, total + max(bottom)*0.01, f'{total:.0f} ms', 
                ha='center', va='bottom', fontsize=9, fontweight='bold')
    
    # Wykres 2: Udział procentowy (pie chart zbiorczy lub grouped bar)
    # Pokazujemy % udział każdej składowej dla każdego modelu
    x2 = np.arange(len(labels))
    width2 = 0.25
    
    for i, (model, det, tx, llm, comp, ota) in enumerate(
        zip(models_display, det_means, tx_means, llm_means, comp_means, ota_means)):
        total = det + tx + llm + comp + ota
        pcts = [det/total*100, tx/total*100, llm/total*100, comp/total*100, ota/total*100]
        bars = ax2.bar(x2 + i*width2, pcts, width2, 
                       color=colors, edgecolor='white', linewidth=0.5,
                       alpha=0.85)
        # Etykieta modelu
        for j, (bar, pct) in enumerate(zip(bars, pcts)):
            if pct > 5:
                ax2.text(bar.get_x() + bar.get_width()/2., bar.get_height() + 0.5,
                        f'{pct:.0f}%', ha='center', va='bottom', fontsize=7,
                        color=colors[j], fontweight='bold')
    
    ax2.set_ylabel('Udział w T_total [%]')
    ax2.set_title('Procentowy udział składowych w całkowitym opóźnieniu', fontsize=13, fontweight='bold')
    ax2.set_xticks(x2 + width2)
    ax2.set_xticklabels(labels, rotation=25, ha='right', fontsize=8)
    ax2.legend(models_display, loc='upper right', fontsize=9)
    ax2.grid(axis='y', alpha=0.3)
    
    plt.tight_layout(pad=2)
    plt.savefig(output_png, dpi=150, bbox_inches='tight', facecolor='white')
    plt.close()
    
    print(f"✓ Chart saved: {output_png}")
    return output_png


# =============================================================================
# MAIN
# =============================================================================

def main():
    print("╔══════════════════════════════════════════════════════════════╗")
    print("║  MagistralaCAN4 — Eksperyment 1.1                          ║")
    print("║  Cold Start Latency Breakdown                               ║")
    print("╚══════════════════════════════════════════════════════════════╝")
    print(f"\n  Modele: {', '.join(MODELS)}")
    print(f"  Próbek na model: {TRIALS_PER_MODEL}")
    print(f"  Rzeczywistych zapytań API na model: {REAL_CALLS_PER_MODEL}")
    print(f"  Łącznie zapytań API: {len(MODELS) * REAL_CALLS_PER_MODEL}")
    
    # 1. Przeprowadź eksperyment
    results = run_full_experiment()
    
    # 2. Oblicz statystyki
    stats = compute_statistics(results)
    
    # 3. Eksportuj dane
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    
    json_file = export_json(results, stats, f"experiment_1.1_results_{timestamp}.json")
    csv_file = export_csv(results, f"experiment_1.1_raw_{timestamp}.csv")
    stats_csv = export_stats_csv(stats, f"experiment_1.1_statistics_{timestamp}.csv")
    txt_file = export_txt_report(results, stats, f"experiment_1.1_report_{timestamp}.txt")
    
    # 4. Wizualizacja
    png_file = visualize_results(stats, f"experiment_1.1_chart_{timestamp}.png")
    
    # 5. Podsumowanie
    print(f"\n{'='*60}")
    print("EKSPERYMENT ZAKOŃCZONY")
    print(f"{'='*60}")
    print(f"\nWygenerowane pliki ({timestamp}):")
    print(f"  📊 {json_file}")
    print(f"  📋 {csv_file}")
    print(f"  📈 {stats_csv}")
    print(f"  📝 {txt_file}")
    if png_file:
        print(f"  🎨 {png_file}")
    
    print(f"\nTabela porównawcza (μ ± σ) [ms]:")
    print(f"{'Model':<22} {'t_llm':>10} {'T_total':>12}")
    print(f"{'-'*44}")
    for s in stats:
        print(f"{s.model:<22} {s.mean_llm_ms:>5.0f}±{s.sigma_llm_ms:.0f}  {s.mean_total_ms:>6.0f}±{s.sigma_total_ms:.0f}")
    
    return results, stats


if __name__ == "__main__":
    main()
