#!/usr/bin/env python3
"""Kalibracja/weryfikacja CH1 Hantek 1008C burst mode, wysoka rozdzielczosc czasowa,
konwersja na wolty przy uzyciu fabrycznej kalibracji EEPROM (klasa Hantek1008, nie Raw).

Cel: zmierzyc realny ksztalt/amplitude/czestotliwosc sygnalu podlaczonego do CH1
(np. wyjscie kompensacji sondy albo programowalny generator LVTTL), bez aliasingu
(w przeciwienstwie do trybu roll uzytego w monitor_ch1_live.py).
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "hantek1008_driver"))
from hantek1008 import Hantek1008  # noqa: E402

CH1 = 0
NS_PER_DIV = 1000  # 1us/div * 80div = 80us okno; sample_period = ns_per_div/25 = 40ns/probke


def find_edges(values, threshold):
    rising, falling = [], []
    for i in range(1, len(values)):
        if values[i - 1] < threshold <= values[i]:
            rising.append(i)
        elif values[i - 1] >= threshold > values[i]:
            falling.append(i)
    return rising, falling


def main():
    sample_period_us = (NS_PER_DIV / 25) / 1000.0
    dev = Hantek1008(ns_per_div=NS_PER_DIV, vertical_scale_factor=0.125, active_channels=[CH1])
    dev.connect()
    dev.init()
    try:
        data = dev.request_samples_burst_mode(mode="volt")
        ch = data[CH1]
        vmin, vmax = min(ch), max(ch)
        vmid = (vmin + vmax) / 2

        print(f"Liczba probek: {len(ch)}")
        print(f"Okno: {len(ch) * sample_period_us:.1f} us, rozdzielczosc: {sample_period_us*1000:.0f} ns/probke")
        print(f"CH1 zakres (wolty, wg fabrycznej kalibracji EEPROM): [{vmin:.4f} V, {vmax:.4f} V]")
        print(f"Vpp = {vmax - vmin:.4f} V")
        print(f"Pierwsze 20 probek [V]: {[round(v,3) for v in ch[:20]]}")

        rising, falling = find_edges(ch, vmid)
        print(f"Liczba zboczy rosnacych: {len(rising)}, opadajacych: {len(falling)}")

        if len(rising) >= 2:
            periods_us = [(rising[i+1] - rising[i]) * sample_period_us for i in range(len(rising)-1)]
            avg_period_us = sum(periods_us) / len(periods_us)
            print(f"Okresy miedzy zboczami rosnacymi [us]: {[round(p,2) for p in periods_us]}")
            print(f"Srednia czestotliwosc: {1000.0/avg_period_us:.2f} kHz "
                  f"(okres {avg_period_us:.3f} us)")
        elif len(rising) == 1 and len(falling) == 1:
            half_us = abs(falling[0] - rising[0]) * sample_period_us
            print(f"Widac tylko pol okresu w oknie (~{half_us:.2f} us) - zmniejsz ns_per_div "
                  f"zeby zobaczyc pelny okres, albo sygnal jest wolniejszy niz okno.")
        else:
            print("Brak wykrytych zboczy w oknie - sygnal moze byc stalym poziomem DC "
                  "w tym oknie czasowym, albo zbyt wolny wzgledem 80us okna.")
    finally:
        dev.close()


if __name__ == "__main__":
    main()
