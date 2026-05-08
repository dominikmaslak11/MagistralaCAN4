#include "CanOpenFrame.h"
#include <QHash>

CanOpenFrame CanOpenFrame::fromCanFrame(const CanFrame &frame) {
    CanOpenFrame co;
    co.canId = frame.id; co.timestamp = frame.timestamp; co.dlc = frame.dlc;
    for (int i = 0; i < frame.dlc && i < 8; ++i) co.data[i] = frame.data[i];

    if (frame.extended || frame.dlc < 1) return co;

    co.funcCode = (frame.id >> 7) & 0x0F;
    co.nodeId   = frame.id & 0x7F;

    uint16_t id = frame.id & 0x7FF;

    if      (id == 0x000)                co.type = NMT;
    else if (id == 0x080)                co.type = SYNC;
    else if (id >= 0x081 && id <= 0x0FF) co.type = EMCY;
    else if (id >= 0x181 && id <= 0x1FF) co.type = PDO1_TX;
    else if (id >= 0x201 && id <= 0x27F) co.type = PDO1_RX;
    else if (id >= 0x281 && id <= 0x2FF) co.type = PDO2_TX;
    else if (id >= 0x301 && id <= 0x37F) co.type = PDO2_RX;
    else if (id >= 0x381 && id <= 0x3FF) co.type = PDO3_TX;
    else if (id >= 0x401 && id <= 0x47F) co.type = PDO3_RX;
    else if (id >= 0x481 && id <= 0x4FF) co.type = PDO4_TX;
    else if (id >= 0x501 && id <= 0x57F) co.type = PDO4_RX;
    else if (id >= 0x581 && id <= 0x5FF) co.type = SDO_TX;
    else if (id >= 0x601 && id <= 0x67F) co.type = SDO_RX;
    else if (id >= 0x701 && id <= 0x77F) co.type = HEARTBEAT;

    return co;
}

bool CanOpenFrame::isCanOpen(const CanFrame &frame) {
    if (frame.extended) return false;
    uint16_t id = frame.id & 0x7FF;
    return (id == 0x000 || id == 0x080 ||
            (id >= 0x081 && id <= 0x77F));
}

QString CanOpenFrame::toString() const {
    switch (type) {
    case NMT:       return "NMT (0x000)";
    case SYNC:      return "SYNC (0x080)";
    case EMCY:      return QString("EMCY node %1").arg(nodeId);
    case PDO1_TX: case PDO1_RX: return QString("PDO1 node %1").arg(nodeId);
    case SDO_TX: case SDO_RX: return QString("SDO node %1").arg(nodeId);
    case HEARTBEAT: return QString("Heartbeat node %1").arg(nodeId);
    default:        return QString("Func 0x%1 node %2").arg(funcCode, 1, 16).arg(nodeId);
    }
}

// ═══════════════════════════════════════════════════════════════
// CanOpenParser
// ═══════════════════════════════════════════════════════════════

CanOpenParser::CanOpenParser() {
    m_typeDb = {
        {CanOpenFrame::NMT,  {"NMT", "Network Management"}},
        {CanOpenFrame::SYNC, {"SYNC", "Synchronization"}},
        {CanOpenFrame::EMCY, {"EMCY", "Emergency"}},
        {CanOpenFrame::PDO1_TX, {"PDO1 TX", "Process Data 1 Transmit"}},
        {CanOpenFrame::PDO1_RX, {"PDO1 RX", "Process Data 1 Receive"}},
        {CanOpenFrame::PDO2_TX, {"PDO2 TX", "Process Data 2 Transmit"}},
        {CanOpenFrame::PDO2_RX, {"PDO2 RX", "Process Data 2 Receive"}},
        {CanOpenFrame::SDO_TX, {"SDO TX", "Service Data Transmit"}},
        {CanOpenFrame::SDO_RX, {"SDO RX", "Service Data Receive"}},
        {CanOpenFrame::HEARTBEAT, {"Heartbeat", "Node Heartbeat"}},
    };
}

QString CanOpenParser::typeName(CanOpenFrame::Type t) const {
    return m_typeDb.value(t, {"???", "Unknown"}).name;
}

QString CanOpenParser::nmtCommand(uint8_t cmd) {
    switch (cmd) {
    case 0x01: return "Start Remote Node";
    case 0x02: return "Stop Remote Node";
    case 0x80: return "Enter Pre-Operational";
    case 0x81: return "Reset Node";
    case 0x82: return "Reset Communication";
    default:   return QString("NMT 0x%1").arg(cmd, 2, 16, QChar('0'));
    }
}

QString CanOpenParser::emcyCode(uint16_t code) {
    switch (code) {
    case 0x1000: return "Generic Error";
    case 0x5000: return "Device Hardware";
    case 0x6000: return "Device Software";
    case 0x8100: return "Communication";
    case 0x8210: return "PDO Length Exceeded";
    default:     return QString("EMCY 0x%1").arg(code, 4, 16, QChar('0'));
    }
}

QString CanOpenParser::sdoCommand(uint8_t cmd) {
    uint8_t ccs = (cmd >> 5) & 0x7;
    switch (ccs) {
    case 0: return "Download Segment";
    case 1: return "Initiate Download";
    case 2: return "Upload Segment";
    case 3: return "Initiate Upload";
    case 4: return "Abort Transfer";
    default: return "SDO";
    }
}

QString CanOpenParser::heartbeatState(uint8_t state) {
    switch (state) {
    case 0x00: return "Boot-up";
    case 0x04: return "Stopped";
    case 0x05: return "Operational";
    case 0x7F: return "Pre-operational";
    default:   return QString("State 0x%1").arg(state, 2, 16, QChar('0'));
    }
}
