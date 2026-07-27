// esp_experiment_2_2.ino — Eksperyment 2.2: Odpornosc bufora podczas fazy
// adaptacji (Buffer Overflow Threshold), wg "Pomiary dla CAN-Edge AI.md".
//
// Symuluje faze "Cold Start" (system czeka ~2.2s na odpowiedz z LLM) BEZ
// prawdziwego zapytania do LLM/WiFi - czas oczekiwania jest sterowany
// deterministycznie z hosta (ramka kontrolna START_BUSY), co pozwala
// precyzyjnie i powtarzalnie przemiatac czas oczekiwania.
//
// Model: w normalnym trybie (IDLE) system "przetwarza" kazda ramke
// natychmiast (bufor pozostaje pusty). W trybie BUSY (symulowany Cold
// Start) system NIE przetwarza ramek z kolejki aplikacyjnej (bufOccupied
// rosnie), ale WCIAZ odczytuje je przez SPI z MCP2515 (to tania, szybka
// operacja - realistyczne, ze nawet zajety system nadaza z odbiorem SPI).
// Jesli bufor programowy (BUFFER_SIZE) sie zapelni, kolejne ramki sa
// zliczane jako utracone (droppedCount++) - to jest badane zjawisko.
// Po uplywie zadanego czasu BUSY, bufor jest natychmiast "przetworzony"
// (wyzerowany), system wraca do IDLE.
//
// Protokol sterujacy (CAN ID CONTROL_CAN_ID=0x7FE):
//   data[0]=0x01                                -> RESET liczników
//   data[0]=0x02, data[1..4]=czas_ms (uint32 BE) -> START_BUSY (rozpocznij
//                                                    symulowany Cold Start)
//   data[0]=0x03                                -> REPORT (wypisz wynik)
//
// ZMIEN BUFFER_SIZE PRZED KAZDYM ZESTAWEM POMIAROW (4/8/16/32/64).

#include <SPI.h>
#include <mcp2515.h>

#define CAN_CS         5
#define CAN_OSC_MHZ    16
#define CAN_BITRATE    CAN_250KBPS

// ─── ZMIEN TO PRZED KAZDYM ZESTAWEM POMIAROW ────────────────────────────
#define BUFFER_SIZE    64
// Dostepne: 4, 8, 16, 32, 64
// ─────────────────────────────────────────────────────────────────────────

#define MCP_INT_PIN    4

#define TEST_CAN_ID    0x200
#define CONTROL_CAN_ID 0x7FE
#define CTRL_RESET       0x01
#define CTRL_START_BUSY  0x02
#define CTRL_REPORT      0x03

static MCP2515 mcp2515(CAN_CS);
static can_frame rxMsg;

volatile bool g_frameReady = false;
static uint32_t g_totalArrived = 0;
static uint32_t g_droppedCount = 0;
static uint32_t g_bufOccupied = 0;
static bool     g_busy = false;
static unsigned long g_busyEndMs = 0;

void IRAM_ATTR onMcpInterrupt() {
    g_frameReady = true;
}

void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(MCP_INT_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(MCP_INT_PIN), onMcpInterrupt, FALLING);

    SPI.begin();
    while (mcp2515.reset() != MCP2515::ERROR_OK) delay(500);
    mcp2515.setFilterMask(MCP2515::MASK0, true, 0x00000000);
    mcp2515.setFilterMask(MCP2515::MASK1, true, 0x00000000);
    mcp2515.setBitrate(CAN_BITRATE, CAN_OSC_MHZ >= 16 ? MCP_16MHZ : MCP_8MHZ);
    mcp2515.setNormalMode();

    Serial.print("[SETUP] Eksperyment 2.2 gotowy, BUFFER_SIZE=");
    Serial.println(BUFFER_SIZE);
}

static void checkBusyExpiry() {
    if (g_busy && (long)(millis() - g_busyEndMs) >= 0) {
        g_busy = false;
        g_bufOccupied = 0; // natychmiastowe "przetworzenie" bufora po powrocie do IDLE
    }
}

void loop() {
    checkBusyExpiry();

    if (!g_frameReady) return;
    g_frameReady = false;

    while (mcp2515.readMessage(&rxMsg) == MCP2515::ERROR_OK) {
        checkBusyExpiry();

        bool extended = (rxMsg.can_id & CAN_EFF_FLAG) != 0;
        uint32_t id = rxMsg.can_id & (extended ? CAN_EFF_MASK : CAN_SFF_MASK);

        if (id == TEST_CAN_ID) {
            g_totalArrived++;
            if (g_busy) {
                if (g_bufOccupied < BUFFER_SIZE) {
                    g_bufOccupied++;
                } else {
                    g_droppedCount++;
                }
            }
            // w IDLE: ramka "przetworzona natychmiast", nic do zrobienia
        } else if (id == CONTROL_CAN_ID && rxMsg.can_dlc >= 1) {
            if (rxMsg.data[0] == CTRL_RESET) {
                g_totalArrived = 0;
                g_droppedCount = 0;
                g_bufOccupied = 0;
                g_busy = false;
            } else if (rxMsg.data[0] == CTRL_START_BUSY && rxMsg.can_dlc >= 5) {
                uint32_t durationMs = (uint32_t(rxMsg.data[1]) << 24)
                                     | (uint32_t(rxMsg.data[2]) << 16)
                                     | (uint32_t(rxMsg.data[3]) << 8)
                                     | (uint32_t(rxMsg.data[4]));
                g_totalArrived = 0;
                g_droppedCount = 0;
                g_bufOccupied = 0;
                g_busy = true;
                g_busyEndMs = millis() + durationMs;
            } else if (rxMsg.data[0] == CTRL_REPORT) {
                Serial.print("RESULT,");
                Serial.print(g_totalArrived);
                Serial.print(',');
                Serial.print(g_droppedCount);
                Serial.print(',');
                Serial.println(BUFFER_SIZE);
            }
        }
    }
}
