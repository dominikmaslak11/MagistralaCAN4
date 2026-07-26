#!/usr/bin/env python3
"""Kalibracja: przechwycenie natychmiastowe (bez wyzwalacza) w trakcie
wyslania jednej ramki CAN 0x123 - weryfikuje mapowanie bitow kanalow
i realnosc sygnalow przed pelnym przebiegiem N=1000."""
import subprocess
import threading
import time
from atk_logic_driver import AtkLogicAnalyzer, channel_bits, find_edges

SAMPLE_RATE = 10_000_000  # 10 MHz -> 100ns/probka
DEPTH = 1024 * 50         # 51200 probek = 5.12 ms okno, wielokrotnosc 1024 (pelne bloki)

az = AtkLogicAnalyzer()
try:
    az.connect_handshake()
    az.wake_fpga()
    az.configure_capture(SAMPLE_RATE, DEPTH, trigger_position_percent=10, threshold_volts=1.65)
    az.arm_trigger(channel=0, edge="none", instantly=True, enabled_channels={0, 1})

    def send_frame_delayed():
        time.sleep(0.5)
        subprocess.run(["cansend", "can0", "123#DEADBEEF11223344"], check=False)

    t = threading.Thread(target=send_frame_delayed)
    t.start()

    per_channel = az.read_capture(timeout_s=15)
    t.join()

    ch0 = channel_bits(per_channel, 0)
    ch1 = channel_bits(per_channel, 1)

    print(f"Kanaly odebrane w strumieniu: {sorted(per_channel.keys())}")
    print(f"CH0 probek: {len(ch0)}, CH1 probek: {len(ch1)}")
    print(f"CH0 - unikalne wartosci: {set(ch0)}, ile 1: {sum(ch0)}")
    print(f"CH1 - unikalne wartosci: {set(ch1)}, ile 1: {sum(ch1)}")

    ch0_falling = find_edges(ch0, "falling")
    ch1_rising = find_edges(ch1, "rising")
    ch1_falling = find_edges(ch1, "falling")
    print(f"CH0 falling edges (probka #): {ch0_falling[:10]}")
    print(f"CH1 rising edges (probka #):  {ch1_rising[:10]}")
    print(f"CH1 falling edges (probka #): {ch1_falling[:10]}")

    if ch0_falling and ch1_rising:
        d = ch1_rising[0] - ch0_falling[0]
        print(f"Delta (probki): {d}  =>  {d / SAMPLE_RATE * 1e6:.2f} us")
    else:
        print("BRAK oczekiwanych zboczy - sprawdz podlaczenie kanalow / mapowanie bitow.")

    # Zrzut surowych pierwszych bajtow CH0 do recznej inspekcji
    ch0_raw = per_channel.get(0, b"")
    print("Pierwsze 16 bajtow CH0 (hex):", ch0_raw[:16].hex())

finally:
    az.stop()
    az.close()
