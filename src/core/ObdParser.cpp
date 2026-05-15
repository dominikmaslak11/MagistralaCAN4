#include "ObdFrame.h"

ObdFrame ObdFrame::fromCanFrame(const CanFrame &frame) {
    ObdFrame o;
    o.canId = frame.id; o.timestamp = frame.timestamp; o.dlc = frame.dlc;
    for (int i = 0; i < frame.dlc && i < 64; ++i) o.data[i] = frame.data[i];

    if (frame.dlc < 2) return o;

    // OBD-II request: ID 0x7DF or 0x7E0-0x7E7, data[0]=length, data[1]=mode, data[2]=PID
    // OBD-II response: ID 0x7E8-0x7EF, data[0]=length, data[1]=mode+0x40, data[2]=PID
    bool isReqId = (frame.id == 0x7DF) || (frame.id >= 0x7E0 && frame.id <= 0x7E7);
    bool isRespId = (frame.id >= 0x7E8 && frame.id <= 0x7EF);

    if (!isReqId && !isRespId) return o;

    uint8_t len  = frame.data[0] & 0x0F;
    uint8_t b1   = frame.data[1];
    o.len = len;

    if (b1 >= 0x41 && b1 <= 0x4A) {
        o.type = Response;
        o.mode = b1 - 0x40;
    } else if (b1 >= 0x01 && b1 <= 0x0A) {
        o.type = Request;
        o.mode = b1;
    }

    if (frame.dlc >= 3)
        o.pid = static_cast<uint16_t>(frame.data[2]);
    if (o.mode == 0x01 && frame.dlc >= 4 && o.pid == 0x00)
        o.pid = (static_cast<uint16_t>(frame.data[2]) << 8) | frame.data[3];

    return o;
}

bool ObdFrame::isObd(const CanFrame &frame) {
    return frame.id == 0x7DF || (frame.id >= 0x7E0 && frame.id <= 0x7EF);
}

QString ObdFrame::toString() const {
    return type == Request
        ? QString("[REQ] Mode 0x%1 PID 0x%2").arg(mode, 2, 16, QChar('0')).arg(pid, 4, 16, QChar('0')).toUpper()
        : QString("[RESP] Mode 0x%1 PID 0x%2").arg(mode, 2, 16, QChar('0')).arg(pid, 4, 16, QChar('0')).toUpper();
}

// ═══════════════════════════════════════════════════════════════
// ObdParser
// ═══════════════════════════════════════════════════════════════

ObdParser::ObdParser() {
    m_modeDb = {
        {0x01, "Current Data"}, {0x02, "Freeze Frame"}, {0x03, "Stored DTC"},
        {0x04, "Clear DTC"}, {0x05, "Oxygen Sensor"}, {0x06, "On-Board Monitoring"},
        {0x07, "Pending DTC"}, {0x08, "Control Operation"}, {0x09, "Vehicle Information"},
        {0x0A, "Permanent DTC"}
    };

    // Mode 0x01 PIDs
    // {name, unit, scale, offset, numBytes}
    // numBytes=1: value = data[3]*scale + offset
    // numBytes=2: value = (data[3]*256 + data[4])*scale + offset (SAE J1979)
    auto p = [](uint8_t m, uint16_t pid) { return qMakePair(m, pid); };
    m_pidDb = {
        {p(0x01, 0x00), {"PIDs supported (00-20)", "",    1,           0,    4}},
        {p(0x01, 0x04), {"Calculated Engine Load",  "%",  100.0/255,   0,    1}},
        {p(0x01, 0x05), {"Engine Coolant Temp",     "°C", 1,          -40,   1}},
        {p(0x01, 0x0A), {"Fuel Pressure",           "kPa",3,           0,    1}},
        {p(0x01, 0x0B), {"Intake Manifold Pressure","kPa",1,           0,    1}},
        {p(0x01, 0x0C), {"Engine RPM",              "rpm",0.25,        0,    2}},
        {p(0x01, 0x0D), {"Vehicle Speed",           "km/h",1,          0,    1}},
        {p(0x01, 0x0E), {"Timing Advance",          "°",  0.5,        -64,   1}},
        {p(0x01, 0x0F), {"Intake Air Temperature",  "°C", 1,          -40,   1}},
        {p(0x01, 0x10), {"MAF Air Flow Rate",       "g/s",0.01,        0,    2}},
        {p(0x01, 0x11), {"Throttle Position",       "%",  100.0/255,   0,    1}},
        {p(0x01, 0x13), {"Oxygen Sensors Present",  "",   1,           0,    1}},
        {p(0x01, 0x14), {"O2 Sensor 1 Voltage",     "V",  0.005,       0,    1}},
        {p(0x01, 0x1C), {"OBD Standard",            "",   1,           0,    1}},
        {p(0x01, 0x1F), {"Run Time Since Start",    "s",  1,           0,    2}},
        {p(0x01, 0x20), {"PIDs supported (21-40)",  "",   1,           0,    4}},
        {p(0x01, 0x21), {"Distance with MIL",       "km", 1,           0,    2}},
        {p(0x01, 0x2F), {"Fuel Level Input",        "%",  100.0/255,   0,    1}},
        {p(0x01, 0x33), {"Barometric Pressure",     "kPa",1,           0,    1}},
        {p(0x01, 0x42), {"Control Module Voltage",  "V",  0.001,       0,    2}},
        {p(0x01, 0x46), {"Ambient Air Temperature", "°C", 1,          -40,   1}},
    };

    // More Mode 0x01 PIDs
    m_pidDb.insert({
        {p(0x01, 0x22), {"Fuel Rail Pressure (rel)", "kPa",  0.079,       0,    2}},
        {p(0x01, 0x23), {"Fuel Rail Pressure (abs)", "kPa",  10,          0,    2}},
        {p(0x01, 0x2C), {"Commanded EGR",            "%",    100.0/255,   0,    1}},
        {p(0x01, 0x2E), {"Commanded Evap. Purge",    "%",    100.0/255,   0,    1}},
        {p(0x01, 0x2F), {"Fuel Level Input",         "%",    100.0/255,   0,    1}},
        {p(0x01, 0x30), {"Warm-ups Since Clear",     "",     1,           0,    1}},
        {p(0x01, 0x31), {"Distance Since Clear",     "km",   1,           0,    2}},
        {p(0x01, 0x43), {"Absolute Load Value",      "%",    100.0/255,   0,    2}},
        {p(0x01, 0x45), {"Relative Throttle Pos",    "%",    100.0/255,   0,    1}},
        {p(0x01, 0x46), {"Ambient Air Temperature",  "°C",   1,          -40,   1}},
        {p(0x01, 0x47), {"Abs. Throttle Position B", "%",    100.0/255,   0,    1}},
        {p(0x01, 0x49), {"Accel. Pedal Position D",  "%",    100.0/255,   0,    1}},
        {p(0x01, 0x4C), {"Cmd. Throttle Actuator",   "%",    100.0/255,   0,    1}},
        {p(0x01, 0x4D), {"Time MIL On",              "min",  1,           0,    2}},
        {p(0x01, 0x4E), {"Time Since DTC Cleared",   "min",  1,           0,    2}},
        {p(0x01, 0x5C), {"Engine Oil Temperature",   "°C",   1,          -40,   1}},
        {p(0x01, 0x5E), {"Engine Fuel Rate",         "L/h",  0.05,        0,    2}},
    });

    // Mode 0x09 PIDs
    m_pidDb[qMakePair(uint8_t(0x09), uint16_t(0x02))] = {"VIN (chars 1-4)", "", 1, 0, 1};
    m_pidDb[qMakePair(uint8_t(0x09), uint16_t(0x04))] = {"Calibration ID",  "", 1, 0, 1};
    m_pidDb[qMakePair(uint8_t(0x09), uint16_t(0x06))] = {"CVN",             "", 1, 0, 1};
}

QString ObdParser::modeName(uint8_t mode) const {
    return m_modeDb.value(mode, QString("Mode 0x%1").arg(mode, 2, 16, QChar('0')));
}

QString ObdParser::pidName(uint8_t mode, uint16_t pid) const {
    auto it = m_pidDb.find(qMakePair(mode, pid));
    return (it != m_pidDb.end()) ? it->name : QString("PID 0x%1").arg(pid, 4, 16, QChar('0'));
}

QString ObdParser::pidUnit(uint8_t mode, uint16_t pid) const {
    auto it = m_pidDb.find(qMakePair(mode, pid));
    return (it != m_pidDb.end()) ? it->unit : "";
}

double ObdParser::decodePidValue(uint8_t mode, uint16_t pid, const uint8_t *data, int len) const {
    auto it = m_pidDb.find(qMakePair(mode, pid));
    if (it == m_pidDb.end() || len < 4) return 0;

    // data layout: [0]=len, [1]=mode+0x40, [2]=PID, [3]=A, [4]=B
    if (it->numBytes >= 2) {
        if (len < 5) return 0;
        uint16_t raw = (static_cast<uint16_t>(data[3]) << 8) | data[4];
        return raw * it->scale + it->offset;
    }
    return data[3] * it->scale + it->offset;
}

QString ObdParser::decodeDtc(uint16_t rawCode) {
    if (rawCode == 0) return "";
    static const char cats[4] = {'P', 'C', 'B', 'U'};
    char cat = cats[(rawCode >> 14) & 0x3];
    uint8_t d1 = (rawCode >> 12) & 0x3;
    uint8_t d2 = (rawCode >>  8) & 0xF;
    uint8_t d3 = (rawCode >>  4) & 0xF;
    uint8_t d4 =  rawCode        & 0xF;
    return QString("%1%2%3%4%5")
        .arg(cat).arg(d1)
        .arg(d2, 1, 16).arg(d3, 1, 16).arg(d4, 1, 16)
        .toUpper();
}

QStringList ObdParser::parseDtcs(const ObdFrame &f) const {
    QStringList codes;
    if (f.type != ObdFrame::Response) return codes;
    if (f.mode != 0x03 && f.mode != 0x07 && f.mode != 0x0A) return codes;
    // data[0]=ISO-TP len, data[1]=mode+0x40, data[2..] = DTC pairs (2 bytes each, big-endian)
    int dtcBytes = (f.len > 1) ? (f.len - 1) : 0;
    int numDtcs  = dtcBytes / 2;
    for (int i = 0; i < numDtcs; ++i) {
        int idx = 2 + i * 2;
        if (idx + 1 >= static_cast<int>(f.dlc)) break;
        uint16_t raw = (static_cast<uint16_t>(f.data[idx]) << 8) | f.data[idx + 1];
        QString dtc = decodeDtc(raw);
        if (!dtc.isEmpty()) codes.append(dtc);
    }
    return codes;
}
