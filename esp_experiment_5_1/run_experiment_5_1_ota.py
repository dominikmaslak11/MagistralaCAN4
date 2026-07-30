#!/usr/bin/env python3
"""
Eksperyment 5.1 — trzeci stan metodyki: "OTA Update - moment aktualizacji
i kompilacji nowej reguly w pamieci" (Pomiary dla CAN-Edge AI.md, Grupa 5).

Wczesniej NIEZREALIZOWANY (mechanizm aplikowania reguly LLM na urzadzeniu
nie istnial w architekturze projektu - reguly zyly wylacznie po stronie PC,
DecodingAccuracyRunner). Dodany 2026-07-30: firmware esp_experiment_5_1_jtag
(main.cpp) ma teraz tryb MODE_OTA=2, ktory przyjmuje reguly LLM przez CAN
(pola 1:1 z LlmSignalRule w src/core/DecodingAccuracyRunner.h: byteIdx,
byteLen, littleEndian, isSigned, bitMask, scale, offset) i "kompiluje" je
do tabeli aktywnych regul na urzadzeniu (realny zapis, nie no-op).

Poniewaz jedna regula NIE miesci sie w jednej ramce CAN (8 bajtow), jest
dostarczana w 4 ramkach sterujacych: LOAD1 (naglowek: canId+byteIdx+
byteLen+flags), LOAD2 (bitMask), LOAD3 (scale), COMMIT (offset +
wyzwolenie kompilacji - to jest moment mierzony jako "praca OTA").

Ten skrypt jest CELOWO OSOBNY od run_experiment_5_1.py (ktory pozostaje
NIEZMIENIONY dla zachowania porownywalnosci juz opublikowanych wynikow
IDLE/PARSING) - generuje inny rodzaj "ruchu" (sekwencje regul, nie losowe
ramki danych) i inna, wolniejsza, realistyczna czestotliwosc zdarzen
(aktualizacja reguly to rzadsze, ciezsze zdarzenie niz pojedyncza ramka
danych CAN).

Wymaga: firmware esp_experiment_5_1_jtag wgrany na ESP32 (build w
esp_experiment_5_1_jtag/), CAN (PEAK PCAN-USB) podniesiony jako can0.

Uzycie:
  ./run_experiment_5_1_ota.py <katalog_wyjsciowy> [n=20] [window_s=3] [rules_per_s=10]

Domyslnie N=20 pomiarow (spojne z N=20/stan uzytym dla IDLE/PARSING - razem
40+20=60, ale metodyka mowi o N=40 OGOLNIE dla calego Eksperymentu 5.1,
wiec 20 tu jest w duchu tej samej konwencji co pozostale 2 stany).
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
CTRL_OTA_LOAD1 = 0x05
CTRL_OTA_LOAD2 = 0x06
CTRL_OTA_LOAD3 = 0x07
CTRL_OTA_COMMIT = 0x08

MODE_IDLE = 0
MODE_PARSING = 1
MODE_OTA = 2

# Pula CAN ID dla ktorych "przychodza" nowe reguly - te same co ruch
# testowy w Eksperymencie 4.1 (0x100/0x150/0x200), zeby scenariusz byl
# realistyczny (te same sygnaly co model probowalby dekodowac).
RULE_CAN_IDS = [0x100, 0x150, 0x200]

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


def send_ota_rule(sock, rule_can_id, byte_idx, byte_len, little_endian, is_signed,
                   bit_mask, scale, offset):
    flags = (1 if little_endian else 0) | (2 if is_signed else 0)

    load1 = struct.pack("<I", rule_can_id) + bytes([byte_idx, byte_len, flags])
    load2 = struct.pack("<I", bit_mask)
    load3 = struct.pack("<f", scale)
    commit = struct.pack("<f", offset)

    ok = True
    ok &= send_or_skip(sock, build_frame(CONTROL_CAN_ID, bytes([CTRL_OTA_LOAD1]) + load1))
    ok &= send_or_skip(sock, build_frame(CONTROL_CAN_ID, bytes([CTRL_OTA_LOAD2]) + load2))
    ok &= send_or_skip(sock, build_frame(CONTROL_CAN_ID, bytes([CTRL_OTA_LOAD3]) + load3))
    ok &= send_or_skip(sock, build_frame(CONTROL_CAN_ID, bytes([CTRL_OTA_COMMIT]) + commit))
    return ok


def generate_ota_traffic(sock, rules_per_s, duration_s):
    """Symuluje kolejne 'przybywajace' reguly LLM (rozne CAN ID/byteIdx/
    skala kazdorazowo) - kazda regula to 4 ramki sterujace."""
    interval = 1.0 / rules_per_s
    n_committed = 0
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
        rule_can_id = random.choice(RULE_CAN_IDS)
        byte_idx = random.randrange(0, 8)
        byte_len = random.choice([1, 2])
        bit_mask = random.choice([0xFF, 0x0F, 0xFFFF, 0xFFFFFFFF])
        scale = round(random.uniform(0.1, 10.0), 3)
        offset = round(random.uniform(-50.0, 50.0), 3)
        if send_ota_rule(sock, rule_can_id, byte_idx, byte_len, True, False,
                          bit_mask, scale, offset):
            n_committed += 1
        next_send += interval
    return n_committed


def wait_for_result(ser, timeout_s=1.5):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        line = ser.readline()
        if not line:
            continue
        s = line.decode(errors="replace").strip()
        if s.startswith("RESULT,"):
            parts = s.split(",")
            result = {
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
            if len(parts) > 12:
                result["ota_commit_count"] = int(parts[12])
            return result
    return None


def run_measurement(sock, ser, window_s, rules_per_s):
    ser.reset_input_buffer()
    sock.send(build_frame(CONTROL_CAN_ID, bytes([CTRL_RESET])))
    time.sleep(0.05)
    sock.send(build_frame(CONTROL_CAN_ID, bytes([CTRL_SET_MODE, MODE_OTA])))
    time.sleep(0.05)

    n_sent = generate_ota_traffic(sock, rules_per_s, window_s)
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
    ap.add_argument("n", type=int, nargs="?", default=20)
    ap.add_argument("window_s", type=float, nargs="?", default=3.0)
    ap.add_argument("rules_per_s", type=float, nargs="?", default=10.0)
    args = ap.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    sock.bind((CAN_IFACE,))
    ser = serial.Serial(SERIAL_PORT, BAUD, timeout=0.3)
    time.sleep(0.5)

    print(f"Eksperyment 5.1 — stan OTA Update ({args.n} pomiarow x {args.window_s}s "
          f"@ {args.rules_per_s} regul/s) ...")

    rows = []
    for i in range(args.n):
        res = run_measurement(sock, ser, args.window_s, args.rules_per_s)
        if res is None:
            print(f"  [{i+1}/{args.n}] [UWAGA] brak odpowiedzi — pomijam")
            continue
        cpu_load_pct_real = (100.0 * res["loop_task_busy_us"] / res["elapsed_us"]
                              if res["elapsed_us"] > 0 else 0.0)
        used_heap = res["heap_size"] - res["free_heap"]
        row = {
            "state": "OTA",
            "mode": MODE_OTA,
            "elapsed_ms": res["elapsed_ms"],
            "n_sent_rules": res["n_sent"],
            "ota_commit_count": res.get("ota_commit_count", 0),
            "cpu_load_pct_real": round(cpu_load_pct_real, 3),
            "free_heap_kb": round(res["free_heap"] / 1024.0, 2),
            "min_free_heap_kb": round(res["min_free_heap"] / 1024.0, 2),
            "used_heap_kb": round(used_heap / 1024.0, 2),
            "heap_size_kb": round(res["heap_size"] / 1024.0, 2),
            "sketch_size_kb": round(res["sketch_size"] / 1024.0, 2),
            "free_sketch_space_kb": round(res["free_sketch_space"] / 1024.0, 2),
        }
        rows.append(row)
        print(f"  [{i+1}/{args.n}] regul_wyslanych={res['n_sent']:3d} "
              f"skompilowanych={row['ota_commit_count']:3d} "
              f"RAM_used={row['used_heap_kb']:6.1f}kB "
              f"CPU~{row['cpu_load_pct_real']:6.3f}%")

    ser.close()
    sock.close()

    raw_path = out_dir / "raw_data.csv"
    fieldnames = ["state", "mode", "elapsed_ms", "n_sent_rules", "ota_commit_count",
                  "cpu_load_pct_real", "free_heap_kb", "min_free_heap_kb",
                  "used_heap_kb", "heap_size_kb", "sketch_size_kb", "free_sketch_space_kb"]
    with open(raw_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for r in rows:
            w.writerow(r)
    print(f"\n  OK {raw_path}")

    lines = [
        "=" * 78,
        "EKSPERYMENT 5.1 — stan OTA Update (kompilacja nowej reguly LLM w pamieci)",
        "=" * 78,
        "",
    ]
    if rows:
        n = len(rows)
        avg_used = sum(r["used_heap_kb"] for r in rows) / n
        avg_min_free = sum(r["min_free_heap_kb"] for r in rows) / n
        avg_cpu = sum(r["cpu_load_pct_real"] for r in rows) / n
        avg_commits = sum(r["ota_commit_count"] for r in rows) / n
        lines += [
            f"{'stan':>10} {'N':>4} {'RAM uzyte[kB]':>15} {'RAM min.wolne[kB]':>18} "
            f"{'CPU[%] real':>12} {'sr. skompilowanych regul/okno':>30}",
            f"{'OTA':>10} {n:>4} {avg_used:>15.2f} {avg_min_free:>18.2f} "
            f"{avg_cpu:>12.3f} {avg_commits:>30.1f}",
            "",
            f"Flash (STATYCZNE): sketch={rows[0]['sketch_size_kb']:.1f}kB, "
            f"wolne={rows[0]['free_sketch_space_kb']:.1f}kB",
        ]
    lines += [
        "",
        "UWAGA METODOLOGICZNA:",
        "- 'OTA Update' w tym projekcie oznacza dostarczenie i skompilowanie",
        "  reguly dekodujacej LLM (pola 1:1 z LlmSignalRule w",
        "  src/core/DecodingAccuracyRunner.h) na urzadzeniu przez CAN, NIE",
        "  aktualizacje calego firmware'u (klasyczne rozumienie 'OTA' w",
        "  kontekscie ESP32 to zazwyczaj aktualizacja binarki - tu chodzi o",
        "  aktualizacje POJEDYNCZEJ reguly interpretacji sygnalu, zgodnie z",
        "  brzmieniem metodyki 'kompilacji nowej reguly w pamieci').",
        "- Jedna regula NIE miesci sie w jednej ramce CAN (8 bajtow) - wymaga",
        "  4 ramek sterujacych (LOAD1/LOAD2/LOAD3/COMMIT) - naturalne",
        "  ograniczenie przepustowosci magistrali warte odnotowania.",
        "- 'Kompilacja' = zapis reguly do tabeli aktywnych regul na",
        "  urzadzeniu (do 8 regul, analogicznie do tabeli klasyfikacji w",
        "  PARSING) - realna praca, nie no-op; mierzona tym samym",
        "  mechanizmem bezposredniego micros() co PARSING.",
        "- Czestotliwosc 'przybywania' regul (domyslnie 10/s) to parametr",
        "  STRESS-TESTOWY dla uzyskania stabilnej statystyki w krotkim oknie",
        "  pomiarowym (3s) - NIE literalna czestotliwosc rzeczywistych",
        "  aktualizacji LLM w praktyce (te wystepowalyby rzadziej, rzedu",
        "  pojedynczych regul na Cold Start trwajacy sekundy, patrz",
        "  Eksperyment 1.1).",
    ]
    report_path = out_dir / "report.txt"
    with open(report_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"  OK {report_path}")
    print("\n".join(lines))
    print("Gotowe.")


if __name__ == "__main__":
    main()
