#!/usr/bin/env python3
"""
Eksperyment 2.2 — Odpornosc bufora podczas fazy adaptacji
(Buffer Overflow Threshold), wg "Pomiary dla CAN-Edge AI.md", Grupa 2.

Symuluje faze Cold Start (deterministyczne, sterowane z hosta opoznienie
zamiast prawdziwego zapytania LLM) i sprawdza przy jakim czasie oczekiwania
(dla danej czestotliwosci wejsciowej i rozmiaru bufora programowego w
firmware ESP32) dochodzi do utraty ramek.

Wymaga: firmware esp_experiment_2_2.ino wgrany z konkretnym BUFFER_SIZE
(przekazanym tu jako argument - tylko do etykietowania wynikow, firmware
NIE jest przez ten skrypt przeprogramowywany automatycznie).

Uzycie:
  ./run_experiment_2_2.py <katalog_wyjsciowy> <buffer_size>

Dla kazdej czestotliwosci f w [100, 500, 1000] Hz testuje 4 punkty czasu
oczekiwania wokol teoretycznego progu (T_theor = 1000*buffer_size/f [ms]):
0.5x, 0.8x, 1.0x, 1.2x. Razem 3*4=12 pomiarow na jedno wywolanie (jeden
rozmiar bufora) - dla 5 rozmiarow bufora (4/8/16/32/64) daje to lacznie
5*12=60 pomiarow, zgodnie z metodyka.
"""
import sys
import csv
import errno
import socket
import struct
import time
import argparse
from pathlib import Path

import serial

SERIAL_PORT = "/dev/ttyUSB0"
BAUD = 115200
CAN_IFACE = "can0"

TEST_CAN_ID = 0x200
CONTROL_CAN_ID = 0x7FE
CTRL_RESET = 0x01
CTRL_START_BUSY = 0x02
CTRL_REPORT = 0x03

CAN_FRAME_FMT = "=IB3x8s"

FREQUENCIES_HZ = [100, 500, 1000]
# Szerszy zakres niz [0.5-1.2] - przy wysokich czestotliwosciach (male okna
# bezwzgledne, np. 4ms dla f=1000Hz,buffer=4) staly narzut protokolu
# (transmisja ramki sterujacej, ISR/SPI) staje sie porownywalny z samym
# oknem i przesuwa realny prog wyzej niz teoria - trzeba to zlapac.
TEST_FACTORS = [0.3, 0.6, 1.0, 1.5, 2.5, 4.0]  # wzgledem teoretycznego progu


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


def generate_traffic(sock, rate_fps, duration_s):
    interval = 1.0 / rate_fps
    test_frame = build_frame(TEST_CAN_ID, b"\xAA" * 8)
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
            if remaining > 0.001:
                time.sleep(remaining - 0.001)
            while time.perf_counter() < next_send:
                pass
        if send_or_skip(sock, test_frame):
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
            return int(parts[1]), int(parts[2]), int(parts[3])
    return None


def run_one_point(sock, ser, rate_fps, wait_ms, buffer_size):
    ser.reset_input_buffer()
    sock.send(build_frame(CONTROL_CAN_ID, bytes([CTRL_RESET])))
    time.sleep(0.05)

    duration_ms_be = struct.pack(">I", wait_ms)
    sock.send(build_frame(CONTROL_CAN_ID, bytes([CTRL_START_BUSY]) + duration_ms_be))
    # CAN zachowuje kolejnosc ramek od jednego nadawcy (FIFO na magistrali) -
    # ESP32 przetworzy START_BUSY przed kolejnymi ramkami testowymi nawet bez
    # opoznienia. Minimalny margines tylko na wlasny czas transmisji ramki.
    time.sleep(0.002)

    gen_duration_s = wait_ms / 1000.0 + 0.15  # troche dluzej niz okno BUSY
    n_sent = generate_traffic(sock, rate_fps, gen_duration_s)

    time.sleep(0.15)  # margines na "przetworzenie" po stronie ESP32

    result = None
    for _ in range(5):
        sock.send(build_frame(CONTROL_CAN_ID, bytes([CTRL_REPORT])))
        result = wait_for_result(ser, timeout_s=0.8)
        if result is not None:
            break
    if result is None:
        return None

    total_arrived, dropped, buf_size_confirmed = result
    return {
        "buffer_size": buffer_size,
        "buffer_size_confirmed": buf_size_confirmed,
        "frequency_hz": rate_fps,
        "wait_ms_target": wait_ms,
        "n_sent": n_sent,
        "total_arrived": total_arrived,
        "dropped": dropped,
        "loss": dropped > 0,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir")
    ap.add_argument("buffer_size", type=int, help="rozmiar bufora BUFFER_SIZE wgrany w firmware (etykieta)")
    args = ap.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    sock.bind((CAN_IFACE,))
    ser = serial.Serial(SERIAL_PORT, BAUD, timeout=0.3)
    time.sleep(0.5)

    print(f"Eksperyment 2.2 — buffer_size={args.buffer_size}, "
          f"{len(FREQUENCIES_HZ)} czestotliwosci x {len(TEST_FACTORS)} punkty...")

    rows = []
    for freq in FREQUENCIES_HZ:
        theoretical_ms = 1000.0 * args.buffer_size / freq
        for factor in TEST_FACTORS:
            wait_ms = max(1, round(theoretical_ms * factor))
            res = run_one_point(sock, ser, freq, wait_ms, args.buffer_size)
            if res is None:
                print(f"  [UWAGA] Brak odpowiedzi ESP32 dla f={freq}Hz wait={wait_ms}ms — pomijam")
                continue
            res["theoretical_ms"] = round(theoretical_ms, 1)
            res["factor"] = factor
            rows.append(res)
            status = "STRATA" if res["loss"] else "OK"
            print(f"  f={freq:4d}Hz wait={wait_ms:5d}ms (teor={theoretical_ms:6.1f}ms, x{factor}) "
                  f"sent={res['n_sent']:4d} arrived={res['total_arrived']:4d} "
                  f"dropped={res['dropped']:3d} -> {status}")

    ser.close()
    sock.close()

    raw_path = out_dir / "raw_data.csv"
    with open(raw_path, "w", newline="", encoding="utf-8") as f:
        fieldnames = ["buffer_size", "buffer_size_confirmed", "frequency_hz",
                      "theoretical_ms", "factor", "wait_ms_target",
                      "n_sent", "total_arrived", "dropped", "loss"]
        w = csv.DictWriter(f, fieldnames=fieldnames)
        w.writeheader()
        for r in rows:
            w.writerow(r)
    print(f"  OK {raw_path}")

    # Tabela progowa: dla kazdej czestotliwosci - najwiekszy przetestowany
    # wait_ms bez straty (max_safe) i najmniejszy z strata (first_unsafe)
    lines = [
        "=" * 70,
        f"EKSPERYMENT 2.2 — Buffer Overflow Threshold, BUFFER_SIZE={args.buffer_size}",
        "=" * 70,
        "",
        f"{'freq[Hz]':>10} {'teor.prog[ms]':>15} {'max_safe[ms]':>14} {'first_unsafe[ms]':>17}",
    ]
    threshold_rows = []
    for freq in FREQUENCIES_HZ:
        pts = [r for r in rows if r["frequency_hz"] == freq]
        if not pts:
            continue
        safe = [r["wait_ms_target"] for r in pts if not r["loss"]]
        unsafe = [r["wait_ms_target"] for r in pts if r["loss"]]
        max_safe = max(safe) if safe else None
        first_unsafe = min(unsafe) if unsafe else None
        theor = pts[0]["theoretical_ms"]
        threshold_rows.append((freq, theor, max_safe, first_unsafe))
        lines.append(f"{freq:>10} {theor:>15.1f} "
                      f"{(max_safe if max_safe is not None else '-'):>14} "
                      f"{(first_unsafe if first_unsafe is not None else '-'):>17}")

    lines += [
        "",
        "Model: bufor programowy BUFFER_SIZE w ESP32, wypelniany podczas",
        "symulowanej fazy Cold Start (system nie 'przetwarza' ramek), az do",
        "pojemnosci bufora - kolejne ramki ponad pojemnosc sa tracone.",
        "Oczekiwana teoretycznie zaleznosc: T_prog = BUFFER_SIZE / czestotliwosc.",
    ]
    report_path = out_dir / "report.txt"
    with open(report_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"  OK {report_path}")
    print("\n".join(lines))
    print("Gotowe.")


if __name__ == "__main__":
    main()
