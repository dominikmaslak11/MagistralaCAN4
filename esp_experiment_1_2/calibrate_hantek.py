#!/usr/bin/env python3
"""Kalibracja Eksperymentu 1.2 na Hantek 1008C (tryb burst + wyzwalacz sprzetowy).
Wysyla JEDNA ramke CAN 0x123 i sprawdza, czy przechwycone okno zawiera
sensowne zbocza na obu kanalach (CH1=CAN RX, CH2=GPIO ESP32) - przed
pelnym przebiegiem N=1000 (run_experiment_1_2_hantek.py).

Kanaly Hantek sa 0-indeksowane w API (patrz hantek1008_driver/hantek1008.py:
"channel_id/channel_index are zero based, channel names are one based") -
channel=0 odpowiada opisowi "CH1" na obudowie, channel=1 to "CH2".
"""
import logging
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "hantek1008_driver"))
from hantek1008 import Hantek1008Raw  # noqa: E402
# Hantek1008 (wersja z konwersja na wolty) NIE przyjmuje trigger_channel/slope/level
# (Hantek1008.__init__ nie przekazuje ich do Hantek1008Raw.__init__) - potrzebny
# wyzwalacz sprzetowy, wiec uzywamy warstwy Raw (surowe wartosci ADC 12-bit).
# Do wykrywania zbocza wystarczy prog wzgledny (min+max)/2, wiec brak kalibracji
# na wolty nie ma znaczenia dla pomiaru czasu.

logging.basicConfig(level=logging.WARNING)

CH_CAN_RX = 0       # "CH1" na obudowie
CH_GPIO_RESP = 1    # "CH2" na obudowie
NS_PER_DIV = 5000   # 5us/div * 80 div = 400us okno, 200ns/probke - z zapasem
                    # wobec zmierzonego wczesniej t_resp ~110us (max 115us)
SAMPLES_PER_DIV = 25
TOTAL_DIVS = 80
SAMPLE_PERIOD_US = (NS_PER_DIV / SAMPLES_PER_DIV) / 1000.0

TRIGGER_LEVEL = 2048  # polowa zakresu 12-bit ADC - prog do kalibracji na realnym sygnale
CAN_IFACE = "can0"
TRIGGER_FRAME = "123#DEADBEEF11223344"


def find_edges_voltage(values, threshold, edge="falling"):
    edges = []
    for i in range(1, len(values)):
        if edge == "falling" and values[i - 1] >= threshold > values[i]:
            edges.append(i)
        elif edge == "rising" and values[i - 1] < threshold <= values[i]:
            edges.append(i)
    return edges


def main():
    dev = Hantek1008Raw(ns_per_div=NS_PER_DIV, active_channels=[CH_CAN_RX, CH_GPIO_RESP],
                         trigger_channel=CH_CAN_RX, trigger_slope="falling",
                         trigger_level=TRIGGER_LEVEL)
    dev.connect()
    dev.init()
    try:
        def send_frame_delayed():
            time.sleep(0.05)
            subprocess.run(["cansend", CAN_IFACE, TRIGGER_FRAME], check=False)

        t = threading.Thread(target=send_frame_delayed)
        t.start()
        data = dev.request_samples_burst_mode()
        t.join()

        ch0 = data[CH_CAN_RX]
        ch1 = data[CH_GPIO_RESP]
        print(f"Probek: CH1(RX)={len(ch0)}, CH2(GPIO)={len(ch1)}")
        print(f"Okno calkowite: {NS_PER_DIV * TOTAL_DIVS / 1000:.1f} us, "
              f"rozdzielczosc: {SAMPLE_PERIOD_US * 1000:.0f} ns/probke")
        print(f"CH1 zakres (surowe ADC): [{min(ch0)}, {max(ch0)}]")
        print(f"CH2 zakres (surowe ADC): [{min(ch1)}, {max(ch1)}]")

        mid0 = (min(ch0) + max(ch0)) / 2
        mid1 = (min(ch1) + max(ch1)) / 2
        ch0_falling = find_edges_voltage(ch0, mid0, "falling")
        ch1_rising = find_edges_voltage(ch1, mid1, "rising")
        print(f"CH1 zbocza opadajace (indeks probki): {ch0_falling[:10]}")
        print(f"CH2 zbocza rosnace   (indeks probki): {ch1_rising[:10]}")

        if ch0_falling and ch1_rising:
            d_samples = ch1_rising[0] - ch0_falling[0]
            d_us = d_samples * SAMPLE_PERIOD_US
            print(f"Delta: {d_samples} probek => {d_us:.2f} us")
        else:
            print("BRAK oczekiwanych zboczy - sprawdz podlaczenie kanalow / prog wyzwalania / poziom sygnalu.")
    finally:
        dev.close()


if __name__ == "__main__":
    main()
