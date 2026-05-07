#include "J1939Parser.h"
#include <cmath>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════
// J1939Frame – metody statyczne
// ═══════════════════════════════════════════════════════════════

bool J1939Frame::fromCanFrame(const CanFrame &can, J1939Frame &out) {
    if (!can.extended) return false;       // J1939 zawsze używa 29-bit ID
    uint32_t id = can.id;

    out.priority      = (id >> 26) & 0x7;
    bool r            = (id >> 25) & 0x1;
    bool dp           = (id >> 24) & 0x1;
    out.dataPage      = dp;
    out.pduFormat     = (id >> 16) & 0xFF;
    out.pduSpecific   = (id >> 8)  & 0xFF;
    out.sourceAddress = id & 0xFF;

    out.isPdu1 = (out.pduFormat < 240);

    // Oblicz PGN (18-bit)
    uint32_t pgnBase = (static_cast<uint32_t>(r) << 17)
                     | (static_cast<uint32_t>(dp) << 16)
                     | (static_cast<uint32_t>(out.pduFormat) << 8);
    if (out.isPdu1) {
        pgnBase |= out.pduSpecific;   // PDU1 – DA w PS
        out.destAddress = out.pduSpecific;
        out.groupExt = 0;
    } else {
        // PDU2 – PS = Group Extension, dolne 8 bitów PGN = 0
        out.destAddress = 0xFF;        // broadcast
        out.groupExt = out.pduSpecific;
    }
    out.pgn = pgnBase;

    out.timestamp = can.timestamp;
    out.dlc       = can.dlc;
    std::copy(std::begin(can.data), std::end(can.data), std::begin(out.data));

    return true;
}

bool J1939Frame::isJ1939(const CanFrame &can) {
    // J1939 używa 29-bitowych ID; prosta heurystyka – PF w zakresie 0–255
    if (!can.extended) return false;
    uint32_t pf = (can.id >> 16) & 0xFF;
    (void)pf;
    return true;
}

uint32_t J1939Frame::canId() const {
    uint32_t id = 0;
    id |= (static_cast<uint32_t>(priority)    << 26);
    // R (bit 25) – odzyskaj z PGN bit 17
    id |= (((pgn >> 17) & 0x1)               << 25);
    // DP (bit 24) – odzyskaj z PGN bit 16
    id |= (((pgn >> 16) & 0x1)               << 24);
    id |= (static_cast<uint32_t>(pduFormat)   << 16);
    id |= (static_cast<uint32_t>(pduSpecific) << 8);
    id |= sourceAddress;
    return id;
}

QString J1939Frame::toString() const {
    return QString("PGN %1 | SA 0x%2 | Prio %3 | %4 | DLC %5")
        .arg(pgnHex())
        .arg(sourceAddress, 2, 16, QChar('0'))
        .arg(priority)
        .arg(isPdu1 ? QString("DA 0x%1").arg(destAddress, 2, 16, QChar('0'))
                    : QString("GE 0x%1").arg(groupExt, 2, 16, QChar('0')))
        .arg(dlc);
}

// ═══════════════════════════════════════════════════════════════
// J1939Parser – baza PGN
// ═══════════════════════════════════════════════════════════════

J1939Parser::J1939Parser() {
    // ── Silnik – EEC1 (Electronic Engine Controller 1) ──
    registerPg({61444, "Electronic Engine Controller 1", "EEC1", 8, {
        { 899, "Engine Torque Mode",               0,0,4,  1,   0, ""},
        { 512, "Driver's Demand Engine Torque",     1,0,8,  1, -125, "%"},
        { 513, "Actual Engine Torque",              2,0,8,  1, -125, "%"},
        { 190, "Engine Speed",                      3,0,16, 0.125, 0, "rpm"},
        { 512, "Source Address of Controlling Device", 5,0,8, 1,0,""},
        { 1675, "Engine Starter Mode",              6,4,4,  1,   0, ""},
        { 2432, "Engine Demand Torque",             7,0,8,  1, -125, "%"},
    }});

    // ── Silnik – EEC2 ──
    registerPg({61443, "Electronic Engine Controller 2", "EEC2", 8, {
        { 91,  "Accelerator Pedal Position 1",      0,0,8,  0.4, 0, "%"},
        { 92,  "Engine Percent Load At Current Speed", 1,0,8, 1, 0, "%"},
        { 558, "Accelerator Pedal Position 2",      2,0,8,  0.4, 0, "%"},
        { 2975, "Actual Max Available Torque",      3,0,8,  1, -125, "%"},
    }});

    // ── Temperatura silnika ──
    registerPg({65262, "Engine Temperature 1", "ET1", 8, {
        { 110, "Engine Coolant Temperature",         0,0,8,  1, -40, "degC"},
        { 174, "Engine Fuel Temperature 1",          1,0,8,  1, -40, "degC"},
        { 175, "Engine Oil Temperature 1",           2,0,8,  1, -40, "degC"},
        { 176, "Engine Turbo Oil Temperature",       3,0,8,  1, -40, "degC"},
        {  52, "Engine Intercooler Temperature",     4,0,8,  1, -40, "degC"},
    }});

    // ── Cruise Control / Vehicle Speed ──
    registerPg({65265, "Cruise Control / Vehicle Speed", "CCVS", 8, {
        { 84,  "Wheel Based Speed",                  1,0,16, 1.0/256, 0, "km/h"},
        { 595, "Cruise Control Active",              3,0,2,  1, 0, ""},
        { 597, "Brake Switch",                       3,4,2,  1, 0, ""},
        { 598, "Clutch Switch",                      3,2,2,  1, 0, ""},
        { 976, "PTO State",                          7,0,5,  1, 0, ""},
    }});

    // ── Fuel Economy ──
    registerPg({65266, "Fuel Economy", "LFE", 8, {
        { 183, "Engine Fuel Rate",                   0,0,16, 0.05, 0, "L/h"},
        { 184, "Engine Instantaneous Fuel Economy",  2,0,16, 1.0/512, 0, "km/L"},
        { 185, "Engine Average Fuel Economy",        4,0,16, 1.0/512, 0, "km/L"},
        { 182, "Engine Trip Fuel",                   6,0,16, 0.5, 0, "L"},
    }});

    // ── Dash Display ──
    registerPg({65276, "Dash Display", "DD", 8, {
        { 168, "Battery Potential / Voltage",        0,0,16, 0.05, 0, "V"},
        { 177, "Transmission Oil Temperature",       2,0,8,  1, -40, "degC"},
        { 127, "Transmission Oil Level",             4,0,8,  0.4, 0, "%"},
    }});

    // ── Inlet / Exhaust Conditions ──
    registerPg({65270, "Inlet / Exhaust Conditions 1", "IC1", 8, {
        { 102, "Boost Pressure",                     1,0,16, 2, 0, "kPa"},
        { 106, "Air Inlet Pressure",                 4,0,8,  2, 0, "kPa"},
        { 105, "Intake Manifold Temperature",        0,0,8,  1, -40, "degC"},
    }});

    // ── Vehicle Position ──
    registerPg({65267, "Vehicle Position", "VP", 8, {
        { 245, "Vehicle Total Distance",             0,0,32, 5, 0, "m"},
    }});

    // ── DM1 – Active Diagnostic Trouble Codes ──
    registerPg({65226, "Active Diagnostic Trouble Codes", "DM1", 8, {
        { 1213, "Malfunction Indicator Lamp",        0,6,2,  1,0,""},
        {  623, "Red Stop Lamp",                     0,4,2,  1,0,""},
        {  624, "Amber Warning Lamp",                0,2,2,  1,0,""},
        {  987, "Protect Lamp",                      0,0,2,  1,0,""},
    }});

    // ── Transport Protocol – Connection Management ──
    registerPg({60416, "Transport Protocol – CM", "TP.CM", 8, {}});
    // ── Transport Protocol – Data Transfer ──
    registerPg({60160, "Transport Protocol – DT", "TP.DT", 8, {}});

    // ── Time/Date ──
    registerPg({65254, "Time/Date", "TD", 8, {
        { 1601, "Seconds",                           0,0,8,  0.25, 0, "s"},
        { 1602, "Minutes",                           1,0,8,  1, 0, "min"},
        { 1603, "Hours",                             2,0,8,  1, 0, "h"},
        { 1604, "Day",                               4,0,8,  1, 0, ""},
        { 1605, "Month",                             3,0,8,  1, 0, ""},
        { 1606, "Year",                              5,0,16, 1, 1985, ""},
    }});

    // ── Vehicle Electrical Power ──
    registerPg({65271, "Vehicle Electrical Power 1", "VEP1", 8, {
        { 168, "Electrical Potential",               0,0,16, 0.05, 0, "V"},
        { 167, "Alternator Potential",               2,0,16, 0.05, 0, "V"},
        { 158, "Battery Current",                    4,0,16, 0.05, -1600, "A"},
    }});

    // ── Cab Message 1 ──
    registerPg({65263, "Cab Message 1", "CM1", 8, {
        { 597, "Parking Brake Switch",               3,0,2,  1,0,""},
        {  70, "Seat Belt Switch",                   2,2,2,  1,0,""},
    }});
}

const PgnDef* J1939Parser::pgnDef(uint32_t pgn) const {
    auto it = m_pgnDb.find(pgn);
    return (it != m_pgnDb.end()) ? &it.value() : nullptr;
}

QString J1939Parser::pgnName(uint32_t pgn) const {
    auto *def = pgnDef(pgn);
    return def ? def->name : QString("PGN 0x%1").arg(pgn, 5, 16, QChar('0')).toUpper();
}

QString J1939Parser::sourceName(uint8_t sa) {
    switch (sa) {
    case 0x00:   return "Engine #1";
    case 0x03:   return "Engine #2";
    case 0x0B:   return "Brakes – Controller";
    case 0x11:   return "Brakes – Trailer";
    case 0x13:   return "Transmission #1";
    case 0x17:   return "Transmission #2";
    case 0x21:   return "Instrument Cluster";
    case 0x23:   return "Body Controller";
    case 0x2B:   return "Cab Controller";
    case 0x33:   return "Suspension – Front";
    case 0x40:   return "Turbo/Supercharger";
    case 0x47:   return "On-Board Diagnostics (OBD)";
    case 0x49:   return "Vehicle Dynamics";
    case 0xF9:   return "Off-board tool";
    case 0xFE:   return "Null address";
    default:     return QString("SA 0x%1").arg(sa, 2, 16, QChar('0'));
    }
}

double J1939Parser::decodeSpn(const uint8_t *data, int dlc,
                               const SpnDef &spn) {
    int byteStart = spn.byteOffset;
    int bitStart  = spn.bitOffset;
    int bitLen    = spn.bitLength;

    if (byteStart >= dlc) return 0.0;
    if (bitLen > 32) return 0.0;  // bezpiecznik

    // Odczytaj do 4 bajtów little‑endian (J1939 domyślnie LE)
    uint32_t raw = 0;
    int bytesNeeded = (bitStart + bitLen + 7) / 8;
    for (int i = 0; i < bytesNeeded && (byteStart + i) < dlc; ++i) {
        raw |= static_cast<uint32_t>(data[byteStart + i]) << (i * 8);
    }

    // Wytnij interesujące bity
    uint32_t mask = (bitLen >= 32) ? 0xFFFFFFFF : ((1u << bitLen) - 1u);
    uint32_t value = (raw >> bitStart) & mask;

    double result = static_cast<double>(value) * spn.resolution + spn.offset;
    return result;
}

QString J1939Parser::decodeSignals(const J1939Frame &frame) const {
    auto *def = pgnDef(frame.pgn);
    if (!def || def->spns.isEmpty())
        return QString("[PGN %1 – brak definicji SPN]").arg(frame.pgnHex());

    QStringList parts;
    for (const auto &spn : def->spns) {
        double val = decodeSpn(frame.data.data(), frame.dlc, spn);
        parts.append(QString("%1: %2 %3")
                     .arg(spn.name)
                     .arg(val, 0, 'f', 2)
                     .arg(spn.unit.isEmpty() ? "" : spn.unit));
    }
    return parts.join(" | ");
}

void J1939Parser::registerPg(const PgnDef &pg) {
    m_pgnDb[pg.pgn] = pg;
}

// ═══════════════════════════════════════════════════════════════
// Dodatkowe opisy SPN – helper spoza klasy
// ═══════════════════════════════════════════════════════════════
