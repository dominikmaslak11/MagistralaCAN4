// esp_experiment_2_1.ino — Eksperyment 2.1: Maksymalna bezstratna
// przepustowosc (CAN Frame Throughput), wg "Pomiary dla CAN-Edge AI.md".
//
// Rola firmware: liczyc ramki testowe (TEST_CAN_ID) odebrane bezblednie
// przez ESP32+MCP2515 w danym oknie pomiarowym, i raportowac wynik po
// otrzymaniu ramki sterujacej STOP. Generator ruchu (PEAK PCAN-USB, PC)
// steruje oknem przez ramki sterujace CONTROL_CAN_ID:
//   data[0] == 0x01 -> RESET/START (wyzeruj licznik, zacznij nowe okno)
//   data[0] == 0x02 -> STOP/REPORT (wypisz wynik na Serial)
//
// Architektura minimalizujaca strate ramek: przerwanie sprzetowe (INT,
// FALLING) tylko ustawia flage, loop() drenuje bufor MCP2515 najszybciej
// jak sie da (bez zadnego opoznienia/logiki poza inkrementacja licznika),
// analogicznie do esp_can_loopback_test.ino.
//
// Dodatkowo: monitorowanie sprzetowej flagi przepelnienia bufora RX
// MCP2515 (EFLG.RX0OVR/RX1OVR) jako niezalezne, sprzetowe potwierdzenie
// utraty ramek (obok liczenia N_rcvd).

#include <SPI.h>
#include <mcp2515.h>

#define CAN_CS         5
#define CAN_OSC_MHZ    16                // kwarc na module MCP2515 (8 lub 16)

// ─── ZMIEN TO PRZED KAZDYM ZESTAWEM POMIAROW (250k / 500k) ──────────────
#define CAN_BITRATE    CAN_500KBPS
// Dostepne: CAN_250KBPS, CAN_500KBPS — musi byc zgodne z bitrate can0 na PC
// ─────────────────────────────────────────────────────────────────────────

#define MCP_INT_PIN    4                 // MCP2515 INT -> ESP32 GPIO4

#define TEST_CAN_ID    0x200             // ramki testowe generatora ruchu
#define CONTROL_CAN_ID 0x7FE             // ramki sterujace (RESET/STOP)
#define CTRL_RESET     0x01
#define CTRL_STOP      0x02

static MCP2515 mcp2515(CAN_CS);
static can_frame rxMsg;

volatile bool g_frameReady = false;
static uint32_t g_countRcvd = 0;
static uint32_t g_overflowEvents = 0;

void IRAM_ATTR onMcpInterrupt() {
    g_frameReady = true;
}

void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(MCP_INT_PIN, INPUT_PULLUP); // MCP2515 INT jest open-drain, aktywny niski
    attachInterrupt(digitalPinToInterrupt(MCP_INT_PIN), onMcpInterrupt, FALLING);

    SPI.begin();
    while (mcp2515.reset() != MCP2515::ERROR_OK) delay(500);
    mcp2515.setFilterMask(MCP2515::MASK0, true, 0x00000000);
    mcp2515.setFilterMask(MCP2515::MASK1, true, 0x00000000);
    mcp2515.setBitrate(CAN_BITRATE, CAN_OSC_MHZ >= 16 ? MCP_16MHZ : MCP_8MHZ);
    mcp2515.setNormalMode();

    Serial.println("[SETUP] Eksperyment 2.1 (throughput) gotowy");
}

void loop() {
    // Sprawdzaj flage przepelnienia bufora RX MCP2515 (sprzetowe
    // potwierdzenie utraty ramek, niezalezne od licznika programowego)
    uint8_t eflg = mcp2515.getErrorFlags();
    if (eflg & (MCP2515::EFLG_RX0OVR | MCP2515::EFLG_RX1OVR)) {
        g_overflowEvents++;
        mcp2515.clearRXnOVRFlags();
    }

    if (!g_frameReady) return;
    g_frameReady = false;

    while (mcp2515.readMessage(&rxMsg) == MCP2515::ERROR_OK) {
        bool extended = (rxMsg.can_id & CAN_EFF_FLAG) != 0;
        uint32_t id = rxMsg.can_id & (extended ? CAN_EFF_MASK : CAN_SFF_MASK);

        if (id == TEST_CAN_ID) {
            g_countRcvd++;
        } else if (id == CONTROL_CAN_ID && rxMsg.can_dlc >= 1) {
            if (rxMsg.data[0] == CTRL_RESET) {
                g_countRcvd = 0;
                g_overflowEvents = 0;
            } else if (rxMsg.data[0] == CTRL_STOP) {
                Serial.print("RESULT,");
                Serial.print(g_countRcvd);
                Serial.print(',');
                Serial.println(g_overflowEvents);
            }
        }
    }
}
