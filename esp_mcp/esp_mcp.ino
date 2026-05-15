#include <SPI.h>
#include <mcp2515.h>

// ---------------------------------------------------------------------------
// Pinout
// ---------------------------------------------------------------------------
#define CAN_CS        5
#define RELAY_COUNT   6

const int relayPins[RELAY_COUNT] = {2, 15, 13, 12, 25, 26};

// ---------------------------------------------------------------------------
// Serial buffer
// ---------------------------------------------------------------------------
#define SERIAL_BUF_SIZE  128

char    serialBuf[SERIAL_BUF_SIZE];
uint8_t serialIdx = 0;

// ---------------------------------------------------------------------------
// CAN
// ---------------------------------------------------------------------------
struct can_frame canMsg;
MCP2515 mcp2515(CAN_CS);

// ---------------------------------------------------------------------------
// Relay helpers (silent — caller decides whether to print)
// ---------------------------------------------------------------------------
bool setRelay(uint8_t id, bool state) {
    if (id >= RELAY_COUNT) return false;
    digitalWrite(relayPins[id], state ? HIGH : LOW);
    return true;
}

bool getRelay(uint8_t id) {
    if (id >= RELAY_COUNT) return false;
    return digitalRead(relayPins[id]) == HIGH;
}

// ---------------------------------------------------------------------------
// CAN frame printer:  PREFIX  EXT|STD  ID  DLC  B0 B1 … BN
// ---------------------------------------------------------------------------
void printCanFrame(const char* prefix, bool extended, uint32_t id,
                   uint8_t dlc, uint8_t* data) {
    Serial.print(prefix);
    Serial.print(' ');
    Serial.print(extended ? "EXT" : "STD");
    Serial.print(' ');

    // zero-padded hex ID
    if (extended) {
        if (id < 0x10000000) Serial.print('0');
        if (id < 0x1000000)  Serial.print('0');
        if (id < 0x100000)   Serial.print('0');
        if (id < 0x10000)    Serial.print('0');
        if (id < 0x1000)     Serial.print('0');
        if (id < 0x100)      Serial.print('0');
        if (id < 0x10)       Serial.print('0');
    } else {
        if (id < 0x100) Serial.print('0');
        if (id < 0x10)  Serial.print('0');
    }
    Serial.print(id, HEX);

    Serial.print(' ');
    Serial.print(dlc);

    for (uint8_t i = 0; i < dlc; i++) {
        Serial.print(' ');
        if (data[i] < 0x10) Serial.print('0');
        Serial.print(data[i], HEX);
    }
    Serial.println();
}

// ---------------------------------------------------------------------------
// CAN → Serial  (runs every loop iteration when a frame is available)
// ---------------------------------------------------------------------------
void handleCanRx() {
    bool     isExtended = canMsg.can_id & CAN_EFF_FLAG;
    uint32_t id         = canMsg.can_id & CAN_EFF_MASK;

    // echo every received frame to USB
    printCanFrame("RX", isExtended, id, canMsg.can_dlc, canMsg.data);

    // EXT ID 0x1421003F: relay 0 ON/OFF wg data[1] lub data[3]
    if (isExtended && id == 0x1421003F) {
        if (canMsg.can_dlc >= 2) {
            if (canMsg.data[1] == 0x01) {
                setRelay(0, true);
            } else if (canMsg.data[1] == 0x00) {
                setRelay(0, false);
            }
        }
        if (canMsg.can_dlc >= 4) {
            if (canMsg.data[3] == 0x01) {
                setRelay(0, true);
            } else if (canMsg.data[3] == 0x00) {
                setRelay(0, false);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Serial → CAN: send a single frame
// ---------------------------------------------------------------------------
bool sendCanFrame(bool extended, uint32_t id, uint8_t dlc, uint8_t* data) {
    canMsg.can_id  = extended ? (id | CAN_EFF_FLAG) : id;
    canMsg.can_dlc = dlc;
    uint8_t len = (dlc > 8) ? 8 : dlc;
    memcpy(canMsg.data, data, len);

    return mcp2515.sendMessage(&canMsg) == MCP2515::ERROR_OK;
}

// ---------------------------------------------------------------------------
// Serial command parser
// ---------------------------------------------------------------------------
void processCommand(char* cmd) {
    // strip leading whitespace
    while (*cmd == ' ' || *cmd == '\t') cmd++;
    if (*cmd == '\0') return;

    char* token = strtok(cmd, " \t");
    if (token == nullptr) return;

    // --- SIM EXT|STD <ID_HEX> <DLC> <B0> … <BN> — inject frame into RX handler --
    if (strcasecmp(token, "SIM") == 0) {
        char* typeStr = strtok(nullptr, " \t");
        char* idStr   = strtok(nullptr, " \t");
        char* dlcStr  = strtok(nullptr, " \t");

        if (!typeStr || !idStr || !dlcStr) {
            Serial.println("ERR usage: SIM <EXT|STD> <ID_HEX> <DLC> <B0>..<BN>");
            return;
        }

        bool extended = (strcasecmp(typeStr, "EXT") == 0);
        uint32_t id   = strtoul(idStr, nullptr, 16);
        uint8_t  dlc  = atoi(dlcStr);

        if (dlc > 8) { Serial.println("ERR DLC must be 0-8"); return; }

        canMsg.can_id  = extended ? (id | CAN_EFF_FLAG) : id;
        canMsg.can_dlc = dlc;
        memset(canMsg.data, 0, 8);
        for (uint8_t i = 0; i < dlc; i++) {
            char* byteStr = strtok(nullptr, " \t");
            if (!byteStr) { Serial.println("ERR not enough data bytes"); return; }
            canMsg.data[i] = strtoul(byteStr, nullptr, 16);
        }

        Serial.println("OK SIM");
        handleCanRx();

    // --- TX EXT|STD <ID_HEX> <DLC> <B0> … <BN> ---------------------------------
    } else if (strcasecmp(token, "TX") == 0) {
        char* typeStr = strtok(nullptr, " \t");
        char* idStr   = strtok(nullptr, " \t");
        char* dlcStr  = strtok(nullptr, " \t");

        if (!typeStr || !idStr || !dlcStr) {
            Serial.println("ERR usage: TX <EXT|STD> <ID_HEX> <DLC> <B0>..<BN>");
            return;
        }

        bool extended = (strcasecmp(typeStr, "EXT") == 0);
        uint32_t id   = strtoul(idStr,  nullptr, 16);
        uint8_t  dlc  = atoi(dlcStr);

        if (dlc > 8) {
            Serial.println("ERR DLC must be 0-8");
            return;
        }

        uint8_t data[8] = {0};
        for (uint8_t i = 0; i < dlc; i++) {
            char* byteStr = strtok(nullptr, " \t");
            if (!byteStr) {
                Serial.println("ERR not enough data bytes");
                return;
            }
            data[i] = strtoul(byteStr, nullptr, 16);
        }

        if (sendCanFrame(extended, id, dlc, data)) {
            Serial.println("OK TX");
        } else {
            Serial.println("ERR TX failed");
        }

    // --- RELAY <ID> <0|1|ON|OFF> -------------------------------------------------
    } else if (strcasecmp(token, "RELAY") == 0 || strcasecmp(token, "R") == 0) {
        char* idStr    = strtok(nullptr, " \t");
        char* stateStr = strtok(nullptr, " \t");

        if (!idStr || !stateStr) {
            Serial.println("ERR usage: RELAY <ID> <0|1|ON|OFF>");
            return;
        }

        uint8_t relayId = atoi(idStr);
        bool    state   = (strcmp(stateStr, "1")  == 0 ||
                           strcasecmp(stateStr, "ON")  == 0 ||
                           strcasecmp(stateStr, "TRUE") == 0);

        if (!setRelay(relayId, state)) {
            Serial.print("ERR relay ");
            Serial.print(relayId);
            Serial.println(" out of range (0-5)");
        } else {
            Serial.print("OK RELAY ");
            Serial.print(relayId);
            Serial.print(" = ");
            Serial.println(state ? "ON" : "OFF");
        }

    // --- STATUS — dump relay states ----------------------------------------------
    } else if (strcasecmp(token, "STATUS") == 0 || strcasecmp(token, "ST") == 0) {
        Serial.print("STATUS relays: ");
        for (uint8_t i = 0; i < RELAY_COUNT; i++) {
            Serial.print(getRelay(i) ? "1" : "0");
        }
        Serial.println();

    // --- BAUD <kbps> — reconfigure CAN bitrate on the fly ----------------------
    } else if (strcasecmp(token, "BAUD") == 0) {
        char* rateStr = strtok(nullptr, " \t");
        if (!rateStr) { Serial.println("ERR usage: BAUD <kbps>"); return; }
        int kbps = atoi(rateStr);
        CAN_SPEED speed;
        switch (kbps) {
            case 10:   speed = CAN_10KBPS;  break;
            case 20:   speed = CAN_20KBPS;  break;
            case 50:   speed = CAN_50KBPS;  break;
            case 100:  speed = CAN_100KBPS; break;
            case 125:  speed = CAN_125KBPS; break;
            case 250:  speed = CAN_250KBPS; break;
            case 500:  speed = CAN_500KBPS; break;
            case 1000: speed = CAN_1000KBPS; break;
            default:
                Serial.println("ERR BAUD: supported 10,20,50,100,125,250,500,1000 kbps");
                return;
        }
        mcp2515.reset();
        mcp2515.setFilterMask(MCP2515::MASK0, true, 0x00000000);
        mcp2515.setFilterMask(MCP2515::MASK1, true, 0x00000000);
        mcp2515.setBitrate(speed);
        mcp2515.setNormalMode();
        Serial.print("OK BAUD ");
        Serial.print(kbps);
        Serial.println(" kbps");

    // --- HELP --------------------------------------------------------------------
    } else if (strcasecmp(token, "HELP") == 0 || strcmp(token, "?") == 0) {
        Serial.println("--- ESP MCP CAN Bridge ---");
        Serial.println("Commands:");
        Serial.println("  TX <EXT|STD> <ID> <DLC> <B0..BN>   send CAN frame");
        Serial.println("  RELAY <ID> <0|1|ON|OFF>            set relay");
        Serial.println("  STATUS                              show relay states");
        Serial.println("  BAUD <kbps>                        reconfigure CAN speed");
        Serial.println("  HELP                                this info");
        Serial.println("Incoming CAN frames: RX <EXT|STD> <ID> <DLC> <B0..BN>");

    } else {
        Serial.print("ERR unknown: ");
        Serial.println(token);
        Serial.println("  Type HELP");
    }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(300);   // let CH340 settle after USB enumeration

    Serial.println();
    Serial.println("=== ESP MCP CAN Bridge ===");

    // relays
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        pinMode(relayPins[i], OUTPUT);
        digitalWrite(relayPins[i], LOW);
    }
    Serial.print("Relays: ");
    Serial.print(RELAY_COUNT);
    Serial.println(" (all OFF)");

    // CAN
    SPI.begin();
    if (mcp2515.reset() == MCP2515::ERROR_OK) {
        Serial.println("CAN OK");
    } else {
        Serial.println("CAN FAIL – halted");
        while (1);
    }

    // Accept all frames on both RX buffers (mask=0 → all bits are don't-care)
    mcp2515.setFilterMask(MCP2515::MASK0, true, 0x00000000);
    mcp2515.setFilterMask(MCP2515::MASK1, true, 0x00000000);
    mcp2515.setBitrate(CAN_250KBPS);
    mcp2515.setNormalMode();

    Serial.println("CAN BUS Ready (250 kbps, all IDs)");
    Serial.println("Type HELP for commands");
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
void loop() {
    // CAN → Serial
    if (mcp2515.readMessage(&canMsg) == MCP2515::ERROR_OK) {
        handleCanRx();
    }

    // Serial → CAN / commands
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (serialIdx > 0) {
                serialBuf[serialIdx] = '\0';
                processCommand(serialBuf);
                serialIdx = 0;
            }
        } else if (serialIdx < SERIAL_BUF_SIZE - 1) {
            serialBuf[serialIdx++] = c;
        }
    }
}
