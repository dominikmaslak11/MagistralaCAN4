#!/usr/bin/env python3
"""
Eksperyment 5.1 — Profilowanie pamieci i CPU, wg "Pomiary dla CAN-Edge AI.md",
Grupa 5.

Mierzy zuzycie RAM/Flash/CPU ESP32 w dwoch stanach pracy (IDLE, PARSING -
patrz esp_experiment_5_1.ino dla dokladnego opisu i uzasadnienia metodyki;
trzeci stan z metodyki, "OTA Update", NIE jest realizowany - mechanizm
aplikowania reguly na urzadzeniu jeszcze nie istnieje w tym projekcie).

Wymaga: firmware esp_experiment_5_1.ino wgrany na ESP32, CAN (PEAK PCAN-USB)
podniesiony jako can0.

Uzycie:
  ./run_experiment_5_1.py <katalog_wyjsciowy> [n_per_state=20] [window_s=3] [rate_hz=50]

Domyslnie 2 stany x 20 pomiarow = 40 pomiarow, zgodnie z metodyka ("Liczba
pomiarow do weryfikacji: 40").
"""
import sys
import csv
import errno
import random
import socket
import struct
import time
import argparse
from pathlib import Path

import serial

SERIAL_PORT = "/dev/ttyUSB0"
BAUD = 115200
CAN_IFACE = "can0"

CONTROL_CAN_ID = 0x7FE
CTRL_RESET = 0x01
CTRL_REPORT = 0x03
CTRL_SET_MODE = 0x04
MODE_IDLE = 0
MODE_PARSING = 1

TRAFFIC_IDS = [0x100, 0x150, 0x200]

CAN_FRAME_FMT = "=IB3x8s"


def build_frame(can_id, data=b""):
    data = data.ljust(8, b"\x00")[:8]
    return struct.pack(CAN_FRAME_FMT, can_id, 8, data)


def send_or_skip(sock, frame):
    try:
        sock.send(frame)
        return True
    except OSError as e:
        if e.errno == errno.ENOBUFS:
            return False
        raise


def generate_traffic(sock, rate_hz, duration_s):
    """Prosty, realistyczny MIX ruchu na 3 CAN ID uzywanych w Eksperymencie 4.1
    - dla tego eksperymentu nie liczy sie semantyka sygnalow (nie testujemy
    dekodowania), tylko sam fakt realnego obciazenia magistrali/firmware'u."""
    interval = 1.0 / rate_hz
    n_sent = 0
    start = time.perf_counter()
    end_time = start + duration_s
    next_send = start
    while True:
        now = time.perf_counter()
        if now >= end_time:
            break
        if now < next_send:
            remaining = next_send - now
            if remaining > 0:
                time.sleep(remaining)
        can_id = random.choice(TRAFFIC_IDS)
        payload = bytes(random.randrange(256) for _ in range(8))
        if send_or_skip(sock, build_frame(can_id, payload)):
            n_sent += 1
        next_send += interval
    return n_sent


def wait_for_result(ser, timeout_s=1.5):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        line = ser.readline()
        if not line:
            continue
        s = line.decode(errors="replace").strip()
        if s.startswith("RESULT,"):
            parts = s.split(",")
            return {
                "mode": int(parts[1]),
                "elapsed_ms": int(parts[2]),
                "frame_count": int(parts[3]),
                "loop_spins": int(parts[4]),
                "free_heap": int(parts[5]),
                "min_free_heap": int(parts[6]),
                "heap_size": int(parts[7]),
                "sketch_size": int(parts[8]),
                "free_sketch_space": int(parts[9]),
                "loop_task_busy_us": int(parts[10]),
                "elapsed_us": int(parts[11]),
            }
    return None


def run_measurement(sock, ser, mode, window_s, rate_hz):
    ser.reset_input_buffer()
    sock.send(build_frame(CONTROL_CAN_ID, bytes([CTRL_RESET])))
    time.sleep(0.05)
    sock.send(build_frame(CONTROL_CAN_ID, bytes([CTRL_SET_MODE, mode])))
    time.sleep(0.05)

    n_sent = generate_traffic(sock, rate_hz, window_s) if (window_s > 0 and rate_hz > 0) else 0
    if window_s > 0 and rate_hz <= 0:
        time.sleep(window_s)
    time.sleep(0.1)

    result = None
    for _ in range(5):
        sock.send(build_frame(CONTROL_CAN_ID, bytes([CTRL_REPORT])))
        result = wait_for_result(ser, timeout_s=0.8)
        if result is not None:
            break
    if result is None:
        return None
    result["n_sent"] = n_sent
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir")
    ap.add_argument("n_per_state", type=int, nargs="?", default=20)
    ap.add_argument("window_s", type=float, nargs="?", default=3.0)
    ap.add_argument("rate_hz", type=float, nargs="?", default=50.0)
    args = ap.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    sock.bind((CAN_IFACE,))
    ser = serial.Serial(SERIAL_PORT, BAUD, timeout=0.3)
    time.sleep(0.5)

    print("Eksperyment 5.1 — kalibracja (magistrala cicha, tryb IDLE)...")
    calib = run_measurement(sock, ser, MODE_IDLE, args.window_s, 0.0)
    if calib is None:
        print("[BLAD] Brak odpowiedzi ESP32 przy kalibracji — sprawdz polaczenie/firmware.")
        sys.exit(1)
    baseline_spin_rate = calib["loop_spins"] / (calib["elapsed_ms"] / 1000.0)
    print(f"  baseline (0% obciazenia): {baseline_spin_rate:.1f} spins/s "
          f"(elapsed={calib['elapsed_ms']}ms, spins={calib['loop_spins']})")

    rows = []
    labels = [("IDLE", MODE_IDLE), ("PARSING", MODE_PARSING)]
    for label, mode in labels:
        print(f"\n--- Stan: {label} ({args.n_per_state} pomiarow x {args.window_s}s @ {args.rate_hz}Hz) ---")
        for i in range(args.n_per_state):
            res = run_measurement(sock, ser, mode, args.window_s, args.rate_hz)
            if res is None:
                print(f"  [{i+1}/{args.n_per_state}] [UWAGA] brak odpowiedzi — pomijam")
                continue
            spin_rate = res["loop_spins"] / (res["elapsed_ms"] / 1000.0) if res["elapsed_ms"] > 0 else 0.0
            cpu_load_pct_proxy = max(0.0, 100.0 * (1.0 - spin_rate / baseline_spin_rate))
            cpu_load_pct_real = (100.0 * res["loop_task_busy_us"] / res["elapsed_us"]
                                  if res["elapsed_us"] > 0 else 0.0)
            used_heap = res["heap_size"] - res["free_heap"]
            row = {
                "state": label,
                "mode": mode,
                "elapsed_ms": res["elapsed_ms"],
                "frame_count": res["frame_count"],
                "n_sent": res["n_sent"],
                "loop_spins": res["loop_spins"],
                "spin_rate_hz": round(spin_rate, 1),
                "cpu_load_pct_proxy": round(cpu_load_pct_proxy, 2),
                "loop_task_busy_us": res["loop_task_busy_us"],
                "elapsed_us": res["elapsed_us"],
                "cpu_load_pct_real": round(cpu_load_pct_real, 3),
                "free_heap_kb": round(res["free_heap"] / 1024.0, 2),
                "min_free_heap_kb": round(res["min_free_heap"] / 1024.0, 2),
                "used_heap_kb": round(used_heap / 1024.0, 2),
                "heap_size_kb": round(res["heap_size"] / 1024.0, 2),
                "sketch_size_kb": round(res["sketch_size"] / 1024.0, 2),
                "free_sketch_space_kb": round(res["free_sketch_space"] / 1024.0, 2),
            }
            rows.append(row)
            print(f"  [{i+1}/{args.n_per_state}] frames={res['frame_count']:4d} "
                  f"RAM_used={row['used_heap_kb']:6.1f}kB "
                  f"CPU_real~{row['cpu_load_pct_real']:6.3f}% (proxy~{row['cpu_load_pct_proxy']:5.1f}%)")

    ser.close()
    sock.close()

    raw_path = out_dir / "raw_data.csv"
    fieldnames = ["state", "mode", "elapsed_ms", "frame_count", "n_sent",
                  "loop_spins", "spin_rate_hz", "cpu_load_pct_proxy",
                  "loop_task_busy_us", "elapsed_us", "cpu_load_pct_real",
                  "free_heap_kb", "min_free_heap_kb", "used_heap_kb",
                  "heap_size_kb", "sketch_size_kb", "free_sketch_space_kb"]
    with open(raw_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for r in rows:
            w.writerow(r)
    print(f"\n  OK {raw_path}")

    lines = [
        "=" * 78,
        "EKSPERYMENT 5.1 — Profilowanie pamieci i CPU (ESP32)",
        "=" * 78,
        "",
        f"Kalibracja (magistrala cicha, IDLE): {baseline_spin_rate:.1f} spins/s (=0% obciazenia referencyjnie)",
        "",
        f"{'stan':>10} {'N':>4} {'RAM uzyte[kB]':>15} {'RAM min.wolne[kB]':>18} "
        f"{'CPU[%] real':>12} {'CPU[%] proxy':>13}",
    ]
    for label, _ in labels:
        pts = [r for r in rows if r["state"] == label]
        if not pts:
            continue
        n = len(pts)
        avg_used = sum(p["used_heap_kb"] for p in pts) / n
        avg_min_free = sum(p["min_free_heap_kb"] for p in pts) / n
        avg_cpu_real = sum(p["cpu_load_pct_real"] for p in pts) / n
        avg_cpu_proxy = sum(p["cpu_load_pct_proxy"] for p in pts) / n
        lines.append(f"{label:>10} {n:>4} {avg_used:>15.2f} {avg_min_free:>18.2f} "
                      f"{avg_cpu_real:>12.3f} {avg_cpu_proxy:>13.1f}")

    if rows:
        lines += [
            "",
            f"Flash (STATYCZNE, nie zalezy od stanu): sketch={rows[0]['sketch_size_kb']:.1f}kB, "
            f"wolne={rows[0]['free_sketch_space_kb']:.1f}kB",
        ]

    lines += [
        "",
        "UWAGA METODOLOGICZNA:",
        "- CPU[%] real to bezposredni pomiar zajetosci: micros() przed/po kazdym",
        "  bloku realnej pracy (odczyt SPI + klasyfikacja) w loop(), sumowany w",
        "  oknie miedzy RESET a REPORT. PROBOWANO wczesniej vTaskGetRunTimeStats()/",
        "  uxTaskGetSystemState() (FreeRTOS trace facility, dziala bez JTAG na tym",
        "  rdzeniu) ale okazalo sie NIEWIARYGODNE dla tej architektury: FreeRTOS",
        "  aktualizuje liczniki czasu zadania GLOWNIE przy przelaczeniach kontekstu,",
        "  a nasza petla loop() nigdy nie oddaje sterowania (ciagly polling flagi",
        "  przerwania) - zmierzono empirycznie ZERO przyrostu licznika mimo",
        "  aktywnego przetwarzania ramek. Bezposredni pomiar micros() jest odporny",
        "  na ten problem. Proxy (tempo iteracji petli) pozostawiony w kolumnie",
        "  CPU[%] proxy wylacznie dla ciaglosci porownawczej z pierwszym przebiegiem.",
        "- Stan PARSING wykonuje realna (nie no-op) klasyfikacje per-ID: min/max/",
        "  liczba przelaczen bajtu 0, w tabeli do 8 sledzonych ID - patrz",
        "  classifyFrame() w esp_experiment_5_1.ino.",
        "- Trzeci stan z metodyki (\"OTA Update\") NIE jest realizowany - wymagalby",
        "  mechanizmu aplikowania reguly LLM na urzadzeniu, ktory jeszcze nie",
        "  istnieje w architekturze projektu (patrz raport tego eksperymentu).",
    ]
    report_path = out_dir / "report.txt"
    with open(report_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"  OK {report_path}")
    print("\n".join(lines))
    print("Gotowe.")


if __name__ == "__main__":
    main()
