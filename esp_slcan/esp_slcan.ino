// ESP32 SLCAN bridge — compatible with SavvyCAN and MagistralaCAN4 SlCanDriver
// Protocol: Lawicel SLCAN over USB Serial (115200 baud)
//
// Supported commands:
//   Sn     — set CAN speed (0=10k 1=20k 2=50k 3=100k 4=125k 5=250k 6=500k 7=800k 8=1Mbit)
//   O      — open channel (normal mode)
//   L      — open channel (listen-only mode)
//   C      — close channel
//   tIIILDD..  — transmit standard frame (11-bit ID)
//   TIIIIIIIILDD.. — transmit extended frame (29-bit ID)
//   V      — hardware version  → V1010
//   N      — serial number     → NA123
//   F      — read status flags → F00

#include <SPI.h>
#include <mcp2515.h>

// ---------------------------------------------------------------------------
// Pinout
// ---------------------------------------------------------------------------
#define CAN_CS  5

// ---------------------------------------------------------------------------
// Serial buffer
// ---------------------------------------------------------------------------
#define SERIAL_BUF_SIZE 128

static char    serialBuf[SERIAL_BUF_SIZE];
static uint8_t serialIdx = 0;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static MCP2515   mcp(CAN_CS);
static can_frame rxMsg, txMsg;
static bool      channelOpen    = false;
static bool      listenOnly     = false;
static CAN_SPEED currentSpeed   = CAN_250KBPS;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void slOK()  { Serial.write('\r'); }
static void slERR() { Serial.write('\a'); }   // BEL = SLCAN error response

static int8_t hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool parseHex(const char* s, uint8_t digits, uint32_t& out) {
    out = 0;
    for (uint8_t i = 0; i < digits; i++) {
        int8_t n = hexNibble(s[i]);
        if (n < 0) return false;
        out = (out << 4) | (uint32_t)n;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Send received CAN frame in SLCAN format
// ---------------------------------------------------------------------------
static void sendSlcanFrame(const can_frame& f) {
    bool     ext = f.can_id & CAN_EFF_FLAG;
    uint32_t id  = f.can_id & (ext ? CAN_EFF_MASK : CAN_SFF_MASK);
    uint8_t  dlc = f.can_dlc & 0x0F;

    char buf[12];
    if (ext) {
        snprintf(buf, sizeof(buf), "T%08lX%u", (unsigned long)id, dlc);
    } else {
        snprintf(buf, sizeof(buf), "t%03lX%u", (unsigned long)id, dlc);
    }
    Serial.print(buf);

    for (uint8_t i = 0; i < dlc; i++) {
        snprintf(buf, sizeof(buf), "%02X", f.data[i]);
        Serial.print(buf);
    }
    Serial.write('\r');
}

// ---------------------------------------------------------------------------
// Apply CAN speed and (re)init MCP2515
// ---------------------------------------------------------------------------
static void applySpeed(CAN_SPEED speed, bool listenOnlyMode) {
    mcp.reset();
    mcp.setFilterMask(MCP2515::MASK0, true, 0x00000000);
    mcp.setFilterMask(MCP2515::MASK1, true, 0x00000000);
    mcp.setBitrate(speed);
    if (listenOnlyMode) {
        mcp.setListenOnlyMode();
    } else {
        mcp.setNormalMode();
    }
}

// ---------------------------------------------------------------------------
// SLCAN command parser
// ---------------------------------------------------------------------------
static void processCommand(char* cmd) {
    uint8_t len = strlen(cmd);
    if (len == 0) return;

    char c = cmd[0];

    // Sn — set bitrate (must be done before O/L)
    if (c == 'S' && len == 2) {
        const CAN_SPEED table[] = {
            CAN_10KBPS, CAN_20KBPS, CAN_50KBPS, CAN_100KBPS,
            CAN_125KBPS, CAN_250KBPS, CAN_500KBPS, CAN_500KBPS, CAN_1000KBPS
        };
        uint8_t idx = cmd[1] - '0';
        if (idx > 8) { slERR(); return; }
        currentSpeed = table[idx];
        slOK();
        return;
    }

    // O — open channel (normal mode)
    if (c == 'O' && len == 1) {
        listenOnly  = false;
        channelOpen = true;
        applySpeed(currentSpeed, false);
        slOK();
        return;
    }

    // L — open channel (listen-only)
    if (c == 'L' && len == 1) {
        listenOnly  = true;
        channelOpen = true;
        applySpeed(currentSpeed, true);
        slOK();
        return;
    }

    // C — close channel
    if (c == 'C' && len == 1) {
        channelOpen = false;
        slOK();
        return;
    }

    // V — version
    if (c == 'V' && len == 1) {
        Serial.print("V1010\r");
        return;
    }

    // N — serial number
    if (c == 'N' && len == 1) {
        Serial.print("NA123\r");
        return;
    }

    // F — status flags
    if (c == 'F' && len == 1) {
        Serial.print("F00\r");
        return;
    }

    // Z — timestamp on/off (acknowledge but ignore)
    if (c == 'Z' && len == 2) {
        slOK();
        return;
    }

    // t — transmit standard frame: tIIILDD..
    if (c == 't' && len >= 5) {
        uint32_t id;
        if (!parseHex(cmd + 1, 3, id)) { slERR(); return; }
        uint8_t dlc = cmd[4] - '0';
        if (dlc > 8 || len < (uint8_t)(5 + dlc * 2)) { slERR(); return; }

        txMsg.can_id  = id & CAN_SFF_MASK;
        txMsg.can_dlc = dlc;
        for (uint8_t i = 0; i < dlc; i++) {
            uint32_t b;
            if (!parseHex(cmd + 5 + i * 2, 2, b)) { slERR(); return; }
            txMsg.data[i] = (uint8_t)b;
        }

        if (!listenOnly && mcp.sendMessage(&txMsg) == MCP2515::ERROR_OK) slOK();
        else slERR();
        return;
    }

    // T — transmit extended frame: TIIIIIIIILDD..
    if (c == 'T' && len >= 10) {
        uint32_t id;
        if (!parseHex(cmd + 1, 8, id)) { slERR(); return; }
        uint8_t dlc = cmd[9] - '0';
        if (dlc > 8 || len < (uint8_t)(10 + dlc * 2)) { slERR(); return; }

        txMsg.can_id  = (id & CAN_EFF_MASK) | CAN_EFF_FLAG;
        txMsg.can_dlc = dlc;
        for (uint8_t i = 0; i < dlc; i++) {
            uint32_t b;
            if (!parseHex(cmd + 10 + i * 2, 2, b)) { slERR(); return; }
            txMsg.data[i] = (uint8_t)b;
        }

        if (!listenOnly && mcp.sendMessage(&txMsg) == MCP2515::ERROR_OK) slOK();
        else slERR();
        return;
    }

    slERR();
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(200);

    SPI.begin();
    if (mcp.reset() != MCP2515::ERROR_OK) {
        // MCP2515 not responding — signal error, keep retrying
        while (mcp.reset() != MCP2515::ERROR_OK) delay(500);
    }

    // Default: 250 kbps, all frames accepted, channel closed
    mcp.setFilterMask(MCP2515::MASK0, true, 0x00000000);
    mcp.setFilterMask(MCP2515::MASK1, true, 0x00000000);
    mcp.setBitrate(CAN_250KBPS);
    mcp.setNormalMode();
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
void loop() {
    // CAN → Serial (only when channel is open)
    if (channelOpen && mcp.readMessage(&rxMsg) == MCP2515::ERROR_OK) {
        sendSlcanFrame(rxMsg);
    }

    // Serial → command parser
    while (Serial.available()) {
        char ch = Serial.read();
        if (ch == '\r' || ch == '\n') {
            if (serialIdx > 0) {
                serialBuf[serialIdx] = '\0';
                processCommand(serialBuf);
                serialIdx = 0;
            }
        } else if (serialIdx < SERIAL_BUF_SIZE - 1) {
            serialBuf[serialIdx++] = ch;
        }
    }
}
