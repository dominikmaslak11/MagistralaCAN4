// ESP32 GVRET bridge — compatible with SavvyCAN (Serial Connection)
// Protocol: EVTV GVRET binary over USB Serial (115200 baud)
//
// SavvyCAN: Connection → Add New Device Connection → Serial Connection → COM3

#include <SPI.h>
#include <mcp2515.h>

// ---------------------------------------------------------------------------
// Pinout
// ---------------------------------------------------------------------------
#define CAN_CS       5
#define CRYSTAL_FREQ MCP_16MHZ

// ---------------------------------------------------------------------------
// GVRET protocol constants
// ---------------------------------------------------------------------------
#define GVRET_MAGIC      0xF1
#define CMD_FRAME_OUT    0x00   // device → PC: CAN frame received
#define CMD_FRAME_IN     0x01   // PC → device: send CAN frame
#define CMD_TIME_SYNC    0x02   // timestamp sync (echo)
#define CMD_SETUP_BUS    0x06   // configure CAN speed
#define CMD_GET_INFO     0x08   // device capabilities
#define CMD_KEEPALIVE    0x09   // heartbeat (echo)

// ---------------------------------------------------------------------------
// Ring buffer for incoming serial data
// ---------------------------------------------------------------------------
#define RX_BUF_SIZE 512
static uint8_t rxBuf[RX_BUF_SIZE];
static uint16_t rxHead = 0, rxTail = 0;

static inline void rbPush(uint8_t b) {
    uint16_t next = (rxHead + 1) % RX_BUF_SIZE;
    if (next != rxTail) { rxBuf[rxHead] = b; rxHead = next; }
}
static inline uint16_t rbAvail() {
    return (rxHead - rxTail + RX_BUF_SIZE) % RX_BUF_SIZE;
}
static inline uint8_t rbPeek(uint16_t offset) {
    return rxBuf[(rxTail + offset) % RX_BUF_SIZE];
}
static inline uint8_t rbPop() {
    uint8_t b = rxBuf[rxTail];
    rxTail = (rxTail + 1) % RX_BUF_SIZE;
    return b;
}
static inline void rbConsume(uint16_t n) {
    rxTail = (rxTail + n) % RX_BUF_SIZE;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static MCP2515   mcp(CAN_CS);
static can_frame rxMsg, txMsg;
static uint32_t  canSpeed   = 250000;   // bps
static bool      listenOnly = false;

// ---------------------------------------------------------------------------
// CAN speed helpers
// ---------------------------------------------------------------------------
static CAN_SPEED bpsToCanSpeed(uint32_t bps) {
    if (bps >= 900000)  return CAN_1000KBPS;
    if (bps >= 600000)  return CAN_500KBPS;
    if (bps >= 350000)  return CAN_500KBPS;
    if (bps >= 225000)  return CAN_250KBPS;
    if (bps >= 110000)  return CAN_125KBPS;
    if (bps >= 75000)   return CAN_100KBPS;
    if (bps >= 35000)   return CAN_50KBPS;
    if (bps >= 15000)   return CAN_20KBPS;
    return CAN_10KBPS;
}

static void applyCanSpeed(uint32_t bps, bool lo) {
    canSpeed   = bps;
    listenOnly = lo;
    mcp.reset();
    mcp.setFilterMask(MCP2515::MASK0, false, 0x00000000);
    mcp.setFilterMask(MCP2515::MASK1, false, 0x00000000);
    mcp.setFilter(MCP2515::RXF0, false, 0x00000000);
    mcp.setFilter(MCP2515::RXF1, false, 0x00000000);
    mcp.setFilter(MCP2515::RXF2, false, 0x00000000);
    mcp.setFilter(MCP2515::RXF3, false, 0x00000000);
    mcp.setFilter(MCP2515::RXF4, false, 0x00000000);
    mcp.setFilter(MCP2515::RXF5, false, 0x00000000);
    mcp.setBitrate(bpsToCanSpeed(bps), CRYSTAL_FREQ);
    if (lo) mcp.setListenOnlyMode();
    else    mcp.setNormalMode();
}

// ---------------------------------------------------------------------------
// Send CAN frame to SavvyCAN in GVRET format
// ---------------------------------------------------------------------------
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
    buf[idx++] = (uint8_t)(ts >> 8);
    buf[idx++] = (uint8_t)(ts >> 16);
    buf[idx++] = (uint8_t)(ts >> 24);
    buf[idx++] = (uint8_t)(id);
    buf[idx++] = (uint8_t)(id >> 8);
    buf[idx++] = (uint8_t)(id >> 16);
    buf[idx++] = (uint8_t)(id >> 24);
    buf[idx++] = (uint8_t)(((busNum & 0x0F) << 4) | dlc);
    for (uint8_t i = 0; i < dlc && i < 8; i++) buf[idx++] = f.data[i];

    Serial.write(buf, idx);
}

// ---------------------------------------------------------------------------
// Send device info (response to CMD_GET_INFO)
// Format: F1 08 numBuses [speed(4LE) enabled canFD listenOnly] buildDate\0 swVer\0 hwVer\0
// ---------------------------------------------------------------------------
static void sendDeviceInfo() {
    uint8_t buf[96];
    uint8_t idx = 0;

    buf[idx++] = GVRET_MAGIC;
    buf[idx++] = CMD_GET_INFO;
    buf[idx++] = 1;   // numBuses = 1

    // Bus 0: speed(4LE) + enabled(1) + canFD(1) + listenOnly(1)
    buf[idx++] = (uint8_t)(canSpeed);
    buf[idx++] = (uint8_t)(canSpeed >> 8);
    buf[idx++] = (uint8_t)(canSpeed >> 16);
    buf[idx++] = (uint8_t)(canSpeed >> 24);
    buf[idx++] = 1;                          // enabled
    buf[idx++] = 0;                          // not CAN-FD
    buf[idx++] = listenOnly ? 1 : 0;

    // Build date (null terminated)
    const char *bd = __DATE__ " " __TIME__;
    uint8_t len = strlen(bd);
    memcpy(buf + idx, bd, len + 1);
    idx += len + 1;

    // SW version
    const char *sw = "0.2.0";
    len = strlen(sw);
    memcpy(buf + idx, sw, len + 1);
    idx += len + 1;

    // HW version
    const char *hw = "ESP32-MCP2515";
    len = strlen(hw);
    memcpy(buf + idx, hw, len + 1);
    idx += len + 1;

    Serial.write(buf, idx);
}

// ---------------------------------------------------------------------------
// Binary GVRET command parser
// ---------------------------------------------------------------------------
static void processGVRET() {
    while (true) {
        // Skip bytes until 0xF1
        while (rbAvail() > 0 && rbPeek(0) != GVRET_MAGIC) rbPop();
        if (rbAvail() < 2) return;

        uint8_t cmd = rbPeek(1);

        switch (cmd) {

        // F1 08 — get device info (no payload)
        case CMD_GET_INFO:
            rbConsume(2);
            sendDeviceInfo();
            break;

        // F1 09 — keepalive (echo)
        case CMD_KEEPALIVE: {
            rbConsume(2);
            uint8_t resp[2] = {GVRET_MAGIC, CMD_KEEPALIVE};
            Serial.write(resp, 2);
            break;
        }

        // F1 02 [ts:4LE] — time sync (echo timestamp back)
        case CMD_TIME_SYNC: {
            if (rbAvail() < 6) return;
            rbConsume(2);
            uint8_t ts[4];
            for (int i = 0; i < 4; i++) ts[i] = rbPop();
            uint8_t resp[6] = {GVRET_MAGIC, CMD_TIME_SYNC, ts[0], ts[1], ts[2], ts[3]};
            Serial.write(resp, 6);
            break;
        }

        // F1 06 [speed:4LE] [busNum:1] [flags:1] — setup CAN bus
        // flags: bit0=listenOnly, bit1=canFD
        case CMD_SETUP_BUS: {
            if (rbAvail() < 8) return;
            rbConsume(2);
            uint32_t spd = (uint32_t)rbPop()
                         | ((uint32_t)rbPop() << 8)
                         | ((uint32_t)rbPop() << 16)
                         | ((uint32_t)rbPop() << 24);
            uint8_t busNum = rbPop();
            uint8_t flags  = rbPop();
            bool lo = (flags & 0x01) != 0;

            if (spd >= 10000 && spd <= 1000000) applyCanSpeed(spd, lo);

            // Echo response
            uint8_t resp[8] = {
                GVRET_MAGIC, CMD_SETUP_BUS,
                (uint8_t)(canSpeed),       (uint8_t)(canSpeed >> 8),
                (uint8_t)(canSpeed >> 16), (uint8_t)(canSpeed >> 24),
                busNum, flags
            };
            Serial.write(resp, 8);
            break;
        }

        // F1 01 [id:4LE] [len_bus:1] [data:0-8] — send CAN frame to bus
        case CMD_FRAME_IN: {
            if (rbAvail() < 7) return;               // F1+01+id(4)+len_bus = 7 minimum
            uint8_t len_bus = rbPeek(6);
            uint8_t dlc     = len_bus & 0x0F;
            if (rbAvail() < (uint16_t)(7 + dlc)) return;

            rbConsume(2);
            uint32_t id = (uint32_t)rbPop()
                        | ((uint32_t)rbPop() << 8)
                        | ((uint32_t)rbPop() << 16)
                        | ((uint32_t)rbPop() << 24);
            rbPop();  // len_bus

            txMsg.can_id  = id & 0x1FFFFFFFUL;
            if (id & 0x80000000UL) txMsg.can_id |= CAN_EFF_FLAG;
            txMsg.can_dlc = dlc;
            for (uint8_t i = 0; i < dlc; i++) txMsg.data[i] = rbPop();

            if (!listenOnly) mcp.sendMessage(&txMsg);
            break;
        }

        default:
            rbPop();   // unknown command — discard magic byte and re-scan
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(200);

    SPI.begin();
    while (mcp.reset() != MCP2515::ERROR_OK) delay(500);

    mcp.setFilterMask(MCP2515::MASK0, false, 0x00000000);
    mcp.setFilterMask(MCP2515::MASK1, false, 0x00000000);
    mcp.setFilter(MCP2515::RXF0, false, 0x00000000);
    mcp.setFilter(MCP2515::RXF1, false, 0x00000000);
    mcp.setFilter(MCP2515::RXF2, false, 0x00000000);
    mcp.setFilter(MCP2515::RXF3, false, 0x00000000);
    mcp.setFilter(MCP2515::RXF4, false, 0x00000000);
    mcp.setFilter(MCP2515::RXF5, false, 0x00000000);
    mcp.setBitrate(CAN_250KBPS, CRYSTAL_FREQ);
    mcp.setNormalMode();
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
void loop() {
    // Incoming serial → ring buffer
    while (Serial.available()) rbPush((uint8_t)Serial.read());

    // Parse GVRET commands
    processGVRET();

    // CAN bus → SavvyCAN
    if (mcp.readMessage(&rxMsg) == MCP2515::ERROR_OK) {
        sendFrameToPC(rxMsg, 0);
    }
}
