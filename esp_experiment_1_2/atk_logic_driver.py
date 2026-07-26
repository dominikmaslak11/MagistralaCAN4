#!/usr/bin/env python3
"""
Minimalny sterownik Linux (libusb/pyusb) dla analizatora stanow logicznych
ATK-Logic (Alientek), USB VID:PID = 1a86:ffcc.

Protokol zrekonstruowany z oficjalnego zrodla GPL (github.com/alientek-openedv/atk-logic,
pv/usb/usb_base.cpp, usb_control.cpp, usb_server.cpp, pv/static/util.h) -
NIE jest to oficjalne oprogramowanie producenta, to niezalezna reimplementacja
warstwy przechwytywania (bez GUI/dekodowania protokolow), napisana pod katem
automatyzacji Eksperymentu 1.2 (Hot Execution Latency, N=1000 pomiarow).

Warstwa "B" (komendy FPGA/przechwytywanie) uzywana tutaj:
  0x11 ParameterSetting, 0x12 SimpleTrigger, 0x15 Stop.
Ramka wychodzaca: [8x0x00][0x0A][code][origLen][payload][0x0B][CRC32 LE]
                  dopelniona zerami do wielokrotnosci 2048B, nastepnie
                  "shuffle" (patrz shuffle_out/deinterleave_in nizej).
"""
import struct
import time
import usb.core
import usb.util


def _make_crc_table():
    table = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (0xEDB88320 ^ (c >> 1)) if (c & 1) else (c >> 1)
        table.append(c)
    return table


_CRC_TABLE = _make_crc_table()


def atk_crc32(data: bytes) -> int:
    """CRC32 niestandardowy uzywany przez ATK-Logic (gCRC32 w pv/static/util.cpp):
    tabela identyczna jak standardowe CRC-32 (poly 0xEDB88320), ALE rejestr
    startuje od 0 (nie od 0xFFFFFFFF jak w zlib.crc32) — inny wynik niz zlib!"""
    crc = 0
    for b in data:
        crc = _CRC_TABLE[(crc ^ b) & 0xFF] ^ (crc >> 8)
    return (crc ^ 0xFFFFFFFF) & 0xFFFFFFFF

VENDOR_ID = 0x1a86
PRODUCT_ID = 0xffcc
EP_OUT = 0x02
EP_IN = 0x81
BLOCK = 2048
WORDS_PER_BLOCK = BLOCK // 2      # 1024
WORDS_PER_LANE = WORDS_PER_BLOCK // 4  # 256

CMD_PARAMETER_SETTING = 0x11
CMD_GET_DEVICE_DATA = 0x10
CMD_SIMPLE_TRIGGER = 0x12
CMD_STOP = 0x15

# Tabela indeksow czestotliwosci probkowania (selectHzIndex -> Hz)
SAMPLE_RATE_TABLE = [
    1_000_000, 2_000_000, 4_000_000, 5_000_000, 10_000_000,
    20_000_000, 25_000_000, 40_000_000, 50_000_000, 100_000_000,
]

TRIG_ENABLE_EVEN = 0x80
TRIG_RISING_EVEN = 0x10
TRIG_HIGH_EVEN = 0x40
TRIG_FALLING_EVEN = 0x20
TRIG_ENABLE_ODD = 0x08
TRIG_RISING_ODD = 0x01
TRIG_HIGH_ODD = 0x04
TRIG_FALLING_ODD = 0x02


def deinterleave_block(block: bytes) -> bytes:
    """Wejscie: surowy blok 2048B z urzadzenia (4 'lane' po 512B/256 slow).
    Wyjscie: logiczna kolejnosc slow: dla j=0..255: lane0[j],lane1[j],lane2[j],lane3[j]."""
    assert len(block) == BLOCK
    lanes = [block[i * 512:(i + 1) * 512] for i in range(4)]
    lane_words = [struct.unpack(f"<{WORDS_PER_LANE}H", lane) for lane in lanes]
    out = []
    for j in range(WORDS_PER_LANE):
        for lane in range(4):
            out.append(lane_words[lane][j])
    return struct.pack(f"<{WORDS_PER_BLOCK}H", *out)


def shuffle_block(words: list) -> bytes:
    """Odwrotnosc deinterleave_block: z logicznej listy 1024 slow buduje
    surowy blok 2048B (4 lane po 256 slow), tak jak oczekuje urzadzenie."""
    assert len(words) == WORDS_PER_BLOCK
    lanes = [[0] * WORDS_PER_LANE for _ in range(4)]
    for j in range(WORDS_PER_LANE):
        for lane in range(4):
            lanes[lane][j] = words[4 * j + lane]
    out = b""
    for lane in lanes:
        out += struct.pack(f"<{WORDS_PER_LANE}H", *lane)
    return out


class AtkLogicAnalyzer:
    def __init__(self):
        dev = usb.core.find(idVendor=VENDOR_ID, idProduct=PRODUCT_ID)
        if dev is None:
            raise RuntimeError(f"Urzadzenie {VENDOR_ID:04x}:{PRODUCT_ID:04x} nie znalezione")
        try:
            if dev.is_kernel_driver_active(0):
                dev.detach_kernel_driver(0)
        except (NotImplementedError, usb.core.USBError):
            pass
        try:
            dev.reset()
            time.sleep(0.3)
        except usb.core.USBError:
            pass
        dev.set_configuration()
        usb.util.claim_interface(dev, 0)
        self.dev = dev

    def close(self):
        """Przed zwolnieniem interfejsu wysyla SetResetState(0) (sleep_fpga) -
        obserwacja fizyczna na sprzecie: dioda LED urzadzenia odzwierciedla
        stan SetResetState (zielona=aktywne/1, czerwona=spoczynek/0).
        wake_fpga() (SetResetState(1)) jest wolany przed kazdym przechwyceniem,
        wiec bez tego kroku dioda zostawala zielona na zawsze po close() -
        oficjalna aplikacja usypia FPGA (dioda -> czerwona) przy rozlaczaniu."""
        try:
            self.sleep_fpga()
        except Exception:
            pass
        try:
            usb.util.release_interface(self.dev, 0)
        except Exception:
            pass

    def _send_to_mcu(self, code: int, extra: bytes = b""):
        """Warstwa 'A' (SendToMCU w usb_control.cpp): surowy bufor 512B BEZ
        przeplotu/CRC — buf[0]=0x0A, buf[1]=code, buf[2:]=extra, TERMINATOR
        0x0B zaraz po extra, reszta zer.

        Poprzednia wersja NIE dopisywala tego 0x0B - zweryfikowane snifferem
        USB (usbmon/tshark) na dzialajacej sesji oficjalnej aplikacji: GetMCUVersion
        (code=0x81) idzie na druty jako `0a 81 0b 00...`, SetResetState(0/1)
        (code=0x87) jako `0a 87 00 0b 00...` / `0a 87 01 0b 00...` - MCU nigdy
        nie odpowiadal na nasze ramki bez tego terminatora (odczyt z EP IN
        zawsze timeoutowal, 0 bajtow)."""
        buf = bytearray(512)
        buf[0] = 0x0A
        buf[1] = code
        buf[2:2 + len(extra)] = extra
        buf[2 + len(extra)] = 0x0B
        self.dev.clear_halt(EP_OUT)
        self.dev.write(EP_OUT, bytes(buf), timeout=2000)

    def _send_to_mcu_and_read(self, code: int, extra: bytes = b"",
                               attempts: int = 6, read_timeout_ms: int = 100):
        """GetMCUVersion/SetResetState + odczyt odpowiedzi z retry, analogicznie
        do ConnectDevice::CheckDeviceCreanInfo() (connect.cpp) - oficjalna
        aplikacja rowniez potrzebuje do 6 prob (widoczne w jej wlasnym logu:
        '第N次尝试连接' / 'MCU信息失败' powtarzane, zanim padnie odpowiedz).
        Pierwsze proby czesto dostaja tylko smieciowe wypelnienie 0xFF z EP IN
        (FPGA/MCU jeszcze nie gotowe) - filtrujemy je i traktujemy jak brak
        odpowiedzi, ponawiajac cala sekwencje wyslij+czytaj."""
        last_err = None
        for _ in range(attempts):
            self._send_to_mcu(code, extra)
            self.dev.clear_halt(EP_IN)
            try:
                data = bytes(self.dev.read(EP_IN, 512, timeout=read_timeout_ms))
                if data and data != b"\xff" * len(data) and data[0] == 0x0A:
                    return data
                last_err = f"odpowiedz odrzucona (smiec/puste): {data[:8].hex() if data else '<brak>'}"
            except usb.core.USBError as e:
                last_err = str(e)
            time.sleep(0.05)
        raise TimeoutError(f"Brak poprawnej odpowiedzi po {attempts} probach ({code:#04x}): {last_err}")

    def wake_fpga(self):
        """SetResetState(1) — 'wybudzenie FPGA' (Session::fpgaActive() w
        session.cpp). WYMAGANE przed ParameterSetting/SimpleTrigger, inaczej
        urzadzenie nigdy nie zaczyna strumieniowac danych przechwycenia."""
        self._send_to_mcu(0x87, bytes([1]))
        time.sleep(0.05)

    def sleep_fpga(self):
        self._send_to_mcu(0x87, bytes([0]))

    def _drain(self, max_reads=50, chunk=2048, timeout_ms=100):
        """Analog petli 'while(usb->ReadSynchronous(data)) ...' - wyczyszczenie
        wszelkich zalegajacych/starych danych z bulk IN przed nowa operacja."""
        for _ in range(max_reads):
            try:
                self.dev.clear_halt(EP_IN)
                self.dev.read(EP_IN, chunk, timeout=timeout_ms)
            except usb.core.USBError:
                break

    def connect_handshake(self):
        """Pelna sekwencja polaczenia z ConnectDevice::CheckDeviceCreanInfo()
        w connect.cpp: GetMCUVersion -> impuls resetu (0 potem 1) -> drain ->
        GetDeviceData (odczyt wersji FPGA) -> drain -> KONCOWY SetResetState(0)
        (connect.cpp linia ~174, po walidacji FPGA, tuz przed emitowaniem
        sukcesu polaczenia). To ostatnie (0) jest KRYTYCZNE: zostawia FPGA w
        stanie spoczynku, tak by kolejne wywolanie wake_fpga()=SetResetState(1)
        w Session::fpgaActive() (session.cpp:415, wywolywane na poczatku
        KAZDEGO nowego przechwycenia) bylo realnym zboczem 0->1, a nie
        powtorzeniem tej samej wartosci. Bez tego kroku FPGA nigdy nie
        zaczynala strumieniowac danych (USBTimeoutError przy kazdej probie) -
        zweryfikowane empirycznie na sprzecie."""
        self._send_to_mcu(0x81)  # GetMCUVersion
        self._drain(max_reads=20, timeout_ms=50)

        self._send_to_mcu(0x87, bytes([0]))  # SetResetState(0)
        self._send_to_mcu(0x87, bytes([1]))  # SetResetState(1) - impuls resetu
        time.sleep(0.05)
        self._drain(max_reads=50, timeout_ms=50)

        self._write_layer_b(CMD_GET_DEVICE_DATA, b"")  # odczyt wersji FPGA
        self._drain(max_reads=5, timeout_ms=200)

        time.sleep(0.03)
        self._send_to_mcu(0x87, bytes([0]))  # SetResetState(0) koncowy - patrz docstring
        self._drain(max_reads=20, timeout_ms=50)

    def _write_layer_b(self, code: int, payload: bytes):
        # Zgodnie z USBControl::Write(code,data,len) + Write(data,len) w usb_control.cpp:
        # "origLen" zapisany w ramce to (len(payload)+2)-1 = len(payload)+1, NIE len(payload).
        orig_len = (len(payload) + 1) & 0xFF
        core = bytes([code, orig_len]) + payload  # to jest dokladnie to, co C++ CRCuje
        crc = atk_crc32(core)
        # USBControl::Write(quint8*,qint32) w C++ alokuje len_inner+15 bajtow ale
        # zapisuje tylko len_inner+14 (offset 8 + 0x0A + core + 0x0B + CRC32(4)) -
        # ostatni bajt bufora zostaje niezapisany (czyli 0, z wczesniejszego memset).
        # Bez tego dodatkowego zera cala ramka jest przesunieta o 1B przy przeplocie.
        body = bytes([0x0A]) + core + bytes([0x0B]) + struct.pack("<I", crc) + bytes(1)
        frame = bytes(8) + body
        pad = (-len(frame)) % BLOCK
        frame += bytes(pad)

        out = bytearray()
        for i in range(0, len(frame), BLOCK):
            block = frame[i:i + BLOCK]
            words = list(struct.unpack(f"<{WORDS_PER_BLOCK}H", block))
            out += shuffle_block(words)

        # Bez clear_halt() PRZED KAZDYM zapisem endpoint OUT wiesza sie (ACK
        # przestaje przychodzic -> USBTimeoutError) na dowolnych niezerowych
        # bajtach — zweryfikowane empirycznie na prawdziwym sprzecie. Powod
        # nieznany (mozliwy quirk sterownika/firmware WCH), ale obejscie
        # jest w 100% powtarzalne.
        self.dev.clear_halt(EP_OUT)
        self.dev.write(EP_OUT, bytes(out), timeout=5000)

    def configure_capture(self, sample_rate_hz: int, depth_samples: int,
                           trigger_position_percent: int = 10,
                           threshold_volts: float = 1.65, rle: bool = False):
        if sample_rate_hz not in SAMPLE_RATE_TABLE:
            raise ValueError(f"Nieobslugiwana czestotliwosc: {sample_rate_hz}")
        hz_index = SAMPLE_RATE_TABLE.index(sample_rate_hz)

        flags = 0
        if rle:
            flags |= 0x40
        thresh_byte = min(127, round(abs(threshold_volts) * 10))
        if threshold_volts < 0:
            thresh_byte |= 0x80

        trigger_depth = depth_samples // 100 * trigger_position_percent

        payload = bytes([
            flags,
            thresh_byte,
            hz_index + 1,
        ]) + depth_samples.to_bytes(5, "little") + trigger_depth.to_bytes(5, "little")

        self._write_layer_b(CMD_PARAMETER_SETTING, payload)
        time.sleep(0.03)  # wymagane opoznienie ("ustabilizowanie napiecia FPGA")
        self._depth_samples = depth_samples
        self._sample_rate_hz = sample_rate_hz

    def arm_trigger_instant_all(self, num_channels: int = 16):
        """Replika ZWERYFIKOWANEJ na sniferze USB (usbmon/tshark, sesja dzialajacej
        oficjalnej aplikacji) ramki SimpleTrigger uzywanej przy 'Run' bez konkretnego
        wyzwalacza: payload = [0xFF]*num_pairs + [0x00, 0x00] (KONCOWKA 2 bajty, NIE
        1 jak zakladala wczesniejsza wersja arm_trigger() zrekonstruowana tylko ze
        zrodla C++ - empirycznie zaobserwowany real payload to
        `ffffffffffffffff0000` dla 16 kanalow/8 par). 0xFF na pare kanalow =
        TRIG_ENABLE|RISING|FALLING|HIGH dla obu (even+odd) - de facto 'przechwytuj
        wszystko, wyzwol natychmiast na dowolnej aktywnosci'. Po tej komendzie w
        realnej sesji dane zaczely plynac przez EP IN ~1.3ms pozniej."""
        num_pairs = (num_channels + 1) // 2
        payload = bytes([0xFF]) * num_pairs + bytes([0x00, 0x00])
        self._write_layer_b(CMD_SIMPLE_TRIGGER, payload)

    def arm_trigger(self, channel: int, edge: str, instantly: bool = False,
                     enabled_channels=None):
        """channel: kanal wyzwalajacy (0-indexed). edge: 'rising'|'falling'|'high'|'none'.
        enabled_channels: zbior indeksow WSZYSTKICH kanalow bioracych udzial w
        przechwyceniu (domyslnie tylko `channel`). KRYTYCZNE: w oryginalnym GUI
        (session_controller.cpp, SessionController::start(), petla po
        json["channelsSet"]) bit ENABLE (0x80/0x08) jest ustawiany dla KAZDEGO
        kanalu z osobna flaga "enable" w UI - NIEZALEZNIE od tego, czy dla tego
        kanalu wybrano konkretny typ wyzwalacza czy 'brak'/'instant'. Wczesniejsza
        wersja tego kodu ustawiala bity TYLKO gdy edge!='none', wiec przy
        wywolaniu arm_trigger(edge='none', instantly=True) (przechwycenie
        natychmiastowe, jak w calibrate.py) caly payload SimpleTrigger byl
        samymi zerami - FPGA nie mial ani jednego kanalu oznaczonego jako
        aktywny i nigdy nie zaczynal strumieniowac danych (USBTimeoutError)."""
        if enabled_channels is None:
            enabled_channels = {channel}
        else:
            enabled_channels = set(enabled_channels) | {channel}
        num_pairs = max(enabled_channels) // 2 + 1
        pair_bytes = [0] * num_pairs
        for ch in enabled_channels:
            pair_idx = ch // 2
            is_even = (ch % 2) == 0
            bits = TRIG_ENABLE_EVEN if is_even else TRIG_ENABLE_ODD
            if ch == channel and edge in ("rising", "falling", "high"):
                if edge == "rising":
                    bits |= TRIG_RISING_EVEN if is_even else TRIG_RISING_ODD
                elif edge == "falling":
                    bits |= TRIG_FALLING_EVEN if is_even else TRIG_FALLING_ODD
                elif edge == "high":
                    bits |= TRIG_HIGH_EVEN if is_even else TRIG_HIGH_ODD
            pair_bytes[pair_idx] |= bits

        payload = bytes(pair_bytes) + bytes([1 if instantly else 0])
        self._write_layer_b(CMD_SIMPLE_TRIGGER, payload)

    def read_capture(self, timeout_s: float = 10.0, idle_reads_to_stop: int = 8):
        """Czyta strumien bulk IN BEZ wysylania GetDeviceData (0x10) - w oryginale
        (pv/thread/thread_read.cpp, ThreadRead::start) po SimpleTrigger host od razu
        zaczyna wywolywac usb->Read(...) w petli; 0x10 sluzy WYLACZNIE do zapytania
        o wersje FPGA podczas connect_handshake(), nie do 'zadania danych' - a
        wyslanie go tutaj bylo druga (obok brakujacego SetResetState(0) w
        connect_handshake) przyczyna wiecznego USBTimeoutError.

        Zwraca dict {channel_id: bytes} - SUROWY, spakowany bitowo strumien
        probek per-kanal (patrz parse_analysis_frames / bits_from_packed_bytes),
        NIE liste 16-bit slow (to bylo bledne zalozenie poprzedniej wersji -
        realny format ramek to pv/data/analysis.cpp + pv/thread/thread_work.cpp
        case 1: [channelID:1][rezerwa:1][spakowane bity, 8 probek/bajt, LSB=
        najwczesniejsza probka - Segment::GetSample: bit = (byte>>(n%8))&1])."""
        self.dev.clear_halt(EP_IN)
        raw = bytearray()
        deadline = time.time() + timeout_s
        idle = 0
        while time.time() < deadline:
            try:
                chunk = self.dev.read(EP_IN, 16384, timeout=500)
                raw += bytes(chunk)
                idle = 0
            except usb.core.USBError:
                idle += 1
                if raw and idle >= idle_reads_to_stop:
                    break
        if not raw:
            raise TimeoutError("Timeout czekania na dane - nie odebrano ani jednego bajtu z EP IN")

        usable_len = (len(raw) // BLOCK) * BLOCK
        deinterleaved = bytearray()
        for i in range(0, usable_len, BLOCK):
            deinterleaved += deinterleave_block(bytes(raw[i:i + BLOCK]))

        per_channel = {}
        for order, payload in parse_analysis_frames(bytes(deinterleaved)):
            if order == 1 and len(payload) > 2:
                ch = payload[0]
                per_channel.setdefault(ch, bytearray()).extend(payload[2:])
        return {ch: bytes(b) for ch, b in per_channel.items()}

    def stop(self):
        try:
            self._write_layer_b(CMD_STOP, b"")
        except Exception:
            pass


def parse_analysis_frames(buf: bytes):
    """Port Analysis::getNextData() (pv/data/analysis.cpp) - skanuje bajt po
    bajcie szukajac ramek [0x0A][order:1][len:u16 LE][payload:len][0x00][0x0B],
    order w zakresie 1..6. Zwraca liste (order, payload_bytes)."""
    frames = []
    i, n = 0, len(buf)
    while i < n:
        if buf[i] == 0x0A and i + 4 <= n:
            order = buf[i + 1]
            if 0 < order < 7:
                length = buf[i + 2] | (buf[i + 3] << 8)
                end = i + 4 + length
                if length > 0 and end + 1 < n and buf[end] == 0x00 and buf[end + 1] == 0x0B:
                    frames.append((order, bytes(buf[i + 4:end])))
                    i = end + 2
                    continue
        i += 1
    return frames


def bits_from_packed_bytes(data: bytes):
    """Segment::GetSample (segment.cpp): bit = (byte >> (n % 8)) & 1, byte = n // 8
    -> probka 0 to bit0 (LSB, najwczesniejsza), rosnaco w obrebie bajtu."""
    bits = []
    for byte in data:
        for k in range(8):
            bits.append((byte >> k) & 1)
    return bits


def channel_bits(per_channel: dict, channel: int):
    """Zwraca liste bitow 0/1 dla danego kanalu z wyniku read_capture()."""
    return bits_from_packed_bytes(per_channel.get(channel, b""))


def find_edges(bits, edge="falling"):
    """Zwraca liste indeksow probek, w ktorych nastapilo zbocze."""
    edges = []
    for i in range(1, len(bits)):
        if edge == "falling" and bits[i - 1] == 1 and bits[i] == 0:
            edges.append(i)
        elif edge == "rising" and bits[i - 1] == 0 and bits[i] == 1:
            edges.append(i)
    return edges
