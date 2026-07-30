#!/usr/bin/env python3
"""Diagnostyka watchdoga (2026-07-30) - jak run_experiment_5_1.py, ale:
- loguje KAZDA surowa linie z serial do pliku (nie tylko RESULT,...)
- NIE przerywa dzialania na bledzie parsowania (kontynuuje, zeby zlapac
  pelny, wielolinijkowy raport task_wdt z lista zaglodzonych zadan)
"""
import socket
import struct
import time
import random
import errno

SERIAL_PORT = "/dev/ttyUSB0"
BAUD = 115200
CAN_IFACE = "can0"
CONTROL_CAN_ID = 0x7FE
CTRL_RESET, CTRL_REPORT, CTRL_SET_MODE = 0x01, 0x03, 0x04
MODE_IDLE, MODE_PARSING = 0, 1
TRAFFIC_IDS = [0x100, 0x150, 0x200]
CAN_FRAME_FMT = "=IB3x8s"
LOG_PATH = "traces/watchdog_full_raw_log_20260730.txt"

import serial


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
    interval = 1.0 / rate_hz
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
        send_or_skip(sock, build_frame(can_id, payload))
        next_send += interval


def main():
    sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    sock.bind((CAN_IFACE,))
    ser = serial.Serial(SERIAL_PORT, BAUD, timeout=0.3)
    time.sleep(0.5)

    with open(LOG_PATH, "w", encoding="utf-8") as logf:
        def log(line):
            ts = time.strftime("%H:%M:%S")
            logf.write(f"[{ts}] {line}\n")
            logf.flush()
            print(line)

        log(f"=== start diagnostyki, {time.ctime()} ===")

        window_s, rate_hz = 3.0, 50.0
        n_per_state = 30  # wiecej niz oryginalne 20, zeby na pewno zlapac watchdog

        for label, mode in [("IDLE", MODE_IDLE), ("PARSING", MODE_PARSING)]:
            log(f"--- Stan {label}, do {n_per_state} pomiarow ---")
            for i in range(n_per_state):
                ser.reset_input_buffer()
                sock.send(build_frame(CONTROL_CAN_ID, bytes([CTRL_RESET])))
                time.sleep(0.05)
                sock.send(build_frame(CONTROL_CAN_ID, bytes([CTRL_SET_MODE, mode])))
                time.sleep(0.05)
                generate_traffic(sock, rate_hz, window_s)
                time.sleep(0.1)
                sock.send(build_frame(CONTROL_CAN_ID, bytes([CTRL_REPORT])))

                deadline = time.time() + 2.0
                got_result = False
                while time.time() < deadline:
                    line = ser.readline()
                    if not line:
                        continue
                    s = line.decode(errors="replace").rstrip()
                    if s:
                        log(f"  RAW: {s}")
                    if s.startswith("RESULT,"):
                        got_result = True
                        break
                if not got_result:
                    log(f"  [{label} {i+1}/{n_per_state}] BRAK RESULT w oknie 2s")
                else:
                    log(f"  [{label} {i+1}/{n_per_state}] OK")

        log("=== koniec diagnostyki (limit pomiarow osiagniety bez re-crashu) ===")

    ser.close()
    sock.close()


if __name__ == "__main__":
    main()
