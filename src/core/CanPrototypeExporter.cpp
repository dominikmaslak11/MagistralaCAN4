#include "CanPrototypeExporter.h"
#include <QTextStream>
#include <QDateTime>
#include <algorithm>
#include <cmath>

// ── Helpers ───────────────────────────────────────────────────────────────────

QString CanPrototypeExporter::sanitize(const QString &s) {
    QString r;
    for (const QChar &c : s)
        r += (c.isLetterOrNumber() || c == '_') ? c : '_';
    if (!r.isEmpty() && r[0].isDigit()) r.prepend('_');
    return r.isEmpty() ? "unnamed" : r;
}

QString CanPrototypeExporter::hexId(uint32_t id) {
    return QString("0x%1").arg(id, 0, 16).toUpper();
}

QString CanPrototypeExporter::sampleHex(const FrameExportInfo &f) {
    QStringList parts;
    for (uint8_t b : f.sampleData)
        parts << QString("0x%1").arg(b, 2, 16, QChar('0')).toUpper();
    return parts.join(' ');
}

QString CanPrototypeExporter::pythonClassName(const FrameExportInfo &f) {
    if (!f.name.isEmpty() && !f.name.startsWith("0x"))
        return "Msg_" + sanitize(f.name);
    return QString("Msg_%1").arg(f.id, 0, 16).toUpper();
}

QString CanPrototypeExporter::luaVarName(const FrameExportInfo &f) {
    if (!f.name.isEmpty() && !f.name.startsWith("0x"))
        return "msg_" + sanitize(f.name).toLower();
    return QString("msg_%1").arg(hexId(f.id)).replace("0x","").toLower();
}

QString CanPrototypeExporter::cppVarName(const FrameExportInfo &f) {
    if (!f.name.isEmpty() && !f.name.startsWith("0x"))
        return sanitize(f.name).toLower();
    return QString("msg%1").arg(hexId(f.id)).replace("0x","").toLower();
}

// Signal comment block (shared across formats)
QString CanPrototypeExporter::sigComment(const DbcSignal &sig, int indent) {
    QString pad(indent, ' ');
    return QString("%1  %2 [startBit=%3, len=%4, %5, scale=%6, offset=%7]  unit: %8\n")
        .arg(pad)
        .arg(sig.name, -20)
        .arg(sig.startBit)
        .arg(sig.length)
        .arg(sig.isLittleEndian ? "LE" : "BE")
        .arg(sig.scale, 0, 'g')
        .arg(sig.offset, 0, 'g')
        .arg(sig.unit.isEmpty() ? "-" : sig.unit);
}

// Python: encode one signal into data[] (byte-aligned simple case, else generic)
QString CanPrototypeExporter::pythonEncodeSignal(const DbcSignal &sig, int indent) {
    QString pad(indent, ' ');
    QString out;
    QTextStream s(&out);

    int startByte = sig.startBit / 8;
    bool byteAligned = (sig.startBit % 8 == 0) && (sig.length % 8 == 0);
    int numBytes     = sig.length / 8;
    QString varName  = sanitize(sig.name).toLower();
    QString rawVar   = "_raw_" + varName;

    s << pad << "# " << sig.name;
    if (!sig.unit.isEmpty()) s << " [" << sig.unit << "]";
    s << ": startBit=" << sig.startBit << ", len=" << sig.length
      << ", " << (sig.isLittleEndian ? "LE" : "BE")
      << ", scale=" << sig.scale << ", offset=" << sig.offset << "\n";

    if (byteAligned && sig.isLittleEndian && numBytes <= 4) {
        int maxRaw = (1 << sig.length) - 1;
        s << pad << rawVar << " = max(0, min(" << maxRaw
          << ", round((self." << varName << " - " << sig.offset
          << ") / " << sig.scale << ")))\n";
        for (int b = 0; b < numBytes; ++b)
            s << pad << "data[" << (startByte + b) << "] = (" << rawVar
              << " >> " << (b * 8) << ") & 0xFF\n";
    } else {
        // Generic bit-level packing via helper
        s << pad << "# complex signal — using _pack_bits helper\n";
        s << pad << "_pack_bits(data, " << sig.startBit << ", " << sig.length
          << ", self." << varName << ", " << sig.scale << ", " << sig.offset
          << ", " << (sig.isLittleEndian ? "True" : "False") << ")\n";
    }
    return out;
}

// Arduino: encode one signal (byte-aligned only, rest as comment)
QString CanPrototypeExporter::arduinoEncodeSignal(const DbcSignal &sig) {
    QString out;
    int startByte    = sig.startBit / 8;
    bool byteAligned = (sig.startBit % 8 == 0) && (sig.length % 8 == 0);
    int  numBytes    = sig.length / 8;
    QString varName  = sanitize(sig.name).toLower();

    out += QString("    // %1").arg(sig.name);
    if (!sig.unit.isEmpty()) out += " [" + sig.unit + "]";
    out += QString(": startBit=%1, len=%2, scale=%3, offset=%4\n")
               .arg(sig.startBit).arg(sig.length).arg(sig.scale).arg(sig.offset);

    if (byteAligned && sig.isLittleEndian && numBytes >= 1 && numBytes <= 4) {
        int    maxRaw  = (1 << sig.length) - 1;
        QString dtype  = numBytes == 1 ? "uint8_t" : (numBytes == 2 ? "uint16_t" : "uint32_t");
        out += QString("    %1 raw_%2 = (%1)constrain((%3 - %4f) / %5f, 0, %6);\n")
                   .arg(dtype, varName, varName,
                        QString::number(sig.offset), QString::number(sig.scale),
                        QString::number(maxRaw));
        for (int b = 0; b < numBytes; ++b)
            out += QString("    txData[%1] = (raw_%2 >> %3) & 0xFF;\n")
                       .arg(startByte + b).arg(varName).arg(b * 8);
    } else {
        out += QString("    packBits(txData, %1, %2, %3, %4f, %5f, %6);\n")
                   .arg(sig.startBit).arg(sig.length).arg(varName)
                   .arg(sig.scale).arg(sig.offset)
                   .arg(sig.isLittleEndian ? "true" : "false");
    }
    return out;
}

// ── generate() dispatcher ─────────────────────────────────────────────────────

QString CanPrototypeExporter::generate(const QVector<FrameExportInfo> &frames,
                                       const Config &cfg) {
    switch (cfg.format) {
        case Format::Python:  return generatePython (frames, cfg);
        case Format::Lua:     return generateLua    (frames, cfg);
        case Format::Arduino: return generateArduino(frames, cfg);
    }
    return {};
}

// ── Python generator ──────────────────────────────────────────────────────────

QString CanPrototypeExporter::generatePython(const QVector<FrameExportInfo> &frames,
                                              const Config &cfg) {
    QString out;
    QTextStream s(&out);

    int periodic = 0;
    for (const auto &f : frames) if (f.periodMs > 0.0) ++periodic;

    // ── File header ──────────────────────────────────────────────────────
    s << "#!/usr/bin/env python3\n"
         "# -*- coding: utf-8 -*-\n"
         "\"\"\"\n"
         "CAN Module Prototype\n"
         "Generated by MagistralaCAN4  ("
      << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm") << ")\n\n"
      << "Detected messages : " << frames.size() << "\n"
      << "  periodic        : " << periodic << "\n"
      << "  non-periodic    : " << (frames.size() - periodic) << "\n\n"
         "Requirements:  pip install python-can\n\n"
         "Usage:\n"
         "  python3 can_prototype.py\n\n"
         "Arduino note:\n"
         "  For Arduino + MCP2515, use the Arduino C++ export option instead.\n"
         "\"\"\"\n\n"
         "import can\n"
         "import time\n"
         "import threading\n\n";

    // ── Config ────────────────────────────────────────────────────────────
    s << "# ─── Bus configuration ────────────────────────────────────────────────────\n"
      << "# Adapt BUS_TYPE / CHANNEL to your hardware:\n"
      << "#   socketcan  — Linux SocketCAN (can0, vcan0, ...)\n"
      << "#   pcan       — PEAK PCAN USB (PCAN_USBBUS1, ...)\n"
      << "#   usb2can    — 8devices USB2CAN\n"
      << "#   kvaser     — Kvaser interfaces\n"
      << "#   slcan      — Serial-line CAN (COM port / /dev/ttyUSB0)\n"
      << "BUS_TYPE = \"" << cfg.busType << "\"\n"
      << "CHANNEL  = \"" << cfg.channel << "\"\n"
      << "BITRATE  = " << cfg.bitrate << "\n\n";

    // ── Generic bit-pack helper ───────────────────────────────────────────
    bool needsPack = false;
    for (const auto &f : frames)
        for (const auto &sig : f.sigList)
            if ((sig.startBit % 8 != 0) || (sig.length % 8 != 0)) { needsPack = true; break; }

    if (needsPack) {
        s << "def _pack_bits(data: list, start_bit: int, length: int,\n"
             "               value: float, scale: float, offset: float,\n"
             "               little_endian: bool) -> None:\n"
             "    \"\"\"Generic signal packer (Intel/Motorola byte order).\"\"\"\n"
             "    raw = max(0, min((1 << length) - 1, round((value - offset) / scale)))\n"
             "    if little_endian:\n"
             "        for i in range(length):\n"
             "            b, bit = divmod(start_bit + i, 8)\n"
             "            if 0 <= b < len(data):\n"
             "                data[b] = (data[b] | (((raw >> i) & 1) << bit)) & 0xFF\n"
             "    else:\n"
             "        bit = start_bit\n"
             "        for i in range(length):\n"
             "            b, bpos = bit // 8, 7 - (bit % 8)\n"
             "            if 0 <= b < len(data):\n"
             "                data[b] = (data[b] | (((raw >> (length-1-i)) & 1) << bpos)) & 0xFF\n"
             "            bit = bit - 1 if (bit % 8) else bit + 15\n\n\n";
    }

    // ── One class per message ─────────────────────────────────────────────
    for (const auto &f : frames) {
        QString cls = pythonClassName(f);
        s << "class " << cls << ":\n";

        // docstring
        s << "    \"\"\"\n"
          << "    ID: " << hexId(f.id)
          << "  |  Name: " << f.name
          << "  |  DLC: " << f.dlc << "\n";
        if (f.periodMs > 0.0)
            s << "    Heartbeat period: " << f.periodMs << " ms"
              << (f.jitterMs > 0.0 ? QString("  (jitter ±%1 ms)").arg(f.jitterMs, 0,'f',2) : "")
              << "\n";
        else
            s << "    Non-periodic / event-driven\n";

        if (!f.sigList.isEmpty()) {
            s << "\n    DBC Signals:\n";
            for (const auto &sig : f.sigList)
                s << sigComment(sig, 4);
        }
        if (!f.counterBytes.isEmpty()) {
            s << "\n    Auto-detected patterns:\n";
            for (int b : f.counterBytes)
                s << "      Byte " << b << ": rolling counter (0x00 → 0xFF, wraps)\n";
        }
        if (!f.sampleData.isEmpty())
            s << "\n    Sample payload: " << sampleHex(f) << "\n";
        s << "    \"\"\"\n";

        // Class variables
        s << "    ID     = " << hexId(f.id) << "\n"
          << "    DLC    = " << f.dlc << "\n"
          << "    EXT    = " << (f.extended ? "True" : "False") << "\n";
        if (f.periodMs > 0.0)
            s << "    PERIOD = " << QString::number(f.periodMs / 1000.0, 'f', 6)
              << "  # " << f.periodMs << " ms\n";
        s << "\n";

        // __init__
        s << "    def __init__(self):\n";
        if (!f.sigList.isEmpty()) {
            for (const auto &sig : f.sigList) {
                QString varName = sanitize(sig.name).toLower();
                double defVal   = sig.offset;
                s << "        self." << varName << " = " << defVal;
                if (!sig.unit.isEmpty())
                    s << "    # " << sig.unit << "  — " << sig.name;
                s << "\n";
            }
        } else {
            // Raw byte array fallback
            s << "        self.data = [";
            for (int b = 0; b < std::min(f.dlc, (int)f.sampleData.size()); ++b) {
                if (b) s << ", ";
                s << QString("0x%1").arg(f.sampleData[b], 2, 16, QChar('0')).toUpper();
            }
            s << "]\n";
        }
        for (int cb : f.counterBytes)
            s << "        self._ctr_byte" << cb << " = 0x00  # rolling counter → byte " << cb << "\n";
        s << "\n";

        // encode()
        s << "    def encode(self) -> \"list[int]\":\n"
             "        \"\"\"Pack current signal values into payload bytes.\"\"\"\n"
             "        data = list(bytearray(self.DLC))\n";
        if (!f.sigList.isEmpty()) {
            for (const auto &sig : f.sigList)
                s << pythonEncodeSignal(sig, 8);
        } else {
            s << "        data = list(self.data)[:self.DLC]  # raw bytes — no DBC loaded\n";
        }
        for (int cb : f.counterBytes) {
            s << "        data[" << cb << "] = self._ctr_byte" << cb << "  # rolling counter\n"
              << "        self._ctr_byte" << cb << " = (self._ctr_byte" << cb << " + 1) & 0xFF\n";
        }
        s << "        return data\n\n";

        // build_message()
        s << "    def build_message(self) -> \"can.Message\":\n"
             "        return can.Message(\n"
             "            arbitration_id=self.ID,\n"
             "            data=self.encode(),\n"
             "            is_extended_id=self.EXT,\n"
             "        )\n\n\n";
    }

    // ── _periodic helper ──────────────────────────────────────────────────
    if (periodic > 0) {
        s << "def _periodic(msg_obj, bus: \"can.BusABC\", stop: threading.Event) -> None:\n"
             "    \"\"\"Background thread: transmit msg_obj at PERIOD until stop is set.\"\"\"\n"
             "    while not stop.is_set():\n"
             "        try:\n"
             "            bus.send(msg_obj.build_message())\n"
             "        except can.CanError as exc:\n"
             "            print(f\"[TX error] {exc}\")\n"
             "        stop.wait(msg_obj.PERIOD)\n\n\n";
    }

    // ── main() ────────────────────────────────────────────────────────────
    s << "def main() -> None:\n"
         "    bus  = can.Bus(interface=BUS_TYPE, channel=CHANNEL, bitrate=BITRATE)\n"
         "    stop = threading.Event()\n\n"
         "    # ── Instantiate and configure messages ───────────────────────────────\n";

    for (const auto &f : frames) {
        QString cls  = pythonClassName(f);
        QString var  = cls.toLower().replace("msg_","obj_");
        s << "    " << var << " = " << cls << "()\n";
        for (const auto &sig : f.sigList) {
            QString sv = sanitize(sig.name).toLower();
            s << "    " << var << "." << sv << " = " << sig.offset
              << "  # " << (sig.unit.isEmpty() ? sig.name : sig.unit) << "\n";
        }
    }
    s << "\n";

    if (periodic > 0) {
        s << "    # ── Start periodic transmit threads ─────────────────────────────────\n"
             "    threads = [\n";
        for (const auto &f : frames) {
            if (f.periodMs <= 0.0) continue;
            QString cls = pythonClassName(f);
            QString var = cls.toLower().replace("msg_","obj_");
            s << "        threading.Thread(target=_periodic, args=(" << var
              << ", bus, stop), daemon=True),\n";
        }
        s << "    ]\n"
             "    for t in threads:\n"
             "        t.start()\n\n";
    }

    // Optional receive loop
    if (cfg.withReceiver) {
        s << "    # ── Optional receive loop ───────────────────────────────────────────\n"
             "    def rx_loop():\n"
             "        while not stop.is_set():\n"
             "            msg = bus.recv(timeout=0.1)\n"
             "            if msg:\n"
             "                print(f\"RX  id={msg.arbitration_id:#05x}  \"\n"
             "                      f\"data={msg.data.hex(' ')}\")\n"
             "    threading.Thread(target=rx_loop, daemon=True).start()\n\n";
    }

    s << "    print(f\"[CAN] TX on {CHANNEL} @ {BITRATE} bps.  Ctrl+C to stop.\")\n"
         "    try:\n"
         "        while True:\n"
         "            time.sleep(0.1)\n"
         "    except KeyboardInterrupt:\n"
         "        print(\"\\n[CAN] Stopping...\")\n"
         "    finally:\n"
         "        stop.set()\n"
         "        bus.shutdown()\n\n\n"
         "if __name__ == \"__main__\":\n"
         "    main()\n";

    return out;
}

// ── Lua generator ─────────────────────────────────────────────────────────────

QString CanPrototypeExporter::generateLua(const QVector<FrameExportInfo> &frames,
                                           const Config &cfg) {
    QString out;
    QTextStream s(&out);

    int periodic = 0;
    for (const auto &f : frames) if (f.periodMs > 0.0) ++periodic;

    s << "-- CAN Module Prototype\n"
         "-- Generated by MagistralaCAN4  ("
      << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm") << ")\n"
      << "-- Messages: " << frames.size()
      << "  (periodic: " << periodic << ", non-periodic: " << (frames.size() - periodic) << ")\n"
      << "--\n"
         "-- Target: any Lua 5.3+ runtime with CAN binding\n"
         "-- For Linux + SocketCAN:  require 'lua-can'   (github.com/marcinzelent/lua-can)\n"
         "-- For Arduino/NodeMCU:    port to C++ using MCP_CAN library\n"
         "--\n"
         "-- Bus configuration\n"
      << "local CHANNEL = \"" << cfg.channel << "\"\n"
      << "local BITRATE = " << cfg.bitrate << "\n\n";

    // Bit-pack helper (only if needed)
    bool needsPack = false;
    for (const auto &f : frames)
        for (const auto &sig : f.sigList)
            if ((sig.startBit % 8 != 0) || (sig.length % 8 != 0)) { needsPack = true; break; }

    if (needsPack) {
        s << "--- Generic signal packer (Intel byte order)\n"
             "local function pack_bits(data, start_bit, length, value, scale, offset)\n"
             "    local raw = math.max(0, math.min((1 << length) - 1,\n"
             "                math.floor((value - offset) / scale + 0.5)))\n"
             "    for i = 0, length - 1 do\n"
             "        local byte_idx = (start_bit + i) // 8 + 1\n"
             "        local bit_pos  = (start_bit + i) % 8\n"
             "        data[byte_idx] = (data[byte_idx] | (((raw >> i) & 1) << bit_pos)) & 0xFF\n"
             "    end\n"
             "end\n\n";
    }

    // One table per message
    for (const auto &f : frames) {
        QString var = luaVarName(f);

        s << "-- ═══════════════════════════════════════════════════════════════════\n"
          << "--- Message " << hexId(f.id) << "  — " << f.name << "\n";
        if (f.periodMs > 0.0)
            s << "--- Period: " << f.periodMs << " ms"
              << (f.jitterMs > 0.0 ? QString("  (jitter ±%1 ms)").arg(f.jitterMs,0,'f',2) : "")
              << "\n";
        if (!f.sigList.isEmpty()) {
            s << "--- Signals:\n";
            for (const auto &sig : f.sigList)
                s << "---" << sigComment(sig, 1);
        }
        if (!f.counterBytes.isEmpty()) {
            s << "--- Auto-detected:\n";
            for (int b : f.counterBytes)
                s << "---   Byte " << b << ": rolling counter\n";
        }
        s << "\n";

        s << "local " << var << " = {\n"
          << "    id       = " << hexId(f.id) << ",\n"
          << "    extended = " << (f.extended ? "true" : "false") << ",\n"
          << "    dlc      = " << f.dlc << ",\n";
        if (f.periodMs > 0.0)
            s << "    period_ms = " << f.periodMs << ",\n";
        s << "    signals  = {\n";
        for (const auto &sig : f.sigList) {
            QString sn = sanitize(sig.name).toLower();
            s << "        " << sn << " = { value = " << sig.offset
              << ", start_bit = " << sig.startBit
              << ", length = " << sig.length
              << ", scale = " << sig.scale
              << ", offset = " << sig.offset
              << ", le = " << (sig.isLittleEndian ? "true" : "false") << " },";
            if (!sig.unit.isEmpty()) s << "  -- " << sig.unit;
            s << "\n";
        }
        s << "    },\n";
        // Counter bytes
        for (int b : f.counterBytes)
            s << "    _ctr_byte" << b << " = 0,  -- rolling counter → byte " << b << "\n";
        // Sample data
        s << "    sample = {";
        for (int b = 0; b < f.dlc && b < (int)f.sampleData.size(); ++b) {
            if (b) s << ", ";
            s << QString("0x%1").arg(f.sampleData[b], 2, 16, QChar('0')).toUpper();
        }
        s << "},\n}\n\n";

        // encode() method
        s << "function " << var << ":encode()\n"
             "    local data = {}\n"
             "    for i = 1, self.dlc do data[i] = 0x00 end\n";
        for (const auto &sig : f.sigList) {
            QString sn = sanitize(sig.name).toLower();
            int startByte    = sig.startBit / 8;
            bool byteAligned = (sig.startBit % 8 == 0) && (sig.length % 8 == 0);
            int numBytes     = sig.length / 8;

            s << "    -- " << sig.name;
            if (!sig.unit.isEmpty()) s << " [" << sig.unit << "]";
            s << ": startBit=" << sig.startBit << ", len=" << sig.length
              << ", scale=" << sig.scale << "\n";

            if (byteAligned && sig.isLittleEndian && numBytes >= 1 && numBytes <= 4) {
                int maxRaw = (1 << sig.length) - 1;
                s << "    local raw_" << sn << " = math.max(0, math.min(" << maxRaw
                  << ", math.floor((self.signals." << sn << ".value - "
                  << sig.offset << ") / " << sig.scale << " + 0.5)))\n";
                for (int b = 0; b < numBytes; ++b)
                    s << "    data[" << (startByte + b + 1) << "] = (raw_" << sn
                      << " >> " << (b * 8) << ") & 0xFF\n";
            } else {
                s << "    pack_bits(data, " << sig.startBit << ", " << sig.length
                  << ", self.signals." << sn << ".value, "
                  << sig.scale << ", " << sig.offset << ")\n";
            }
        }
        if (f.sigList.isEmpty()) {
            s << "    -- No DBC loaded — copy sample data\n"
                 "    for i = 1, #self.sample do data[i] = self.sample[i] end\n";
        }
        for (int cb : f.counterBytes) {
            s << "    data[" << (cb + 1) << "] = self._ctr_byte" << cb << "\n"
              << "    self._ctr_byte" << cb << " = (self._ctr_byte" << cb << " + 1) & 0xFF\n";
        }
        s << "    return data\n"
          << "end\n\n";
    }

    // ── Simulation loop ───────────────────────────────────────────────────
    s << "-- ══════════════════════════════════════════════════════════════════════\n"
         "-- Simulation loop (adapt to your CAN library API)\n"
         "-- ══════════════════════════════════════════════════════════════════════\n\n"
         "-- Example: adjust signal values here before running\n";
    for (const auto &f : frames) {
        QString var = luaVarName(f);
        for (const auto &sig : f.sigList) {
            QString sn = sanitize(sig.name).toLower();
            s << var << ".signals." << sn << ".value = " << sig.offset
              << "  -- " << sig.name;
            if (!sig.unit.isEmpty()) s << " [" << sig.unit << "]";
            s << "\n";
        }
    }
    s << "\n"
         "local CAN = require('can')  -- adapt to your binding\n"
         "CAN.open(CHANNEL, BITRATE)\n\n"
         "local messages = {\n";
    for (const auto &f : frames) {
        if (f.periodMs > 0.0)
            s << "    " << luaVarName(f) << ",\n";
    }
    s << "}\n\n"
         "local next_tx = {}\n"
         "local t0 = os.clock()\n"
         "for i = 1, #messages do next_tx[i] = t0 end\n\n"
         "print('[CAN] Starting simulation on ' .. CHANNEL .. ' @ ' .. BITRATE .. ' bps')\n"
         "print('[CAN] Press Ctrl+C to stop.')\n\n"
         "while true do\n"
         "    local now = os.clock()\n"
         "    for i, msg in ipairs(messages) do\n"
         "        if now >= next_tx[i] then\n"
         "            CAN.send(msg.id, msg:encode(), msg.extended)\n"
         "            next_tx[i] = next_tx[i] + msg.period_ms / 1000.0\n"
         "        end\n"
         "    end\n"
         "    -- 1 ms sleep (platform-specific)\n"
         "    os.execute('sleep 0.001')\n"
         "end\n";

    return out;
}

// ── Arduino C++ generator ─────────────────────────────────────────────────────

QString CanPrototypeExporter::generateArduino(const QVector<FrameExportInfo> &frames,
                                               const Config &cfg) {
    QString out;
    QTextStream s(&out);

    int periodic = 0;
    for (const auto &f : frames) if (f.periodMs > 0.0) ++periodic;

    s << "/**\n"
         " * CAN Module Prototype\n"
         " * Generated by MagistralaCAN4  ("
      << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm") << ")\n"
      << " *\n"
         " * Target  : Arduino (Uno/Mega/Nano) + MCP2515 CAN controller\n"
         " * Library : MCP_CAN  —  github.com/coryjfowler/MCP_CAN_lib\n"
         " *           #include <mcp_can.h>  in Library Manager\n"
         " *\n"
         " * Detected messages  : " << frames.size() << "\n"
         " *   periodic         : " << periodic << "\n"
         " *   non-periodic     : " << (frames.size() - periodic) << "\n"
         " *\n"
         " * Bus speed : " << cfg.bitrate << " bps\n"
         " *\n"
         " * Wiring (MCP2515 breakout):\n"
         " *   VCC → 5V/3.3V   GND → GND\n"
         " *   SCK → D13        MOSI → D11    MISO → D12    CS → D10\n"
         " *   INT → D2  (optional interrupt pin)\n"
         " */\n\n"
         "#include <SPI.h>\n"
         "#include <mcp_can.h>\n\n";

    // Bus speed constant
    QString speedConst;
    switch (cfg.bitrate) {
        case 125000: speedConst = "CAN_125KBPS"; break;
        case 250000: speedConst = "CAN_250KBPS"; break;
        case 500000: speedConst = "CAN_500KBPS"; break;
        case 1000000:speedConst = "CAN_1000KBPS"; break;
        default:     speedConst = "CAN_500KBPS"; break;
    }

    s << "// ── Hardware ────────────────────────────────────────────────────────────────\n"
         "const int  CAN_CS_PIN  = 10;          // SPI chip select\n"
      << "const long CAN_SPEED   = " << speedConst << ";\n"
         "const byte MCP_CLOCK   = MCP_16MHZ;   // or MCP_8MHZ depending on crystal\n\n"
         "MCP_CAN CAN(CAN_CS_PIN);\n\n";

    // packBits helper — emit only when needed (non-byte-aligned or Motorola signals)
    bool needsPack = false;
    for (const auto &f : frames)
        for (const auto &sig : f.sigList)
            if ((sig.startBit % 8 != 0) || (sig.length % 8 != 0)) { needsPack = true; break; }

    if (needsPack) {
        s << "// ── Generic signal packer ───────────────────────────────────────────────────\n"
             "// Packs a physical value into a CAN data buffer at the given bit position.\n"
             "// Intel LE: startBit = LSB, bits grow towards MSB.  Motorola BE: startBit = MSB.\n"
             "void packBits(byte* data, int startBit, int length, float value,\n"
             "              float scale, float offset, bool littleEndian) {\n"
             "    long raw = constrain((long)((value - offset) / scale), 0L, (1L << length) - 1L);\n"
             "    if (littleEndian) {\n"
             "        for (int i = 0; i < length; i++) {\n"
             "            int b = (startBit + i) / 8, p = (startBit + i) % 8;\n"
             "            if (b < 8) { if ((raw >> i) & 1) data[b] |= (1 << p);\n"
             "                         else                data[b] &= ~(1 << p); }\n"
             "        }\n"
             "    } else {\n"
             "        // Motorola: startBit is MSB; bits descend in DBC visual order\n"
             "        for (int i = 0; i < length; i++) {\n"
             "            int srcBit = length - 1 - i;\n"
             "            int row = startBit / 8, col = 7 - (startBit % 8) + i;\n"
             "            int b = row + col / 8, p = col % 8;\n"
             "            if (b >= 0 && b < 8) { if ((raw >> srcBit) & 1) data[b] |= (1 << p);\n"
             "                                    else                     data[b] &= ~(1 << p); }\n"
             "        }\n"
             "    }\n"
             "}\n\n";
    }

    // Per-message variables + encode functions
    for (const auto &f : frames) {
        QString vn = cppVarName(f);
        int period = static_cast<int>(std::round(f.periodMs));

        s << "// ── " << hexId(f.id) << "  " << f.name << " ─────────────────────────────────────────\n";
        if (f.periodMs > 0.0)
            s << "// Period: " << period << " ms"
              << (f.jitterMs > 0.0 ? QString("  (jitter ±%1 ms)").arg(f.jitterMs,0,'f',1) : "")
              << "\n";
        if (!f.sigList.isEmpty()) {
            s << "// Signals:\n";
            for (const auto &sig : f.sigList) {
                s << "//   " << sig.name;
                if (!sig.unit.isEmpty()) s << " [" << sig.unit << "]";
                s << ": startBit=" << sig.startBit << ", len=" << sig.length
                  << ", scale=" << sig.scale << ", offset=" << sig.offset << "\n";
            }
        }
        if (!f.counterBytes.isEmpty()) {
            for (int b : f.counterBytes)
                s << "// Byte " << b << ": rolling counter (auto-detected)\n";
        }
        s << "\n";

        s << "const uint32_t ID_" << vn.toUpper() << " = " << hexId(f.id) << ";\n";
        if (period > 0)
            s << "const unsigned long PERIOD_" << vn.toUpper() << " = " << period << ";  // ms\n"
              << "unsigned long lastTx_" << vn << " = 0;\n";
        s << "byte txBuf_" << vn << "[" << f.dlc << "];\n";
        for (int b : f.counterBytes)
            s << "uint8_t ctr_" << vn << "_b" << b << " = 0;  // rolling counter byte " << b << "\n";

        // Signal variables
        for (const auto &sig : f.sigList) {
            QString sv = sanitize(sig.name).toLower();
            s << "float " << sv << " = " << sig.offset << "f;";
            if (!sig.unit.isEmpty()) s << "  // " << sig.unit;
            s << "\n";
        }
        s << "\n";

        // encode function
        s << "void encode_" << vn << "() {\n"
             "    memset(txBuf_" << vn << ", 0, sizeof(txBuf_" << vn << "));\n";
        for (const auto &sig : f.sigList)
            s << arduinoEncodeSignal(sig).replace("txData", "txBuf_" + vn);
        if (f.sigList.isEmpty() && !f.sampleData.isEmpty()) {
            s << "    // No DBC loaded — using last captured payload\n";
            for (int b = 0; b < f.dlc && b < (int)f.sampleData.size(); ++b)
                s << "    txBuf_" << vn << "[" << b << "] = "
                  << QString("0x%1").arg(f.sampleData[b], 2, 16, QChar('0')).toUpper() << ";\n";
        }
        for (int b : f.counterBytes) {
            s << "    txBuf_" << vn << "[" << b << "] = ctr_" << vn << "_b" << b << "++;\n";
        }
        s << "}\n\n";
    }

    // setup()
    s << "// ── Arduino setup ───────────────────────────────────────────────────────────\n"
         "void setup() {\n"
         "    Serial.begin(115200);\n"
         "    while (CAN.begin(MCP_ANY, CAN_SPEED, MCP_CLOCK) != CAN_OK) {\n"
         "        Serial.println(\"CAN init failed — retrying...\");\n"
         "        delay(200);\n"
         "    }\n"
         "    CAN.setMode(MCP_NORMAL);\n"
         "    Serial.println(\"CAN OK — module simulation running\");\n\n"
         "    // ── Set initial signal values here ────────────────────────────────\n";
    for (const auto &f : frames) {
        for (const auto &sig : f.sigList) {
            QString sv = sanitize(sig.name).toLower();
            s << "    " << sv << " = " << sig.offset << "f;";
            if (!sig.unit.isEmpty()) s << "  // " << sig.unit;
            s << "\n";
        }
    }
    s << "}\n\n";

    // loop()
    s << "// ── Arduino loop ────────────────────────────────────────────────────────────\n"
         "void loop() {\n"
         "    unsigned long now = millis();\n\n";
    for (const auto &f : frames) {
        if (f.periodMs <= 0.0) continue;
        QString vn      = cppVarName(f);
        QString periVar = "PERIOD_" + vn.toUpper();
        s << "    // Transmit " << hexId(f.id) << " every " << (int)std::round(f.periodMs) << " ms\n"
          << "    if (now - lastTx_" << vn << " >= " << periVar << ") {\n"
          << "        encode_" << vn << "();\n"
          << "        CAN.sendMsgBuf(ID_" << vn.toUpper() << ", "
          << (f.extended ? "1" : "0") << ", " << f.dlc
          << ", txBuf_" << vn << ");\n"
          << "        lastTx_" << vn << " = now;\n"
          << "    }\n\n";
    }

    if (cfg.withReceiver) {
        s << "    // Optional: print received frames\n"
             "    unsigned char rxLen = 0;\n"
             "    byte rxBuf[8];\n"
             "    unsigned long rxId;\n"
             "    if (CAN_MSGAVAIL == CAN.checkReceive()) {\n"
             "        CAN.readMsgBuf(&rxId, &rxLen, rxBuf);\n"
             "        Serial.print(\"RX  id=0x\"); Serial.print(rxId, HEX);\n"
             "        Serial.print(\"  data=\");\n"
             "        for (byte i = 0; i < rxLen; i++) {\n"
             "            Serial.print(rxBuf[i], HEX); Serial.print(' ');\n"
             "        }\n"
             "        Serial.println();\n"
             "    }\n";
    }

    s << "}\n";
    return out;
}
