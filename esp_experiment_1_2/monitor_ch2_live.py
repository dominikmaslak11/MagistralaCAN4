#!/usr/bin/env python3
"""Szybki podglad na zywo CH2 (channel=1) Hantek 1008C - surowe probki ADC (12-bit).
Cel: sprawdzic, czy generator sygnalu podlaczony do CH2 (+ wspolna masa) jest
faktycznie widoczny, zanim przejdziemy do wlasciwej kalibracji.

Tryb roll (ciagly), 440 probek/s, tylko kanal 0 aktywny.
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "hantek1008_driver"))
from hantek1008 import Hantek1008Raw  # noqa: E402

CH2 = 1
DURATION_SEC = 5.0
SAMPLING_RATE = 440


def main():
    dev = Hantek1008Raw(active_channels=[CH2])
    dev.connect()
    dev.init()
    try:
        gen = dev.request_samples_roll_mode(sampling_rate=SAMPLING_RATE)
        start = time.monotonic()
        all_values = []
        print(f"Zbieram probki CH2 przez {DURATION_SEC:.0f}s (probkowanie {SAMPLING_RATE} Sa/s)...")
        while time.monotonic() - start < DURATION_SEC:
            chunk = next(gen)[CH2]
            all_values.extend(chunk)
            vmin, vmax = min(chunk), max(chunk)
            print(f"t={time.monotonic()-start:5.2f}s  n={len(chunk):4d}  "
                  f"min={vmin:5d}  max={vmax:5d}  swing={vmax-vmin:5d}  "
                  f"first10={chunk[:10]}")
        gen.close()

        print()
        print(f"=== PODSUMOWANIE ({len(all_values)} probek) ===")
        print(f"Zakres calkowity: [{min(all_values)}, {max(all_values)}] (skala 12-bit ADC: 0-4095)")
        print(f"Rozpietosc (swing): {max(all_values) - min(all_values)} zliczen")
        if max(all_values) - min(all_values) < 50:
            print("=> WYGLADA NA SZUM: brak realnego sygnalu na CH2. Sprawdz sonde/mase.")
        else:
            print("=> WIDAC REALNY SYGNAL na CH2 (znaczacy swing ADC).")
    finally:
        dev.close()


if __name__ == "__main__":
    main()
