#!/usr/bin/env python3
"""
PCAN CAN frame sender — wysyla ramki CAN przez PCAN_USBBUS2.
MagistralaCAN4.exe powinien nasluchiwac na PCAN_USBBUS1.

Obie magistrale muszą być połączone fizycznie (CANH-CANH, CANL-CANL)
lub przez wspólną szynę z rezystorem terminującym 120Ω po każdym końcu.
"""

import can
import time
import sys

CHANNEL = "PCAN_USBBUS2"   # kanał nadający (zmień na USBBUS1 jeśli odwrócone)
BITRATE = 500000            # 500 kbps — zmień jeśli używasz innej prędkości

FRAMES = [
    # (arbitration_id, is_extended, data_bytes)
    (0x123, False, [0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88]),
    (0x456, False, [0xAA, 0xBB, 0xCC, 0xDD]),
    (0x7FF, False, [0x01, 0x02, 0x03]),
    (0x18DA00F1, True, [0x02, 0x10, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00]),  # UDS extended
    (0x321, False, [0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02, 0x03]),
]

def main():
    print(f"Laczenie z {CHANNEL} @ {BITRATE // 1000} kbps...")
    try:
        bus = can.interface.Bus(
            interface="pcan",
            channel=CHANNEL,
            bitrate=BITRATE,
        )
    except can.CanError as e:
        print(f"BLAD: Nie mozna otworzyc {CHANNEL}: {e}")
        print("Sprawdz czy urzadzenie jest podlaczone i sterowniki PEAK sa zainstalowane.")
        sys.exit(1)

    print(f"Polaczono. Wysylam ramki co 1s (Ctrl+C aby zatrzymac)...\n")

    cycle = 0
    try:
        while True:
            for arb_id, is_ext, data in FRAMES:
                msg = can.Message(
                    arbitration_id=arb_id,
                    is_extended_id=is_ext,
                    data=data,
                )
                try:
                    bus.send(msg)
                    id_str = f"0x{arb_id:08X}" if is_ext else f"0x{arb_id:03X}"
                    data_str = " ".join(f"{b:02X}" for b in data)
                    print(f"  TX [{id_str}] DLC={len(data)}  {data_str}")
                except can.CanError as e:
                    print(f"  BLAD wysylania: {e}")
                time.sleep(0.2)

            cycle += 1
            print(f"--- cykl {cycle} ---")
            time.sleep(0.6)

    except KeyboardInterrupt:
        print("\nZatrzymano przez uzytkownika.")
    finally:
        bus.shutdown()
        print("Zamknieto kanal CAN.")

if __name__ == "__main__":
    main()
