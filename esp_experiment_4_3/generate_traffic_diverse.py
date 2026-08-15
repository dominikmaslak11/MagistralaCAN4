#!/usr/bin/env python3
"""
Eksperyment 4.3 (Etap A) — rozbudowany, PARAMETRYZOWANY generator syntetycznego
ruchu CAN, tworzacy WIELE roznych "mini-DBC" (konfiguracji CAN ID + sygnalow),
zamiast jednego stalego zestawu jak w esp_experiment_4_1/generate_traffic.py.

PO CO: fine-tuning (Eksperyment 4.3, Etap D) na obecnym, waskim zestawie
(3 CAN ID, 1 przypadek flag bitowych) nauczylby model rozpoznawac TE
KONKRETNE ramki, nie ogolna zasade — potrzebny duzo szerszy, zroznicowany
korpus, zeby odroznic generalizacje od zapamietywania (patrz
Eksperyment_4.3_Propozycja_Bootstrapped_FineTuning_20260728.md, sekcja 3).

ZROZNICOWANIE wzgledem oryginalnego generatora 4.1:
  - flagi bitowe w ROZNYCH pozycjach bajtu (nie tylko bajt 0)
  - ROZNA liczba flag w bajcie (2-6 — pamietajac, ze nasz klasyfikator
    Kierunku B wymaga 2-6 niezaleznie przelaczajacych sie bitow)
  - flagi ROZPROSZONE na wielu bajtach tej samej ramki
  - bajty MIESZANE: czesc bitow to flagi, czesc to mniejszy "podskalar"
    (czesciowe wykorzystanie bajtu — czesty, realistyczny wzorzec w
    prawdziwych DBC, nietestowany dotychczas w tym projekcie)
  - skalary o roznych zakresach/dynamikach zmian (szybkie/wolne,
    oscylujace/monotoniczne/skokowe)
  - rozne CAN ID, DLC, czestotliwosci ramek

DWA TRYBY DZIALANIA:
  1. `--dump-json <plik>` — CZYSTA SYMULACJA, bez sprzetu/SocketCAN. Generuje
     N konfiguracji + probki ramek w czasie i zapisuje wraz ze znanym ground
     truth do JSON. To jest tryb do budowy korpusu treningowego (Etap A) —
     szybki, darmowy, w pelni reprodukowalny (ziarno losowosci ustalone).
  2. `--iface can0` (jak w oryginalnym generatorze 4.1) — wysyla ruch NA ZYWO
     przez SocketCAN, do uzycia z prawdziwym sprzetem (ESP32+MCP2515) w
     przyszlych testach ewaluacyjnych/harnessem C++.

Format ground truth kompatybilny ze schematem juz uzywanym w
`DecodingAccuracyRunner` (C++): name, byteIdx, byteLen, littleEndian,
isSigned, bitMask, scale, offset — WAZNE: `bitMask` w istniejacym kodzie C++
(`LlmSignalRule::decode()`) juz obsluguje OGOLNIE maski wielobitowe (maskowanie
+ przesuniecie do LSB), nie tylko pojedyncze bity — dzieki temu "podskalary"
(czesciowe wykorzystanie bajtu) NIE wymagaja zmian w istniejacym kodzie C++,
tylko w logice dopasowania dla sygnalow dyskretnych (`findMatchingRule()`),
ktora obecnie zaklada TYLKO pojedyncze bity dla sygnalow dyskretnych — tutaj
(Etap A) klasyfikujemy podskalary jako CIAGLE (isDiscrete=False), co dziala
juz dzis bez zmian w C++ (ocena RMSE zamiast F1) - decyzja projektowa, nie
ograniczenie fundamentalne.
"""
import argparse
import json
import math
import random
import socket
import struct
import time
from dataclasses import dataclass, field, asdict
from typing import Optional

CAN_FRAME_FMT = "=IB3x8s"


def build_frame(can_id, data):
    data = bytes(data).ljust(8, b"\x00")[:8]
    return struct.pack(CAN_FRAME_FMT, can_id, 8, data)


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


# ── Definicje sygnalow ──────────────────────────────────────────────────────

DYNAMICS_CHOICES = ["randwalk_slow", "randwalk_fast", "sine", "exp_settle", "step_rare"]


@dataclass
class ScalarSignal:
    """Sygnal ciagly zajmujacy CALY bajt/bajty (byte_len=1 lub 2)."""
    name: str
    byte_idx: int
    byte_len: int  # 1 lub 2
    little_endian: bool = True
    is_signed: bool = False
    scale: float = 1.0
    offset: float = 0.0
    lo: float = 0.0       # fizyczny zakres wartosci (do symulacji)
    hi: float = 100.0
    dynamics: str = "randwalk_slow"


@dataclass
class PartialScalarSignal:
    """Sygnal ciagly zajmujacy PODZBIOR bitow jednego bajtu (np. bity 3-6) —
    wspolistnieje z flagami bitowymi w tym samym bajcie. Klasyfikowany jako
    CIAGLY (nie dyskretny) w ground truth, mimo malego zakresu wartosci —
    patrz uwaga w docstring modulu."""
    name: str
    byte_idx: int
    bit_lo: int   # najnizszy bit zajmowany (0-7)
    bit_hi: int   # najwyzszy bit zajmowany (0-7), wlacznie
    scale: float = 1.0
    offset: float = 0.0
    dynamics: str = "randwalk_slow"

    @property
    def bit_mask(self) -> int:
        mask = 0
        for b in range(self.bit_lo, self.bit_hi + 1):
            mask |= (1 << b)
        return mask

    @property
    def max_raw(self) -> int:
        return (1 << (self.bit_hi - self.bit_lo + 1)) - 1


@dataclass
class BitFlagSignal:
    """Pojedyncza, niezaleznie przelaczajaca sie flaga bitowa."""
    name: str
    byte_idx: int
    bit_idx: int
    toggle_lo_s: float = 3.0
    toggle_hi_s: float = 20.0
    initial: int = 0


@dataclass
class CanIdConfig:
    can_id: int
    dlc: int
    period_s: float
    scalars: list = field(default_factory=list)          # list[ScalarSignal]
    partial_scalars: list = field(default_factory=list)   # list[PartialScalarSignal]
    flags: list = field(default_factory=list)             # list[BitFlagSignal]

    def all_signal_names(self):
        return ([s.name for s in self.scalars]
                + [s.name for s in self.partial_scalars]
                + [f.name for f in self.flags])


# ── Generowanie zroznicowanych konfiguracji ─────────────────────────────────

def _make_bitflag_group(byte_idx, n_flags, name_prefix, rng):
    """Tworzy n_flags (2-6) niezaleznych flag w jednym bajcie, na losowo
    wybranych, nie nakladajacych sie pozycjach bitow."""
    assert 2 <= n_flags <= 6, "klasyfikator Kierunku B wymaga 2-6 bitow"
    bit_positions = rng.sample(range(8), n_flags)
    flags = []
    for i, bit in enumerate(sorted(bit_positions)):
        flags.append(BitFlagSignal(
            name=f"{name_prefix}_bit{bit}",
            byte_idx=byte_idx,
            bit_idx=bit,
            toggle_lo_s=rng.uniform(2.0, 5.0),
            toggle_hi_s=rng.uniform(10.0, 25.0),
            initial=rng.randint(0, 1),
        ))
    return flags, set(bit_positions)


def make_diverse_configs(seed: int = 42, n_configs: int = 30) -> list:
    """Generuje N zroznicowanych konfiguracji CAN ID. Deterministyczne przy
    ustalonym ziarnie (reprodukowalnosc korpusu)."""
    rng = random.Random(seed)
    configs = []
    base_id = 0x300  # inny zakres niz oryginalny generator 4.1 (0x100-0x200),
                      # zeby unikaly kolizji przy ewentualnym wspolnym uzyciu

    for i in range(n_configs):
        can_id = base_id + i * 0x10
        dlc = rng.choice([4, 6, 8])
        period_s = rng.choice([0.02, 0.05, 0.1, 0.2])

        scalars = []
        partial_scalars = []
        flags = []

        used_bytes = set()
        pattern = rng.choice([
            "pure_scalars", "pure_flags_one_byte", "flags_spread_multi_byte",
            "mixed_byte", "scalars_plus_flags",
        ])

        if pattern == "pure_scalars":
            # 2-3 sygnaly ciagle o roznych dynamikach, zadne flagi - kontrola
            # negatywna (test czy klasyfikator NIE fałszywie wykrywa flag)
            n_sig = rng.randint(2, 3)
            b = 0
            for s in range(n_sig):
                blen = rng.choice([1, 2])
                if b + blen > dlc:
                    break
                lo, hi = sorted([rng.uniform(-100, 0), rng.uniform(0, 4000)])
                scalars.append(ScalarSignal(
                    name=f"scalar_{i}_{s}", byte_idx=b, byte_len=blen,
                    is_signed=rng.choice([True, False]) if blen == 2 else False,
                    scale=rng.choice([0.1, 0.25, 1.0, 0.5]),
                    offset=rng.choice([0.0, -40.0]),
                    lo=lo, hi=hi, dynamics=rng.choice(DYNAMICS_CHOICES),
                ))
                used_bytes.update(range(b, b + blen))
                b += blen

        elif pattern == "pure_flags_one_byte":
            # WIELE flag w JEDNYM bajcie, liczba flag zmienna 2-6 (kluczowy
            # przypadek testowy — analogiczny do 0x200 z Eksperymentu 4.1,
            # ale z ROZNA liczba flag, nie zawsze 5)
            byte_idx = rng.randint(0, dlc - 1)
            n_flags = rng.randint(2, 6)
            grp, _ = _make_bitflag_group(byte_idx, n_flags, f"flag_{i}", rng)
            flags.extend(grp)
            used_bytes.add(byte_idx)

        elif pattern == "flags_spread_multi_byte":
            # Flagi ROZPROSZONE na 2 roznych bajtach tej samej ramki
            n_groups = 2
            candidate_bytes = rng.sample(range(dlc), min(n_groups, dlc))
            for gi, byte_idx in enumerate(candidate_bytes):
                n_flags = rng.randint(2, 4)
                grp, _ = _make_bitflag_group(byte_idx, n_flags, f"flag_{i}_{gi}", rng)
                flags.extend(grp)
                used_bytes.add(byte_idx)

        elif pattern == "mixed_byte":
            # Bajt MIESZANY: czesc bitow flagi, czesc podskalar - realistyczny,
            # dotad nietestowany wzorzec (np. bity 0-1 flagi, bity 2-5 podskalar
            # 4-bitowy, bit 6-7 nieuzywane/rezerwa)
            byte_idx = rng.randint(0, dlc - 1)
            n_flag_bits = rng.randint(2, 3)
            flag_bits = list(range(n_flag_bits))
            grp = []
            for bit in flag_bits:
                grp.append(BitFlagSignal(
                    name=f"mixedflag_{i}_bit{bit}", byte_idx=byte_idx, bit_idx=bit,
                    toggle_lo_s=rng.uniform(2.0, 5.0), toggle_hi_s=rng.uniform(10.0, 25.0),
                    initial=rng.randint(0, 1),
                ))
            flags.extend(grp)
            remaining_bits = 8 - n_flag_bits
            sub_width = rng.randint(3, min(5, remaining_bits))
            bit_lo = n_flag_bits
            bit_hi = bit_lo + sub_width - 1
            partial_scalars.append(PartialScalarSignal(
                name=f"mixedsub_{i}", byte_idx=byte_idx, bit_lo=bit_lo, bit_hi=bit_hi,
                scale=rng.choice([1.0, 2.0, 0.5]), offset=0.0,
                dynamics=rng.choice(["randwalk_slow", "step_rare"]),
            ))
            used_bytes.add(byte_idx)

        else:  # scalars_plus_flags
            # Ramka laczy sygnaly ciagle NA INNYCH bajtach niz flagi (test czy
            # klasyfikator poprawnie odroznia oba typy w tej samej ramce)
            flag_byte = rng.randint(0, dlc - 1)
            n_flags = rng.randint(2, 5)
            grp, _ = _make_bitflag_group(flag_byte, n_flags, f"flag_{i}", rng)
            flags.extend(grp)
            used_bytes.add(flag_byte)

            free_bytes = [b for b in range(dlc) if b not in used_bytes]
            if len(free_bytes) >= 1:
                b = free_bytes[0]
                blen = 2 if len(free_bytes) >= 2 and free_bytes[1] == b + 1 else 1
                lo, hi = sorted([rng.uniform(-50, 0), rng.uniform(0, 1000)])
                scalars.append(ScalarSignal(
                    name=f"scalar_{i}", byte_idx=b, byte_len=blen,
                    scale=rng.choice([0.1, 1.0, 0.5]),
                    offset=0.0, lo=lo, hi=hi,
                    dynamics=rng.choice(DYNAMICS_CHOICES),
                ))

        configs.append(CanIdConfig(
            can_id=can_id, dlc=dlc, period_s=period_s,
            scalars=scalars, partial_scalars=partial_scalars, flags=flags,
        ))

    return configs


# ── Symulator ────────────────────────────────────────────────────────────────

class DiverseSim:
    """Ogolny symulator dla dowolnej listy CanIdConfig — generalizacja
    VehicleSim z esp_experiment_4_1/generate_traffic.py na dowolna liczbe/
    rodzaj sygnalow."""

    def __init__(self, configs: list, seed: int = 123):
        self.configs = configs
        self.rng = random.Random(seed)
        self.t0 = time.perf_counter()
        self.state = {}  # (can_id, signal_name) -> biezaca wartosc fizyczna
        self.flag_state = {}  # (can_id, signal_name) -> 0/1
        self.next_toggle = {}
        self.next_send = {}

        for cfg in configs:
            self.next_send[cfg.can_id] = 0.0
            for s in cfg.scalars:
                self.state[(cfg.can_id, s.name)] = self.rng.uniform(s.lo, s.hi)
            for ps in cfg.partial_scalars:
                self.state[(cfg.can_id, ps.name)] = self.rng.uniform(0, ps.max_raw)
            for f in cfg.flags:
                self.flag_state[(cfg.can_id, f.name)] = f.initial
                self.next_toggle[(cfg.can_id, f.name)] = (
                    time.perf_counter() + self.rng.uniform(f.toggle_lo_s, f.toggle_hi_s))

    def _step_scalar_value(self, v, lo, hi, dynamics, t):
        if dynamics == "randwalk_slow":
            v += self.rng.uniform(-1, 1) * (hi - lo) * 0.01
        elif dynamics == "randwalk_fast":
            v += self.rng.uniform(-1, 1) * (hi - lo) * 0.05
        elif dynamics == "sine":
            mid = (lo + hi) / 2.0
            amp = (hi - lo) / 2.0
            v = mid + amp * math.sin(t * self.rng.uniform(0.2, 1.0))
        elif dynamics == "exp_settle":
            v = hi - (hi - lo) * math.exp(-t / 60.0) + self.rng.uniform(-1, 1) * (hi - lo) * 0.005
        elif dynamics == "step_rare":
            if self.rng.random() < 0.002:
                v = self.rng.uniform(lo, hi)
        return clamp(v, lo, hi)

    def step(self):
        t = time.perf_counter() - self.t0
        now = time.perf_counter()
        for cfg in self.configs:
            for s in cfg.scalars:
                key = (cfg.can_id, s.name)
                self.state[key] = self._step_scalar_value(self.state[key], s.lo, s.hi, s.dynamics, t)
            for ps in cfg.partial_scalars:
                key = (cfg.can_id, ps.name)
                self.state[key] = self._step_scalar_value(self.state[key], 0, ps.max_raw, ps.dynamics, t)
            for f in cfg.flags:
                key = (cfg.can_id, f.name)
                if now >= self.next_toggle[key]:
                    self.flag_state[key] = 1 - self.flag_state[key]
                    self.next_toggle[key] = now + self.rng.uniform(f.toggle_lo_s, f.toggle_hi_s)

    def build_frame_for(self, cfg: CanIdConfig):
        data = [0] * 8
        for s in cfg.scalars:
            phys = self.state[(cfg.can_id, s.name)]
            raw = int(round((phys - s.offset) / s.scale))
            mask = (1 << (8 * s.byte_len)) - 1
            raw &= mask
            byte_order = range(s.byte_len) if s.little_endian else range(s.byte_len - 1, -1, -1)
            for shift_i, i in enumerate(byte_order):
                data[s.byte_idx + i] = (raw >> (8 * shift_i)) & 0xFF
        for ps in cfg.partial_scalars:
            phys = self.state[(cfg.can_id, ps.name)]
            raw = int(round((phys - ps.offset) / ps.scale)) & ps.max_raw
            data[ps.byte_idx] |= (raw << ps.bit_lo) & ps.bit_mask
        for f in cfg.flags:
            bit_val = self.flag_state[(cfg.can_id, f.name)]
            if bit_val:
                data[f.byte_idx] |= (1 << f.bit_idx)
        return build_frame(cfg.can_id, data)


# ── Serializacja ground truth (dla Etapu B/C) ───────────────────────────────

def ground_truth_json(configs: list) -> list:
    out = []
    for cfg in configs:
        entry = {"can_id": cfg.can_id, "dlc": cfg.dlc, "period_s": cfg.period_s, "signals": []}
        for s in cfg.scalars:
            d = asdict(s)
            d["kind"] = "scalar"
            d["is_discrete"] = False
            entry["signals"].append(d)
        for ps in cfg.partial_scalars:
            d = asdict(ps)
            d["kind"] = "partial_scalar"
            d["is_discrete"] = False
            d["bit_mask"] = ps.bit_mask
            entry["signals"].append(d)
        for f in cfg.flags:
            d = asdict(f)
            d["kind"] = "bit_flag"
            d["is_discrete"] = True
            entry["signals"].append(d)
        out.append(entry)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iface", default=None, help="np. can0 - jesli podane, wysyla NA ZYWO przez SocketCAN")
    ap.add_argument("--duration", type=float, default=3600.0, help="czas trwania [s]")
    ap.add_argument("--n-configs", type=int, default=30, help="liczba roznych konfiguracji CAN ID do wygenerowania")
    ap.add_argument("--seed", type=int, default=42, help="ziarno losowosci konfiguracji (reprodukowalnosc)")
    ap.add_argument("--dump-json", default=None,
                     help="tryb symulacji offline (bez sprzetu) - zapisuje ground truth + probki ramek do JSON")
    ap.add_argument("--period-scale", type=float, default=1.0,
                     help="mnoznik okresow nadawania (1.0 = bez zmian). Skaluje "
                          "CZESTOTLIWOSC PROBKOWANIA wzgledem dynamiki sygnalow, "
                          "wiec realnie zmienia statystyki per-bajt (changed_pairs/"
                          "big_jumps) -- w odroznieniu od predkosci magistrali czy "
                          "liczby CAN ID, ktore ich nie zmieniaja (Eksperymenty 4.8/4.9). "
                          "NIE wplywa na definicje sygnalow ani ground truth.")
    ap.add_argument("--dump-samples-per-id", type=int, default=40,
                     help="w trybie --dump-json: ile probek ramek na CAN ID zapisac (do budowy korpusu)")
    args = ap.parse_args()

    configs = make_diverse_configs(seed=args.seed, n_configs=args.n_configs)
    print(f"Wygenerowano {len(configs)} zroznicowanych konfiguracji CAN ID "
          f"(ziarno={args.seed}).")
    if args.period_scale != 1.0:
        # Skalujemy WYLACZNIE okresy nadawania -- definicje sygnalow i ground
        # truth pozostaja identyczne dla danego ziarna.
        for cfg in configs:
            cfg.period_s *= args.period_scale
        print(f"Okresy nadawania przeskalowane x{args.period_scale} "
              f"(zakres: {min(c.period_s for c in configs):.4f}-"
              f"{max(c.period_s for c in configs):.4f} s).")
    pattern_counts = {}
    for cfg in configs:
        n_flags = len(cfg.flags)
        n_scalars = len(cfg.scalars) + len(cfg.partial_scalars)
        key = f"flags={n_flags},scalars={n_scalars}"
        pattern_counts[key] = pattern_counts.get(key, 0) + 1
    print("Rozklad typow konfiguracji:", pattern_counts)

    sim = DiverseSim(configs, seed=args.seed + 1000)

    if args.dump_json:
        # Tryb symulacji offline - bez sprzetu, do budowy korpusu (Etap A/B)
        samples = {cfg.can_id: [] for cfg in configs}
        target_total = args.dump_samples_per_id * len(configs)
        collected = 0
        step_count = 0
        # Symulujemy czas "sztucznie" (przyspieszony) - nie czekamy realnie,
        # bo tu nie wysylamy nic na prawdziwa magistrale, tylko generujemy dane.
        fake_dt = 0.05
        while collected < target_total and step_count < target_total * 50:
            sim.t0 -= fake_dt  # przyspiesz uplyw "czasu" symulacji
            for key in list(sim.next_toggle.keys()):
                sim.next_toggle[key] -= fake_dt
            sim.step()
            for cfg in configs:
                if len(samples[cfg.can_id]) < args.dump_samples_per_id:
                    frame_bytes = sim.build_frame_for(cfg)
                    _, _, payload = struct.unpack(CAN_FRAME_FMT, frame_bytes)
                    samples[cfg.can_id].append(list(payload[:cfg.dlc]))
                    collected += 1
            step_count += 1

        out = {
            "seed": args.seed,
            "n_configs": len(configs),
            "ground_truth": ground_truth_json(configs),
            "samples": {str(k): v for k, v in samples.items()},
        }
        with open(args.dump_json, "w", encoding="utf-8") as f:
            json.dump(out, f, indent=2)
        print(f"Zapisano korpus offline: {args.dump_json} "
              f"({sum(len(v) for v in samples.values())} probek ramek, "
              f"{len(configs)} CAN ID).")
        return

    if not args.iface:
        print("Brak --iface i brak --dump-json - nic do zrobienia. "
              "Podaj jedno z nich.")
        return

    sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)
    sock.bind((args.iface,))

    for cfg in configs:
        sim.next_send[cfg.can_id] = 0.0

    print(f"Generator Eksperymentu 4.3 (zroznicowany) wystartowal na {args.iface}, "
          f"{len(configs)} CAN ID, czas trwania {args.duration:.0f}s. Ctrl+C aby zatrzymac.")

    start = time.perf_counter()
    n_frames = 0
    try:
        while time.perf_counter() - start < args.duration:
            now = time.perf_counter()
            sim.step()
            for cfg in configs:
                if now - sim.next_send[cfg.can_id] >= cfg.period_s:
                    try:
                        sock.send(sim.build_frame_for(cfg))
                        n_frames += 1
                    except OSError:
                        pass
                    sim.next_send[cfg.can_id] = now
            time.sleep(0.002)
    except KeyboardInterrupt:
        pass
    finally:
        sock.close()
        print(f"Generator zatrzymany. Wyslano {n_frames} ramek.")


if __name__ == "__main__":
    main()
