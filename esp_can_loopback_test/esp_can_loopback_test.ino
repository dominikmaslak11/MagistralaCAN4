// esp_can_loopback_test.ino — Prosty test dwukierunkowej komunikacji CAN
// miedzy PCAN-USB (Linux, SocketCAN can0) a ESP32+MCP2515.
//
// Cel: potwierdzic ze okablowanie (CANH/CANL, terminacja, wspolna masa,
// kwarc MCP2515) jest poprawne w OBIE STRONY, zanim uruchomimy wlasciwy
// eksperyment. To NIE jest firmware Eksperymentu 1.1/1.2 - to czysto
// diagnostyczny test.
//
// Co robi:
//   1. RX: kazda odebrana ramka CAN -> wypisana na Serial (ID, DLC, dane).
//   2. TX-echo: natychmiast po odebraniu, odsyla ramke zwrotna o ID
//      (odebrane_ID + 1) z tymi samymi danymi — do zaobserwowania w
//      `candump can0` na PC (potwierdza TX z ESP32 dziala).
//   3. TX-heartbeat: co 2 sekundy wysyla ramke ID=0x7AA z rosnacym licznikiem
//      w data[0] — nawet bez zadnej ramki od PC mozna zobaczyc w candump,
//      ze ESP32 potrafi nadawac na magistrali.
//
// Test z Linuxa (w dwoch terminalach):
//   Terminal 1: candump can0                       (patrz na 0x7AA co 2s
//                                                    i echo po wyslaniu)
//   Terminal 2: cansend can0 123#DEADBEEF           (powinno przyjsc echo
//                                                    z ID 0x124 na candump)
//   Serial Monitor (115200 baud): podglad odebranych ramek po stronie ESP32

#include <SPI.h>
#include <mcp2515.h>

#define CAN_CS        5
#define CAN_OSC_MHZ   8    // kwarc na module MCP2515 (8 lub 16)
#define HEARTBEAT_ID  0x7AA
#define HEARTBEAT_MS  2000

static MCP2515 mcp2515(CAN_CS);
static can_frame rxMsg, txMsg;
static unsigned long lastHeartbeat = 0;
static uint8_t heartbeatCounter = 0;

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("[SETUP] Test loopback CAN — start");

    SPI.begin();
    MCP2515::ERROR err;
    int tries = 0;
    while ((err = mcp2515.reset()) != MCP2515::ERROR_OK) {
        Serial.printf("[SETUP] mcp2515.reset() blad=%d, ponawiam...\n", (int)err);
        delay(500);
        if (++tries > 10) {
            Serial.println("[SETUP] BLAD: MCP2515 nie odpowiada po 10 probach — sprawdz okablowanie SPI (CS/SCK/MOSI/MISO) i zasilanie modulu.");
        }
    }
    mcp2515.setFilterMask(MCP2515::MASK0, true, 0x00000000);
    mcp2515.setFilterMask(MCP2515::MASK1, true, 0x00000000);
    mcp2515.setBitrate(CAN_250KBPS, CAN_OSC_MHZ >= 16 ? MCP_16MHZ : MCP_8MHZ);
    mcp2515.setNormalMode();

    Serial.println("[SETUP] MCP2515 gotowy. Oczekiwanie na ramki CAN (dowolne ID)...");
    Serial.println("[SETUP] Na PC: 'candump can0' w jednym terminalu, 'cansend can0 123#DEADBEEF' w drugim.");
}

void loop() {
    // RX + echo
    if (mcp2515.readMessage(&rxMsg) == MCP2515::ERROR_OK) {
        bool extended = (rxMsg.can_id & CAN_EFF_FLAG) != 0;
        uint32_t id = rxMsg.can_id & (extended ? CAN_EFF_MASK : CAN_SFF_MASK);

        Serial.printf("[RX] ID=0x%03lX DLC=%d data=[", (unsigned long)id, rxMsg.can_dlc);
        for (int i = 0; i < rxMsg.can_dlc; i++)
            Serial.printf("%02X ", rxMsg.data[i]);
        Serial.println("]");

        // echo: ID+1, te same dane
        txMsg.can_id = extended ? ((id + 1) | CAN_EFF_FLAG) : (id + 1);
        txMsg.can_dlc = rxMsg.can_dlc;
        memcpy(txMsg.data, rxMsg.data, rxMsg.can_dlc);
        MCP2515::ERROR sendErr = mcp2515.sendMessage(&txMsg);
        Serial.printf("[TX-echo] ID=0x%03lX wyslane, wynik=%d (0=OK)\n",
                      (unsigned long)(id + 1), (int)sendErr);
    }

    // Heartbeat co HEARTBEAT_MS
    unsigned long now = millis();
    if (now - lastHeartbeat >= HEARTBEAT_MS) {
        lastHeartbeat = now;
        txMsg.can_id = HEARTBEAT_ID;
        txMsg.can_dlc = 1;
        txMsg.data[0] = heartbeatCounter++;
        MCP2515::ERROR sendErr = mcp2515.sendMessage(&txMsg);
        Serial.printf("[TX-heartbeat] ID=0x%03X counter=%d wynik=%d (0=OK)\n",
                      HEARTBEAT_ID, txMsg.data[0], (int)sendErr);
    }
}
