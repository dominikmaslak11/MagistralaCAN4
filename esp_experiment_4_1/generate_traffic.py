#!/usr/bin/env python3
"""
Eksperyment 4.1 — generator syntetycznego ruchu CAN (10 sygnalow / mini-DBC)
przez PEAK PCAN-USB (raw SocketCAN), do wykorzystania jako "ground truth"
w DecodingAccuracyRunner (C++, MagistralaCAN4).

KODOWANIE MUSI BYC IDENTYCZNE z groundTruthFor() w
src/core/DecodingAccuracyRunner.cpp — jesli zmieniasz cos tutaj, zmien tez tam.

CAN ID 0x100 "EngineData" (okres ~20ms):
  byte0-1 (LE, unsigned) RPM        raw = RPM / 0.25
  byte2   (unsigned)     CoolantTemp raw = Temp + 40      [-40..215 C]
  byte3   (unsigned)     Throttle   raw = Throttle / 0.4  [0..100 %]

CAN ID 0x150 "SteeringData" (okres ~50ms):
  byte0-1 (LE, SIGNED)   SteeringAngle raw = Angle / 0.1  [deg]
  byte2-3 (LE, unsigned) VehicleSpeed  raw = Speed / 0.01 [km/h]

CAN ID 0x200 "BodyStatus" (okres ~100ms):
  byte0 bit0 LeftIndicator, bit1 RightIndicator, bit2 Headlights,
        bit3 DriverDoor, bit4 Handbrake
"""
import socket
import struct
import time
import math
import random
import argparse

CAN_FRAME_FMT = "=IB3x8s"


def build_frame(can_id, data):
    data = bytes(data).ljust(8, b"\x00")[:8]
    return struct.pack(CAN_FRAME_FMT, can_id, 8, data)


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


class VehicleSim:
    def __init__(self):
        self.t0 = time.perf_counter()
        self.rpm = 900.0
        self.throttle = 5.0
        self.temp = 20.0
        self.speed = 0.0
        self.angle = 0.0
        self.discrete = {
            "left": 0, "right": 0, "headlights": 0, "door": 0, "handbrake": 1,
        }
        self._next_toggle = {k: time.perf_counter() + random.uniform(3, 15)
                              for k in self.discrete}

    def step(self):
        t = time.perf_counter() - self.t0

        # Throttle: powolny random-walk 0-80%
        self.throttle += random.uniform(-3, 3)
        self.throttle = clamp(self.throttle, 0.0, 80.0)

        # RPM: baza + wplyw throttle + szum
        target_rpm = 800.0 + self.throttle * 45.0
        self.rpm += (target_rpm - self.rpm) * 0.08 + random.uniform(-40, 40)
        self.rpm = clamp(self.rpm, 700.0, 6500.0)

        # Temperatura: krzywa nagrzewania silnika (stala czasowa ~5 min) do ~90C
        self.temp = 90.0 - (90.0 - 20.0) * math.exp(-t / 300.0) + random.uniform(-0.5, 0.5)
        self.temp = clamp(self.temp, -40.0, 130.0)

        # Predkosc: podaza za throttle/rpm z opoznieniem
        target_speed = clamp((self.rpm - 800.0) / 6500.0 * 140.0, 0.0, 140.0)
        self.speed += (target_speed - self.speed) * 0.05
        self.speed = clamp(self.speed, 0.0, 180.0)

        # Kat kierownicy: oscylacja sinusoidalna + losowe wieksze skrety
        self.angle = 60.0 * math.sin(t * 0.3) + 15.0 * math.sin(t * 1.7)
        self.angle = clamp(self.angle, -450.0, 450.0)

        # Stany dyskretne: kazdy przelacza sie niezaleznie w losowych momentach
        now = time.perf_counter()
        for k in self.discrete:
            if now >= self._next_toggle[k]:
                self.discrete[k] = 1 - self.discrete[k]
                self._next_toggle[k] = now + random.uniform(3, 20)

    def frame_0x100(self):
        raw_rpm = int(round(self.rpm / 0.25)) & 0xFFFF
        raw_temp = int(round(self.temp + 40.0)) & 0xFF
        raw_throttle = int(round(self.throttle / 0.4)) & 0xFF
        data = [raw_rpm & 0xFF, (raw_rpm >> 8) & 0xFF, raw_temp, raw_throttle, 0, 0, 0, 0]
        return build_frame(0x100, data)

    def frame_0x150(self):
        raw_angle = int(round(self.angle / 0.1))
        raw_angle &= 0xFFFF  # two's complement 16-bit
        raw_speed = int(round(self.speed / 0.01)) & 0xFFFF
        data = [raw_angle & 0xFF, (raw_angle >> 8) & 0xFF,
                raw_speed & 0xFF, (raw_speed >> 8) & 0xFF, 0, 0, 0, 0]
        return build_frame(0x150, data)

    def frame_0x200(self):
        byte0 = (self.discrete["left"] << 0) | (self.discrete["right"] << 1) \
              | (self.discrete["headlights"] << 2) | (self.discrete["door"] << 3) \
              | (self.discrete["handbrake"] << 4)
        data = [byte0, 0, 0, 0, 0, 0, 0, 0]
        return build_frame(0x200, data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iface", default="can0")
    ap.add_argument("--duration", type=float, default=3600.0,
                     help="czas trwania generatora [s], domyslnie 1h")
    args = ap.parse_args()

    sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    sock.bind((args.iface,))

    sim = VehicleSim()

    t_last_100 = 0.0
    t_last_150 = 0.0
    t_last_200 = 0.0
    period_100 = 0.020
    period_150 = 0.050
    period_200 = 0.100

    print(f"Generator ruchu Eksperymentu 4.1 wystartowal na {args.iface} "
          f"(0x100@50Hz, 0x150@20Hz, 0x200@10Hz), czas trwania {args.duration:.0f}s. "
          "Ctrl+C aby zatrzymac wczesniej.")

    start = time.perf_counter()
    n_frames = 0
    try:
        while time.perf_counter() - start < args.duration:
            now = time.perf_counter()
            sim.step()

            if now - t_last_100 >= period_100:
                sock.send(sim.frame_0x100())
                t_last_100 = now
                n_frames += 1
            if now - t_last_150 >= period_150:
                sock.send(sim.frame_0x150())
                t_last_150 = now
                n_frames += 1
            if now - t_last_200 >= period_200:
                sock.send(sim.frame_0x200())
                t_last_200 = now
                n_frames += 1

            time.sleep(0.002)
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()
        print(f"Generator zatrzymany. Wyslano {n_frames} ramek.")


if __name__ == "__main__":
    main()
