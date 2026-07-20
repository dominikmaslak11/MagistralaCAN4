// esp_mcp.ino — GVRET bridge + relay control
// Protocol: EVTV GVRET binary over USB Serial (115200 baud)
// Compatible with SavvyCAN and MagistralaCAN4
//
// Relay 0 (GPIO 2): EXT ID 0x1421003F, data[1] bit 0
//   bit 0 == 1 → ON,  bit 0 == 0 → OFF
//
// Remaining relays (1-5) on GPIO 15,13,12,25,26 — manual via ASCII cmds.
// ASCII commands work in parallel with GVRET (use Serial Monitor when not
// connected to MagistralaCAN4, or while connected — responses are ignored
// by the GVRET parser on the PC side).

#include <SPI.h>
#include <mcp2515.h>

// ──────────────────────────────────────────────────────────────
// Pinout
// ──────────────────────────────────────────────────────────────
#define CAN_CS      5
#define RELAY_COUNT 6
const int relayPins[RELAY_COUNT] = {2, 15, 13, 12, 25, 26};

// ──────────────────────────────────────────────────────────────
// GVRET protocol constants
// ──────────────────────────────────────────────────────────────
#define GVRET_MAGIC   0xF1
#define CMD_FRAME_OUT 0x00   // device → PC: frame received
#define CMD_FRAME_IN  0x01   // PC → device: send frame
#define CMD_TIME_SYNC 0x02   // timestamp echo
#define CMD_SETUP_BUS 0x06   // configure CAN speed
#define CMD_GET_INFO  0x08   // device capabilities
#define CMD_KEEPALIVE 0x09   // heartbeat echo

// ──────────────────────────────────────────────────────────────
// Ring buffer — GVRET binary input
// ──────────────────────────────────────────────────────────────
#define RB_SIZE 512
static uint8_t  rb[RB_SIZE];
static uint16_t rbHead = 0, rbTail = 0;

static inline void     rbPush(uint8_t b)     { uint16_t n=(rbHead+1)%RB_SIZE; if(n!=rbTail){rb[rbHead]=b;rbHead=n;} }
static inline uint16_t rbAvail()             { return (rbHead-rbTail+RB_SIZE)%RB_SIZE; }
static inline uint8_t  rbPeek(uint16_t off)  { return rb[(rbTail+off)%RB_SIZE]; }
static inline uint8_t  rbPop()               { uint8_t b=rb[rbTail]; rbTail=(rbTail+1)%RB_SIZE; return b; }
static inline void     rbConsume(uint16_t n) { rbTail=(rbTail+n)%RB_SIZE; }

// ──────────────────────────────────────────────────────────────
// ASCII command buffer — parallel with GVRET
// ──────────────────────────────────────────────────────────────
#define ASCII_BUF 64
static char    asciiBuf[ASCII_BUF];
static uint8_t asciiIdx = 0;

// ──────────────────────────────────────────────────────────────
// State
// ──────────────────────────────────────────────────────────────
static MCP2515   mcp2515(CAN_CS);
static can_frame rxMsg, txMsg;
static uint32_t  canSpeed   = 250000;
static bool      listenOnly = false;

// ──────────────────────────────────────────────────────────────
// Relay helpers
// ──────────────────────────────────────────────────────────────
static bool setRelay(uint8_t id, bool state) {
    if (id >= RELAY_COUNT) return false;
    digitalWrite(relayPins[id], state ? HIGH : LOW);
    return true;
}
static bool getRelay(uint8_t id) {
    if (id >= RELAY_COUNT) return false;
    return digitalRead(relayPins[id]) == HIGH;
}

// ──────────────────────────────────────────────────────────────
// CAN speed helpers
// ──────────────────────────────────────────────────────────────
static CAN_SPEED bpsToCanSpeed(uint32_t bps) {
    if (bps >= 900000) return CAN_1000KBPS;
    if (bps >= 600000) return CAN_500KBPS;
    if (bps >= 350000) return CAN_500KBPS;
    if (bps >= 225000) return CAN_250KBPS;
    if (bps >= 110000) return CAN_125KBPS;
    if (bps >= 75000)  return CAN_100KBPS;
    if (bps >= 35000)  return CAN_50KBPS;
    if (bps >= 15000)  return CAN_20KBPS;
    return CAN_10KBPS;
}

static void applyCanSpeed(uint32_t bps, bool lo) {
    canSpeed   = bps;
    listenOnly = lo;
    mcp2515.reset();
    mcp2515.setFilterMask(MCP2515::MASK0, true, 0x00000000);
    mcp2515.setFilterMask(MCP2515::MASK1, true, 0x00000000);
    mcp2515.setBitrate(bpsToCanSpeed(bps));
    if (lo) mcp2515.setListenOnlyMode();
    else    mcp2515.setNormalMode();
}

// ──────────────────────────────────────────────────────────────
// GVRET: forward received CAN frame to PC
// ──────────────────────────────────────────────────────────────
static void sendFrameToPC(const can_frame &f, uint8_t busNum = 0) {
    uint8_t buf[20];
    uint8_t idx = 0;
    uint32_t ts = millis();
    uint32_t id = f.can_id & ((f.can_id & CAN_EFF_FLAG) ? CAN_EFF_MASK : CAN_SFF_MASK);
    if (f.can_id & CAN_EFF_FLAG) id |= 0x80000000UL;
    uint8_t dlc = f.can_dlc & 0x0F;

    buf[idx++] = GVRET_MAGIC;
    buf[idx++] = CMD_FRAME_OUT;
    buf[idx++] = (uint8_t)(ts);
    buf[idx++] = (uint8_t)(ts >>  8);
    buf[idx++] = (uint8_t)(ts >> 16);
    buf[idx++] = (uint8_t)(ts >> 24);
    buf[idx++] = (uint8_t)(id);
    buf[idx++] = (uint8_t)(id >>  8);
    buf[idx++] = (uint8_t)(id >> 16);
    buf[idx++] = (uint8_t)(id >> 24);
    buf[idx++] = (uint8_t)(((busNum & 0x0F) << 4) | dlc);
    for (uint8_t i = 0; i < dlc && i < 8; i++) buf[idx++] = f.data[i];
    Serial.write(buf, idx);
}

// ──────────────────────────────────────────────────────────────
// GVRET: device info response (CMD_GET_INFO)
// ──────────────────────────────────────────────────────────────
static void sendDeviceInfo() {
    uint8_t buf[96];
    uint8_t idx = 0;
    buf[idx++] = GVRET_MAGIC;
    buf[idx++] = CMD_GET_INFO;
    buf[idx++] = 1;   // numBuses
    buf[idx++] = (uint8_t)(canSpeed);
    buf[idx++] = (uint8_t)(canSpeed >>  8);
    buf[idx++] = (uint8_t)(canSpeed >> 16);
    buf[idx++] = (uint8_t)(canSpeed >> 24);
    buf[idx++] = 1;                       // enabled
    buf[idx++] = 0;                       // not CAN-FD
    buf[idx++] = listenOnly ? 1 : 0;
    const char *bd = __DATE__ " " __TIME__;
    uint8_t len = strlen(bd);
    memcpy(buf+idx, bd, len+1); idx += len+1;
    const char *sw = "0.2.0";
    len = strlen(sw); memcpy(buf+idx, sw, len+1); idx += len+1;
    const char *hw = "ESP32-MCP2515-RELAY";
    len = strlen(hw); memcpy(buf+idx, hw, len+1); idx += len+1;
    Serial.write(buf, idx);
}

// ──────────────────────────────────────────────────────────────
// Relay logic — applied to every CAN frame (RX + TX from PC)
//
// EXT ID 0x1421003F, data[1] bit 0:
//   1 → relay 0 ON,  0 → relay 0 OFF
// ──────────────────────────────────────────────────────────────
static void applyRelayLogic(const can_frame &f) {
    if (!(f.can_id & CAN_EFF_FLAG)) return;
    uint32_t id = f.can_id & CAN_EFF_MASK;
    if (id != 0x1421003FUL || f.can_dlc < 2) return;
    setRelay(0, (f.data[1] & 0x01) != 0);
}

// ──────────────────────────────────────────────────────────────
// GVRET binary command parser (PC → ESP)
// ──────────────────────────────────────────────────────────────
static void processGVRET() {
    while (true) {
        // Skip bytes until 0xF1
        while (rbAvail() > 0 && rbPeek(0) != GVRET_MAGIC) rbPop();
        if (rbAvail() < 2) return;

        uint8_t cmd = rbPeek(1);
        switch (cmd) {

        case CMD_GET_INFO:
            rbConsume(2);
            sendDeviceInfo();
            break;

        case CMD_KEEPALIVE: {
            rbConsume(2);
            uint8_t resp[2] = {GVRET_MAGIC, CMD_KEEPALIVE};
            Serial.write(resp, 2);
            break;
        }

        case CMD_TIME_SYNC: {
            if (rbAvail() < 6) return;
            rbConsume(2);
            uint8_t ts[4];
            for (int i = 0; i < 4; i++) ts[i] = rbPop();
            uint8_t resp[6] = {GVRET_MAGIC, CMD_TIME_SYNC, ts[0], ts[1], ts[2], ts[3]};
            Serial.write(resp, 6);
            break;
        }

        case CMD_SETUP_BUS: {
            if (rbAvail() < 8) return;
            rbConsume(2);
            uint32_t spd = (uint32_t)rbPop()
                         | ((uint32_t)rbPop() <<  8)
                         | ((uint32_t)rbPop() << 16)
                         | ((uint32_t)rbPop() << 24);
            uint8_t busNum = rbPop();
            uint8_t flags  = rbPop();
            if (spd >= 10000 && spd <= 1000000)
                applyCanSpeed(spd, (flags & 0x01) != 0);
            uint8_t resp[8] = {
                GVRET_MAGIC, CMD_SETUP_BUS,
                (uint8_t)(canSpeed),       (uint8_t)(canSpeed >>  8),
                (uint8_t)(canSpeed >> 16), (uint8_t)(canSpeed >> 24),
                busNum, flags
            };
            Serial.write(resp, 8);
            break;
        }

        case CMD_FRAME_IN: {
            if (rbAvail() < 7) return;
            uint8_t len_bus = rbPeek(6);
            uint8_t dlc = len_bus & 0x0F;
            if (rbAvail() < (uint16_t)(7 + dlc)) return;
            rbConsume(2);
            uint32_t id = (uint32_t)rbPop()
                        | ((uint32_t)rbPop() <<  8)
                        | ((uint32_t)rbPop() << 16)
                        | ((uint32_t)rbPop() << 24);
            rbPop();  // len_bus consumed
            txMsg.can_id  = id & 0x1FFFFFFFUL;
            if (id & 0x80000000UL) txMsg.can_id |= CAN_EFF_FLAG;
            txMsg.can_dlc = dlc;
            for (uint8_t i = 0; i < dlc; i++) txMsg.data[i] = rbPop();
            if (!listenOnly) {
                mcp2515.sendMessage(&txMsg);
                applyRelayLogic(txMsg);
            }
            break;
        }

        default:
            rbPop();  // unknown — discard magic byte, re-scan
            break;
        }
    }
}

// ──────────────────────────────────────────────────────────────
// ASCII command parser (Serial Monitor; responses ignored by
// MagistralaCAN4's GVRET parser on the PC side)
// ──────────────────────────────────────────────────────────────
static void processAsciiCommand(char *cmd) {
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    if (*cmd == '\0') return;

    char *token = strtok(cmd, " \t");
    if (!token) return;

    // RELAY <id> <0|1|ON|OFF>
    if (strcasecmp(token, "RELAY") == 0 || strcasecmp(token, "R") == 0) {
        char *idStr    = strtok(nullptr, " \t");
        char *stateStr = strtok(nullptr, " \t");
        if (!idStr || !stateStr) { Serial.println("ERR usage: RELAY <ID> <0|1|ON|OFF>"); return; }
        uint8_t rid = (uint8_t)atoi(idStr);
        bool state = strcmp(stateStr, "1") == 0 || strcasecmp(stateStr, "ON") == 0;
        if (!setRelay(rid, state)) {
            Serial.println("ERR relay out of range (0-5)");
        } else {
            Serial.print("OK RELAY "); Serial.print(rid);
            Serial.print(" = "); Serial.println(state ? "ON" : "OFF");
        }

    // STATUS
    } else if (strcasecmp(token, "STATUS") == 0 || strcasecmp(token, "ST") == 0) {
        Serial.print("STATUS relays: ");
        for (uint8_t i = 0; i < RELAY_COUNT; i++) Serial.print(getRelay(i) ? "1" : "0");
        Serial.println();

    // TX <EXT|STD> <ID> <DLC> <B0..BN>
    } else if (strcasecmp(token, "TX") == 0) {
        char *typeStr = strtok(nullptr, " \t");
        char *idStr   = strtok(nullptr, " \t");
        char *dlcStr  = strtok(nullptr, " \t");
        if (!typeStr || !idStr || !dlcStr) { Serial.println("ERR usage: TX <EXT|STD> <ID> <DLC> <B0..BN>"); return; }
        bool ext = strcasecmp(typeStr, "EXT") == 0;
        uint32_t id = strtoul(idStr, nullptr, 16);
        uint8_t  dlc = (uint8_t)atoi(dlcStr);
        if (dlc > 8) { Serial.println("ERR DLC 0-8"); return; }
        can_frame f; f.can_id = ext ? (id | CAN_EFF_FLAG) : id; f.can_dlc = dlc;
        memset(f.data, 0, 8);
        for (uint8_t i = 0; i < dlc; i++) {
            char *b = strtok(nullptr, " \t");
            if (!b) { Serial.println("ERR not enough bytes"); return; }
            f.data[i] = (uint8_t)strtoul(b, nullptr, 16);
        }
        if (mcp2515.sendMessage(&f) == MCP2515::ERROR_OK) {
            applyRelayLogic(f);
            Serial.println("OK TX");
        } else {
            Serial.println("ERR TX failed");
        }

    // SIM <EXT|STD> <ID> <DLC> <B0..BN>  — inject frame to GVRET + relay (no CAN TX)
    } else if (strcasecmp(token, "SIM") == 0) {
        char *typeStr = strtok(nullptr, " \t");
        char *idStr   = strtok(nullptr, " \t");
        char *dlcStr  = strtok(nullptr, " \t");
        if (!typeStr || !idStr || !dlcStr) { Serial.println("ERR usage: SIM <EXT|STD> <ID> <DLC> <B0..BN>"); return; }
        bool ext = strcasecmp(typeStr, "EXT") == 0;
        uint32_t id = strtoul(idStr, nullptr, 16);
        uint8_t  dlc = (uint8_t)atoi(dlcStr);
        if (dlc > 8) { Serial.println("ERR DLC 0-8"); return; }
        can_frame f; f.can_id = ext ? (id | CAN_EFF_FLAG) : id; f.can_dlc = dlc;
        memset(f.data, 0, 8);
        for (uint8_t i = 0; i < dlc; i++) {
            char *b = strtok(nullptr, " \t");
            if (!b) { Serial.println("ERR not enough bytes"); return; }
            f.data[i] = (uint8_t)strtoul(b, nullptr, 16);
        }
        sendFrameToPC(f, 0);
        applyRelayLogic(f);
        Serial.println("OK SIM");

    // BAUD <kbps>
    } else if (strcasecmp(token, "BAUD") == 0) {
        char *rateStr = strtok(nullptr, " \t");
        if (!rateStr) { Serial.println("ERR usage: BAUD <kbps>"); return; }
        int kbps = atoi(rateStr);
        if (kbps < 10 || kbps > 1000) { Serial.println("ERR supported: 10,20,50,100,125,250,500,1000 kbps"); return; }
        applyCanSpeed((uint32_t)kbps * 1000, listenOnly);
        Serial.print("OK BAUD "); Serial.print(kbps); Serial.println(" kbps");

    // HELP
    } else if (strcasecmp(token, "HELP") == 0 || strcmp(token, "?") == 0) {
        Serial.println("--- GVRET+Relay Bridge ---");
        Serial.println("RELAY <ID> <0|1|ON|OFF>             set relay manually");
        Serial.println("STATUS                               relay states (6 bits)");
        Serial.println("TX  <EXT|STD> <ID> <DLC> <B0..BN>  send CAN frame");
        Serial.println("SIM <EXT|STD> <ID> <DLC> <B0..BN>  inject to GVRET+relay");
        Serial.println("BAUD <kbps>                         change CAN speed");
        Serial.println("Auto: EXT 0x1421003F data[1] bit0 -> relay 0 ON/OFF");
    } else {
        Serial.print("ERR unknown: "); Serial.println(token);
        Serial.println("  Type HELP");
    }
}

// ──────────────────────────────────────────────────────────────
// Setup
// ──────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);

    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        pinMode(relayPins[i], OUTPUT);
        digitalWrite(relayPins[i], LOW);
    }

    SPI.begin();
    while (mcp2515.reset() != MCP2515::ERROR_OK) delay(500);
    mcp2515.setFilterMask(MCP2515::MASK0, true, 0x00000000);
    mcp2515.setFilterMask(MCP2515::MASK1, true, 0x00000000);
    mcp2515.setBitrate(CAN_250KBPS);
    mcp2515.setNormalMode();
}

// ──────────────────────────────────────────────────────────────
// Main loop
// ──────────────────────────────────────────────────────────────
void loop() {
    // Read serial: push ALL bytes to GVRET ring buffer;
    // printable bytes also accumulate in ASCII command buffer.
    while (Serial.available()) {
        uint8_t b = (uint8_t)Serial.read();
        rbPush(b);
        if (b == '\n' || b == '\r') {
            if (asciiIdx > 0) {
                asciiBuf[asciiIdx] = '\0';
                processAsciiCommand(asciiBuf);
                asciiIdx = 0;
            }
        } else if (b >= 0x20 && b < 0x80 && asciiIdx < ASCII_BUF - 1) {
            asciiBuf[asciiIdx++] = (char)b;
        }
    }

    // Parse GVRET binary packets from ring buffer
    processGVRET();

    // CAN RX → GVRET to PC + relay logic
    if (mcp2515.readMessage(&rxMsg) == MCP2515::ERROR_OK) {
        sendFrameToPC(rxMsg, 0);
        applyRelayLogic(rxMsg);
    }
}
