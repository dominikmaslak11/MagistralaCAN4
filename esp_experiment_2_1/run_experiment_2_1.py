#!/usr/bin/env python3
"""
Eksperyment 2.1 — Maksymalna bezstratna przepustowosc (CAN Frame Throughput).
Wg "Pomiary dla CAN-Edge AI.md", Grupa 2.

Generuje ruch na can0 (PEAK PCAN-USB, raw SocketCAN) w krokach co 100 ramek/s
(100..5000), dla jednego bitrate na uruchomienie (250k lub 500k - musi byc
zgodny z firmware aktualnie wgranym na ESP32 i konfiguracja can0). ESP32
(esp_experiment_2_1.ino) liczy ramki testowe odebrane bezblednie i raportuje
przez Serial po ramce sterujacej STOP.

Protokol sterujacy (CAN ID 0x7FE):
  data[0]=0x01 -> RESET (wyzeruj licznik ESP32)
  data[0]=0x02 -> STOP  (ESP32 wypisuje "RESULT,<n_rcvd>,<overflow_events>")

Uzycie:
  ./run_experiment_2_1.py <katalog_wyjsciowy> <bitrate_bps> [--duration SEC]

Przyklad:
  ./run_experiment_2_1.py ../Eksperyment_2.1_Throughput_250kbps_TIMESTAMP 250000
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
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

SERIAL_PORT = "/dev/ttyUSB0"
BAUD = 115200
CAN_IFACE = "can0"

TEST_CAN_ID = 0x200
CONTROL_CAN_ID = 0x7FE
CTRL_RESET = 0x01
CTRL_STOP = 0x02

CAN_FRAME_FMT = "=IB3x8s"

RATE_START = 100
RATE_STOP = 5000
RATE_STEP = 100

COLOR_250K = "#2a78d6"
COLOR_500K = "#e34948"
INK = "#0b0b0b"
INK_SECONDARY = "#52514e"
GRIDLINE = "#e1e0d9"
SURFACE = "#fcfcfb"


def build_frame(can_id, data=b""):
    data = data.ljust(8, b"\x00")[:8]
    return struct.pack(CAN_FRAME_FMT, can_id, 8, data)


def send_or_skip(sock, frame):
    """Wysyla ramke; zwraca True jesli faktycznie trafila na magistrale
    (sukces), False jesli lokalna kolejka byla pelna (ENOBUFS - magistrala
    nasycona, ramka NIGDY nie opuscila hosta, wiec nie liczy sie do N_sent)."""
    try:
        sock.send(frame)
        return True
    except OSError as e:
        if e.errno == errno.ENOBUFS:
            return False
        raise


def generate_traffic(sock, rate_fps, duration_s):
    """Generuje ruch z docelowa czestotliwoscia rate_fps przez duration_s
    sekund. Hybrydowe pacing: sleep() dla wiekszosci odstepu, busy-wait dla
    ostatniego ~1ms (precyzja). Jesli magistrala jest nasycona (ENOBUFS),
    naturalnie nie nadazamy z tempem - n_sent bedzie mniejszy niz
    rate_fps*duration_s, co odzwierciedla realny sufit przepustowosci."""
    interval = 1.0 / rate_fps
    test_frame = build_frame(TEST_CAN_ID, b"\xAA" * 8)

    n_sent = 0
    n_enobufs = 0
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
        else:
            n_enobufs += 1
        next_send += interval

    actual_elapsed = time.perf_counter() - start
    return n_sent, n_enobufs, actual_elapsed


def wait_for_result(ser, timeout_s=1.5):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        line = ser.readline()
        if not line:
            continue
        s = line.decode(errors="replace").strip()
        if s.startswith("RESULT,"):
            parts = s.split(",")
            return int(parts[1]), int(parts[2])
    return None


def run_one_rate(sock, ser, rate_fps, duration_s):
    ser.reset_input_buffer()
    sock.send(build_frame(CONTROL_CAN_ID, bytes([CTRL_RESET])))
    time.sleep(0.05)

    n_sent, n_enobufs, actual_elapsed = generate_traffic(sock, rate_fps, duration_s)

    time.sleep(0.2)  # daj ESP32 czas dogonic ewentualny backlog przed STOP

    result = None
    for attempt in range(5):
        sock.send(build_frame(CONTROL_CAN_ID, bytes([CTRL_STOP])))
        result = wait_for_result(ser, timeout_s=0.8)
        if result is not None:
            break
    if result is None:
        return None

    n_rcvd, overflow_events = result
    actual_rate = n_sent / actual_elapsed if actual_elapsed > 0 else 0.0
    flr = (1.0 - n_rcvd / n_sent) * 100.0 if n_sent > 0 else 0.0
    return {
        "target_rate_fps": rate_fps,
        "actual_rate_fps": round(actual_rate, 1),
        "n_sent": n_sent,
        "n_enobufs": n_enobufs,
        "n_rcvd": n_rcvd,
        "overflow_events": overflow_events,
        "flr_percent": round(flr, 3),
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir")
    ap.add_argument("bitrate", type=int, help="bitrate CAN w bps, np. 250000 lub 500000")
    ap.add_argument("--duration", type=float, default=2.0, help="czas trwania kazdego kroku [s]")
    args = ap.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    sock.bind((CAN_IFACE,))

    ser = serial.Serial(SERIAL_PORT, BAUD, timeout=0.3)
    time.sleep(0.5)

    rates = list(range(RATE_START, RATE_STOP + 1, RATE_STEP))
    print(f"Eksperyment 2.1 — bitrate={args.bitrate}bps, {len(rates)} krokow "
          f"({RATE_START}-{RATE_STOP} ramek/s, krok {RATE_STEP}), "
          f"{args.duration}s/krok...")

    rows = []
    for i, rate in enumerate(rates):
        res = run_one_rate(sock, ser, rate, args.duration)
        if res is None:
            print(f"  [UWAGA] Brak odpowiedzi ESP32 dla {rate} ramek/s — pomijam")
            continue
        res["bitrate_bps"] = args.bitrate
        rows.append(res)
        print(f"  [{i+1}/{len(rates)}] target={rate:5d}fps actual={res['actual_rate_fps']:7.1f}fps "
              f"sent={res['n_sent']:5d} rcvd={res['n_rcvd']:5d} "
              f"FLR={res['flr_percent']:6.2f}% overflow_ev={res['overflow_events']}")

    ser.close()
    sock.close()

    raw_path = out_dir / "raw_data.csv"
    with open(raw_path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["bitrate_bps", "target_rate_fps", "actual_rate_fps",
                                          "n_sent", "n_enobufs", "n_rcvd",
                                          "overflow_events", "flr_percent"])
        w.writeheader()
        for r in rows:
            w.writerow(r)
    print(f"  OK {raw_path}")

    fig, ax = plt.subplots(figsize=(10, 6), facecolor=SURFACE)
    ax.set_facecolor(SURFACE)
    xs = [r["target_rate_fps"] for r in rows]
    ys = [r["flr_percent"] for r in rows]
    color = COLOR_250K if args.bitrate <= 250000 else COLOR_500K
    ax.plot(xs, ys, color=color, linewidth=1.8, marker="o", markersize=3,
            label=f"{args.bitrate // 1000}kbps")
    ax.set_xlabel("Natężenie ruchu [ramek/s] (target)", fontsize=11, color=INK_SECONDARY)
    ax.set_ylabel("Frame Loss Rate [%]", fontsize=11, color=INK_SECONDARY)
    ax.set_title(
        f"Eksperyment 2.1 — CAN Frame Throughput @ {args.bitrate // 1000}kbps",
        fontsize=12, fontweight="bold", color=INK, pad=12)
    ax.legend(loc="upper left", fontsize=10, facecolor=SURFACE, edgecolor=GRIDLINE)
    ax.grid(color=GRIDLINE, linewidth=0.8, zorder=0)
    ax.set_axisbelow(True)
    for spine in ["top", "right"]:
        ax.spines[spine].set_visible(False)
    for spine in ["left", "bottom"]:
        ax.spines[spine].set_color(GRIDLINE)
    ax.tick_params(colors=INK_SECONDARY)
    fig.tight_layout()
    chart_path = out_dir / "flr_chart.png"
    fig.savefig(chart_path, dpi=200, facecolor=SURFACE)
    plt.close(fig)
    print(f"  OK {chart_path}")

    # Prog: pierwszy target_rate przy ktorym FLR > 0.1%
    threshold = next((r["target_rate_fps"] for r in rows if r["flr_percent"] > 0.1), None)
    max_actual = max((r["actual_rate_fps"] for r in rows), default=0)

    lines = [
        "=" * 60,
        f"EKSPERYMENT 2.1 — CAN Frame Throughput @ {args.bitrate}bps",
        "MagistralaCAN4 + esp_experiment_2_1.ino (ESP32 + MCP2515)",
        "=" * 60,
        f"Zakres: {RATE_START}-{RATE_STOP} ramek/s, krok {RATE_STEP}, "
        f"{args.duration}s/krok, {len(rows)} punktow pomiarowych",
        "",
        f"Pierwszy prog utraty ramek (FLR>0.1%): "
        f"{threshold if threshold else 'brak w zbadanym zakresie'} ramek/s",
        f"Maksymalna faktycznie osiagnieta czestotliwosc nadawania (actual_rate_fps): "
        f"{max_actual:.1f} ramek/s",
        "",
        "Wg formuly: FLR = (1 - N_rcvd/N_sent) * 100%",
        "N_sent = ramki faktycznie wyslane przez generator (sukces send(),",
        "         nie liczac ramek odrzuconych lokalnie przez ENOBUFS - te",
        "         nigdy nie trafily na magistrale, wiec nie licza sie jako 'wyslane').",
        "N_rcvd = ramki bezblednie odebrane i policzone przez ESP32+MCP2515.",
    ]
    report_path = out_dir / "report.txt"
    with open(report_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"  OK {report_path}")
    print("Gotowe.")


if __name__ == "__main__":
    main()
