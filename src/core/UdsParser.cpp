#include "UdsParser.h"
#include <algorithm>

// ═══════════════════════════════════════════════════════════════
// UdsFrame – parsowanie
// ═══════════════════════════════════════════════════════════════

// ── fromPayload — parsuje gotowy payload ISO-TP (payload[0] = SID) ────────────

UdsFrame UdsFrame::fromPayload(const uint8_t *payload, int len,
                                uint32_t canId, uint64_t timestampUs, bool multi) {
    UdsFrame uf;
    uf.canId      = canId;
    uf.timestamp  = timestampUs;
    uf.multiFrame = multi;
    uf.dlc        = static_cast<uint8_t>(std::min(len, static_cast<int>(uf.data.size())));
    std::copy_n(payload, uf.dlc, uf.data.begin());

    if (len < 1) return uf;

    uint8_t b0 = payload[0];

    // ISO-TP FC frame: upper nibble 3, FS byte 0-2 (CTS/Wait/Overflow)
    if ((b0 & 0xF0) == 0x30 && (b0 & 0x0F) <= 2) {
        uf.type = FlowControl;
        return uf;
    }

    if (b0 == 0x7F && len >= 3) {
        uf.type = NegativeResponse;
        uf.sid  = payload[1];
        uf.nrc  = payload[2];
    } else if (b0 >= 0x40 && b0 <= 0x7E) {
        uf.type = PositiveResponse;
        uf.sid  = b0 - 0x40;
        if (len >= 3)
            uf.did = (static_cast<uint16_t>(payload[1]) << 8) | payload[2];
    } else if (b0 >= 0x10 && b0 <= 0x3E) {
        uf.type = Request;
        uf.sid  = b0;
        if (len >= 2) uf.subFunc = payload[1];
        if ((b0 == 0x22 || b0 == 0x2E || b0 == 0x2A || b0 == 0x2F) && len >= 3) {
            uf.did = (static_cast<uint16_t>(payload[1]) << 8) | payload[2];
            uf.subFunc = 0;
        }
    }

    return uf;
}

// ── fromCanFrame — pozostaje dla kompatybilności (deleguje do fromPayload) ────

UdsFrame UdsFrame::fromCanFrame(const CanFrame &frame) {
    if (frame.dlc < 1) return {};
    return fromPayload(frame.data.data(), frame.dlc, frame.id, frame.timestamp, false);
}

// ── looksLikeUds — sprawdza zakres ID diagnostycznego ────────────────────────

bool UdsFrame::looksLikeUds(const CanFrame &frame) {
    if (frame.dlc < 1) return false;
    if (frame.extended) {
        // 29-bit: 0x18DAxx__ (physical) lub 0x18DB33F1 (functional)
        return (frame.id >> 16) == 0x18DAu || frame.id == 0x18DB33F1u;
    }
    // 11-bit: zakres diagnostyczny 0x700-0x7FF (UDS/OBD)
    return frame.id >= 0x700u && frame.id <= 0x7FFu;
}

QString UdsFrame::toString() const {
    switch (type) {
    case Request:
        return QString("[REQ] SID 0x%1").arg(sid, 2, 16, QChar('0')).toUpper();
    case PositiveResponse:
        return QString("[POS] SID 0x%1 (+0x40)").arg(sid, 2, 16, QChar('0')).toUpper();
    case NegativeResponse:
        return QString("[NEG] SID 0x%1 NRC 0x%2").arg(sid, 2, 16, QChar('0')).arg(nrc, 2, 16, QChar('0')).toUpper();
    case FlowControl:
        return QString("[FC] 0x%1").arg(sid, 2, 16, QChar('0')).toUpper();
    default: return "?";
    }
}

// ═══════════════════════════════════════════════════════════════
// UdsParser – baza wiedzy
// ═══════════════════════════════════════════════════════════════

UdsParser::UdsParser() {
    // ── Serwisy UDS (SID) ──
    m_sidDb = {
        {0x10, {"Diagnostic Session Control", "Zmiana sesji diagnostycznej"}},
        {0x11, {"ECU Reset", "Reset sterownika"}},
        {0x14, {"Clear Diagnostic Information", "Kasowanie DTC"}},
        {0x19, {"Read DTC Information", "Odczyt kodów usterek"}},
        {0x22, {"Read Data By Identifier", "Odczyt danych po DID"}},
        {0x23, {"Read Memory By Address", "Odczyt pamięci"}},
        {0x27, {"Security Access", "Dostęp zabezpieczony"}},
        {0x28, {"Communication Control", "Kontrola komunikacji"}},
        {0x2A, {"Read Data By Periodic ID", "Okresowy odczyt danych"}},
        {0x2C, {"Dynamically Define DID", "Dynamiczna definicja DID"}},
        {0x2E, {"Write Data By Identifier", "Zapis danych po DID"}},
        {0x2F, {"InputOutput Control By ID", "Kontrola I/O"}},
        {0x31, {"Routine Control", "Kontrola procedur"}},
        {0x34, {"Request Download", "Żądanie pobrania"}},
        {0x35, {"Request Upload", "Żądanie wysłania"}},
        {0x36, {"Transfer Data", "Transfer danych"}},
        {0x37, {"Request Transfer Exit", "Zakończenie transferu"}},
        {0x38, {"Request File Transfer", "Transfer pliku"}},
        {0x3D, {"Write Memory By Address", "Zapis pamięci"}},
        {0x3E, {"Tester Present", "Podtrzymanie sesji"}},
        {0x84, {"Secured Data Transmission", "Szyfrowana transmisja"}},
    };

    // ── Typowe DID ──
    m_didDb = {
        {0xF180, "Boot Software Identification"},
        {0xF181, "Application Software Identification"},
        {0xF186, "Active Diagnostic Session"},
        {0xF187, "Vehicle Manufacturer Spare Part Number"},
        {0xF190, "VIN"},
        {0xF191, "Vehicle Manufacturer ECU Hardware Number"},
        {0xF192, "System Supplier Identifier"},
        {0xF194, "System Name or Engine Type"},
        {0xF195, "Repair Shop Code"},
        {0xF197, "Programming Date"},
        {0xF1A0, "ECU Serial Number"},
        {0xF1A5, "ECU Manufacturing Date"},
        {0xF1A6, "Vehicle Speed (km/h)"},
        {0xF1A8, "Engine Speed (rpm)"},
        {0xF1B1, "Engine Coolant Temperature"},
        {0xF1B2, "Battery Voltage"},
        {0xF1B3, "Fuel Level"},
        {0xF1B4, "Engine Oil Pressure"},
        {0xF1B5, "Intake Air Temperature"},
        {0xF1C0, "Odometer Value"},
        {0xF1C1, "Total Engine Run Time"},
        {0xF1C2, "Number of Warm-ups"},
        {0xF1C5, "Ambient Air Temperature"},
        {0xF1D0, "Calibration Identification"},
        {0xF1D1, "Calibration Verification Number"},
    };
}

QString UdsParser::serviceName(uint8_t sid) const {
    auto it = m_sidDb.find(sid);
    return (it != m_sidDb.end()) ? it->name : QString("Unknown SID 0x%1").arg(sid, 2, 16, QChar('0'));
}

QString UdsParser::nrcName(uint8_t nrc) {
    switch (nrc) {
    case 0x10: return "General Reject";
    case 0x11: return "Service Not Supported";
    case 0x12: return "Sub-Function Not Supported";
    case 0x13: return "Incorrect Message Length";
    case 0x14: return "Response Too Long";
    case 0x21: return "Busy – Repeat Request";
    case 0x22: return "Conditions Not Correct";
    case 0x24: return "Request Sequence Error";
    case 0x25: return "No Response From Subnet";
    case 0x26: return "Failure Prevents Execution";
    case 0x31: return "Request Out Of Range";
    case 0x33: return "Security Access Denied";
    case 0x35: return "Invalid Key";
    case 0x36: return "Exceeded Number Of Attempts";
    case 0x37: return "Required Time Delay Not Expired";
    case 0x70: return "Upload/Download Not Accepted";
    case 0x71: return "Transfer Data Suspended";
    case 0x72: return "General Programming Failure";
    case 0x73: return "Wrong Block Sequence Counter";
    case 0x78: return "Request Correctly Received – Response Pending";
    case 0x7E: return "Sub-Function Not Supported In Active Session";
    case 0x7F: return "Service Not Supported In Active Session";
    default:   return QString("NRC 0x%1").arg(nrc, 2, 16, QChar('0'));
    }
}

QString UdsParser::didName(uint16_t did) const {
    auto it = m_didDb.find(did);
    return (it != m_didDb.end()) ? it.value() : QString("DID 0x%1").arg(did, 4, 16, QChar('0'));
}

QString UdsParser::subFunctionName(uint8_t sid, uint8_t sub) {
    if (sid == 0x10) { // Diagnostic Session Control
        switch (sub) {
        case 0x01: return "Default Session";
        case 0x02: return "Programming Session";
        case 0x03: return "Extended Diagnostic Session";
        case 0x04: return "Safety System Session";
        default:   return QString("Session 0x%1").arg(sub, 2, 16, QChar('0'));
        }
    }
    if (sid == 0x11) { // ECU Reset
        switch (sub) {
        case 0x01: return "Hard Reset";
        case 0x02: return "Key Off/On Reset";
        case 0x03: return "Soft Reset";
        case 0x04: return "Enable Rapid Power Shutdown";
        case 0x05: return "Disable Rapid Power Shutdown";
        default:   return QString("Reset 0x%1").arg(sub, 2, 16, QChar('0'));
        }
    }
    if (sid == 0x28) { // Communication Control
        switch (sub) {
        case 0x00: return "Enable Rx and Tx";
        case 0x01: return "Enable Rx, Disable Tx";
        case 0x02: return "Disable Rx, Enable Tx";
        case 0x03: return "Disable Rx and Tx";
        default:   return QString("CommCtrl 0x%1").arg(sub, 2, 16, QChar('0'));
        }
    }
    return QString("Sub 0x%1").arg(sub, 2, 16, QChar('0'));
}

QString UdsParser::decodeResponseData(uint8_t sid, const uint8_t *data, int len) {
    if (len < 1) return "";

    switch (sid) {
    case 0x22: {
        // ReadDataByIdentifier: bajty 0-1 to DID echo, reszta to dane
        if (len < 3) return "(brak danych)";
        uint16_t did = (static_cast<uint16_t>(data[0]) << 8) | data[1];
        int dataLen = len - 2;
        QString hex;
        for (int i = 2; i < len && i < 10; ++i)
            hex += QString("%1 ").arg(data[i], 2, 16, QChar('0')).toUpper();
        return QString("DID 0x%1 → %2 bajtów: %3")
            .arg(did, 4, 16, QChar('0')).arg(dataLen).arg(hex.trimmed());
    }
    case 0x19: {
        // Read DTC Information – uproszczone
        if (len < 3) return "(brak DTC)";
        uint8_t dtcStatus = data[0];
        int numDtcs = (len - 1) / 4;
        QStringList dtcs;
        for (int i = 0; i < numDtcs && i < 3; ++i) {
            int off = 1 + i * 4;
            uint32_t dtc = (static_cast<uint32_t>(data[off]) << 16)
                         | (static_cast<uint32_t>(data[off+1]) << 8)
                         | data[off+2];
            dtcs.append(QString("DTC 0x%1").arg(dtc, 6, 16, QChar('0')));
        }
        return QString("Status 0x%1, %2 DTC: %3")
            .arg(dtcStatus, 2, 16, QChar('0')).arg(numDtcs).arg(dtcs.join(", "));
    }
    default:
        return "(dane binarne)";
    }
}
