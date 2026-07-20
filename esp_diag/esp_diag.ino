// ESP32 MCP2515 diagnostic — wyniki przez Serial Monitor (115200)
#include <SPI.h>
#include <mcp2515.h>

#define CAN_CS_PIN   5
#define CRYSTAL_FREQ MCP_16MHZ

MCP2515 mcp(CAN_CS_PIN);

void setup() {
    Serial.begin(115200);
    delay(500);
    SPI.begin();

    Serial.println("=== MCP2515 DIAGNOSTIC ===");
    Serial.printf("SPI pins: MOSI=%d MISO=%d SCK=%d CS=%d\n", MOSI, MISO, SCK, CAN_CS_PIN);

    MCP2515::ERROR err = mcp.reset();
    if (err != MCP2515::ERROR_OK) {
        Serial.printf("BLAD: mcp.reset() = %d  (brak odpowiedzi SPI / zle okablowanie)\n", err);
        return;
    }
    Serial.println("OK: mcp.reset()");

    err = mcp.setBitrate(CAN_250KBPS, CRYSTAL_FREQ);
    Serial.printf("%s: setBitrate(250k, 16MHz) = %d\n", err == MCP2515::ERROR_OK ? "OK" : "BLAD", err);

    err = mcp.setFilterMask(MCP2515::MASK0, false, 0x00000000);
    err = mcp.setFilterMask(MCP2515::MASK1, false, 0x00000000);
    err = mcp.setFilter(MCP2515::RXF0, false, 0x00000000);
    err = mcp.setFilter(MCP2515::RXF1, false, 0x00000000);
    err = mcp.setFilter(MCP2515::RXF2, false, 0x00000000);
    err = mcp.setFilter(MCP2515::RXF3, false, 0x00000000);
    err = mcp.setFilter(MCP2515::RXF4, false, 0x00000000);
    err = mcp.setFilter(MCP2515::RXF5, false, 0x00000000);

    err = mcp.setNormalMode();
    Serial.printf("%s: setNormalMode() = %d\n", err == MCP2515::ERROR_OK ? "OK" : "BLAD", err);

    Serial.println("Nasluchuję ramek CAN... (wyslij ramke przez PCAN)");
}

void loop() {
    static uint32_t lastReport = 0;
    static uint32_t count = 0;

    can_frame f;
    if (mcp.readMessage(&f) == MCP2515::ERROR_OK) {
        count++;
        bool ext = f.can_id & CAN_EFF_FLAG;
        uint32_t id = f.can_id & (ext ? CAN_EFF_MASK : CAN_SFF_MASK);
        Serial.printf("RX [%c] ID=%03lX DLC=%d data=", ext ? 'E' : 'S', (unsigned long)id, f.can_dlc);
        for (int i = 0; i < f.can_dlc; i++) Serial.printf("%02X ", f.data[i]);
        Serial.println();
    }

    if (millis() - lastReport > 3000) {
        lastReport = millis();
        uint8_t ereg = mcp.getErrorFlags();
        Serial.printf("[%lus] ramek=%lu  EFLG=0x%02X%s\n",
            millis() / 1000, count, ereg,
            ereg ? "  << blad CAN (brak terminacji? zly baudrate?)" : "");
    }
}
