// ESP32 SLCAN bridge — Lawicel SLCAN over USB Serial
// MCP2515 @ 16 MHz crystal, domyślnie 250 kbps
//
// Komendy SLCAN:
//   Sn          ustaw prędkość (5=250k, 6=500k, 4=125k, 8=1M)
//   O           otwórz kanał (normal)
//   L           otwórz kanał (listen-only)
//   C           zamknij kanał
//   tIIILDD..   wyślij ramkę standardową (11-bit ID)
//   TIIIIIIIILDD.. wyślij ramkę rozszerzoną (29-bit ID)
//   V / N / F   wersja / numer / flagi

#include <SPI.h>
#include <mcp2515.h>

// ---- Konfiguracja sprzętu ----
#define CAN_CS_PIN    5
#define CRYSTAL_FREQ  MCP_16MHZ
#define DEFAULT_SPEED CAN_250KBPS
#define UART_BAUD     115200

// ---- Stan globalny ----
static MCP2515   mcp(CAN_CS_PIN);
static can_frame rxFrame, txFrame;
static bool      channelOpen  = false;
static bool      listenOnly   = false;
static CAN_SPEED currentSpeed = DEFAULT_SPEED;

// ---- Bufor komend ----
#define CMD_MAX 32
static char    cmd[CMD_MAX];
static uint8_t cmdLen = 0;

// ---- Miniparser hex ----
static int8_t hexNib(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool readHex(const char* s, uint8_t n, uint32_t& out) {
    out = 0;
    for (uint8_t i = 0; i < n; i++) {
        int8_t v = hexNib(s[i]);
        if (v < 0) return false;
        out = (out << 4) | (uint8_t)v;
    }
    return true;
}

// ---- Inicjalizacja MCP2515 ----
static bool mcpInit(CAN_SPEED speed, bool loMode) {
    if (mcp.reset() != MCP2515::ERROR_OK) return false;

    // Maski = 0 → akceptuj każdą ramkę (standard i extended)
    mcp.setFilterMask(MCP2515::MASK0, false, 0x00000000);
    mcp.setFilterMask(MCP2515::MASK1, false, 0x00000000);
    mcp.setFilter(MCP2515::RXF0, false, 0x00000000);
    mcp.setFilter(MCP2515::RXF1, false, 0x00000000);
    mcp.setFilter(MCP2515::RXF2, false, 0x00000000);
    mcp.setFilter(MCP2515::RXF3, false, 0x00000000);
    mcp.setFilter(MCP2515::RXF4, false, 0x00000000);
    mcp.setFilter(MCP2515::RXF5, false, 0x00000000);

    mcp.setBitrate(speed, CRYSTAL_FREQ);

    MCP2515::ERROR err;
    if (loMode) err = mcp.setListenOnlyMode();
    else        err = mcp.setNormalMode();

    return (err == MCP2515::ERROR_OK);
}

// ---- Wyślij ramkę CAN w formacie SLCAN ----
static void slcanTx(const can_frame& f) {
    char buf[30];
    bool     ext = f.can_id & CAN_EFF_FLAG;
    uint32_t id  = f.can_id & (ext ? CAN_EFF_MASK : CAN_SFF_MASK);
    uint8_t  dlc = f.can_dlc & 0x0F;

    if (ext) snprintf(buf, sizeof(buf), "T%08lX%u", (unsigned long)id, dlc);
    else     snprintf(buf, sizeof(buf), "t%03lX%u",  (unsigned long)id, dlc);
    Serial.print(buf);

    for (uint8_t i = 0; i < dlc; i++) {
        snprintf(buf, 3, "%02X", f.data[i]);
        Serial.print(buf);
    }
    Serial.print('\r');
}

// ---- Parser komend SLCAN ----
static void handleCmd() {
    if (cmdLen == 0) return;
    cmd[cmdLen] = '\0';

    char c = cmd[0];

    if (c == 'S' && cmdLen == 2) {
        const CAN_SPEED tbl[] = {
            CAN_10KBPS, CAN_20KBPS, CAN_50KBPS, CAN_100KBPS,
            CAN_125KBPS, CAN_250KBPS, CAN_500KBPS, CAN_500KBPS, CAN_1000KBPS
        };
        uint8_t idx = cmd[1] - '0';
        if (idx > 8) { Serial.print('\a'); return; }
        currentSpeed = tbl[idx];
        Serial.print('\r');
        return;
    }

    if (c == 'O' && cmdLen == 1) {
        listenOnly  = false;
        channelOpen = mcpInit(currentSpeed, false);
        Serial.print('\r');
        return;
    }

    if (c == 'L' && cmdLen == 1) {
        listenOnly  = true;
        channelOpen = mcpInit(currentSpeed, true);
        Serial.print('\r');
        return;
    }

    if (c == 'C' && cmdLen == 1) {
        channelOpen = false;
        mcp.reset();
        Serial.print('\r');
        return;
    }

    if (c == 'V') { Serial.print("V1010\r"); return; }
    if (c == 'N') { Serial.print("NA123\r"); return; }
    if (c == 'F') { Serial.print("F00\r");   return; }
    if (c == 'Z') { Serial.print('\r');       return; }

    if (c == 't' && cmdLen >= 5) {
        uint32_t id;
        uint8_t  dlc = cmd[4] - '0';
        if (!readHex(cmd + 1, 3, id) || dlc > 8 || cmdLen < (uint8_t)(5 + dlc * 2)) {
            Serial.print('\a'); return;
        }
        txFrame.can_id  = id & CAN_SFF_MASK;
        txFrame.can_dlc = dlc;
        for (uint8_t i = 0; i < dlc; i++) {
            uint32_t b;
            readHex(cmd + 5 + i * 2, 2, b);
            txFrame.data[i] = (uint8_t)b;
        }
        if (!listenOnly && mcp.sendMessage(&txFrame) == MCP2515::ERROR_OK)
            Serial.print('\r');
        else
            Serial.print('\a');
        return;
    }

    if (c == 'T' && cmdLen >= 10) {
        uint32_t id;
        uint8_t  dlc = cmd[9] - '0';
        if (!readHex(cmd + 1, 8, id) || dlc > 8 || cmdLen < (uint8_t)(10 + dlc * 2)) {
            Serial.print('\a'); return;
        }
        txFrame.can_id  = (id & CAN_EFF_MASK) | CAN_EFF_FLAG;
        txFrame.can_dlc = dlc;
        for (uint8_t i = 0; i < dlc; i++) {
            uint32_t b;
            readHex(cmd + 10 + i * 2, 2, b);
            txFrame.data[i] = (uint8_t)b;
        }
        if (!listenOnly && mcp.sendMessage(&txFrame) == MCP2515::ERROR_OK)
            Serial.print('\r');
        else
            Serial.print('\a');
        return;
    }

    Serial.print('\a');
}

// ---- Setup ----
void setup() {
    Serial.begin(UART_BAUD);
    delay(300);
    SPI.begin();

    // Czekaj na MCP2515
    while (mcp.reset() != MCP2515::ERROR_OK) delay(200);

    // Skonfiguruj domyślnie (kanał zamknięty)
    mcpInit(DEFAULT_SPEED, false);
    channelOpen = false;
}

// ---- Loop ----
void loop() {
    // CAN → Serial
    if (channelOpen && mcp.readMessage(&rxFrame) == MCP2515::ERROR_OK) {
        slcanTx(rxFrame);
    }

    // Serial → parser komend
    while (Serial.available()) {
        char ch = (char)Serial.read();
        if (ch == '\r' || ch == '\n') {
            handleCmd();
            cmdLen = 0;
        } else if (cmdLen < CMD_MAX - 1) {
            cmd[cmdLen++] = ch;
        }
    }
}
