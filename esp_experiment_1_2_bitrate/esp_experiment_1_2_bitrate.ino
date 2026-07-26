// esp_experiment_1_2_bitrate.ino — Wariant Eksperymentu 1.2 z parametryzowanym
// bitrate CAN, do sprawdzenia hipotezy: skoro t_resp liczymy OD przerwania
// INT (czyli JUZ PO odebraniu calej ramki), to bitrate magistrali CAN nie
// powinien wplywac na wynik - analogicznie do juz potwierdzonej niezaleznosci
// od zegara MCP2515 (8MHz vs 16MHz, ten sam bitrate 250kbps).
//
// Logika identyczna jak baza (esp_experiment_1_2.ino): ISR tylko znacznik
// czasu, loop() odczytuje ramke przez SPI, sprawdza ID, reaguje.
//
// Zmien CAN_BITRATE ponizej przed kazda kompilacja+wgraniem, i podnies can0
// na PC na PASUJACY bitrate (ip link set can0 type can bitrate <X>)
// PRZED uruchomieniem run_experiment_1_2.py.

#include <SPI.h>
#include <mcp2515.h>

#define CAN_CS         5
#define CAN_OSC_MHZ    16                // kwarc na module MCP2515 (8 lub 16)

// ─── ZMIEN TO PRZED KAZDYM PRZEBIEGIEM ──────────────────────────────────
#define CAN_BITRATE    CAN_1000KBPS
// aktywne: 1000KBPS
// Dostepne (musza byc zgodne z bitrate ustawionym na can0 po stronie PC):
//   CAN_125KBPS, CAN_250KBPS, CAN_500KBPS, CAN_1000KBPS
// ─────────────────────────────────────────────────────────────────────────

#define MCP_INT_PIN    4                 // MCP2515 INT -> ESP32 GPIO4
#define REACTION_PIN   13                // ESP32 GPIO13 -> kanal 2 analizatora
#define REACTION_PULSE_US 50             // dlugosc impulsu reakcji (widocznosc na analizatorze)

#define TRIGGER_CAN_ID 0x123

static MCP2515 mcp2515(CAN_CS);
static can_frame rxMsg;

volatile bool g_frameReady = false;
volatile uint64_t g_isrTimestampUs = 0;
static uint32_t g_trialCount = 0;

void IRAM_ATTR onMcpInterrupt() {
    g_isrTimestampUs = (uint64_t)esp_timer_get_time();
    g_frameReady = true;
}

void setup() {
    Serial.begin(115200);
    delay(200);

    pinMode(REACTION_PIN, OUTPUT);
    digitalWrite(REACTION_PIN, LOW);

    pinMode(MCP_INT_PIN, INPUT_PULLUP); // MCP2515 INT jest open-drain, aktywny niski
    attachInterrupt(digitalPinToInterrupt(MCP_INT_PIN), onMcpInterrupt, FALLING);

    SPI.begin();
    while (mcp2515.reset() != MCP2515::ERROR_OK) delay(500);
    mcp2515.setFilterMask(MCP2515::MASK0, true, 0x00000000);
    mcp2515.setFilterMask(MCP2515::MASK1, true, 0x00000000);
    mcp2515.setBitrate(CAN_BITRATE, CAN_OSC_MHZ >= 16 ? MCP_16MHZ : MCP_8MHZ);
    mcp2515.setNormalMode();

    Serial.println("[SETUP] Wariant zmienny-bitrate gotowy - oczekiwanie na CAN ID 0x123");
}

void loop() {
    if (!g_frameReady) return;
    g_frameReady = false;
    uint64_t isrTs = g_isrTimestampUs;

    while (mcp2515.readMessage(&rxMsg) == MCP2515::ERROR_OK) {
        bool extended = (rxMsg.can_id & CAN_EFF_FLAG) != 0;
        uint32_t id = rxMsg.can_id & (extended ? CAN_EFF_MASK : CAN_SFF_MASK);

        if (id == TRIGGER_CAN_ID) {
            uint64_t reactTs = (uint64_t)esp_timer_get_time();
            digitalWrite(REACTION_PIN, HIGH);

            uint64_t tRespUs = reactTs - isrTs;
            g_trialCount++;
            Serial.print(g_trialCount);
            Serial.print(',');
            Serial.println((unsigned long)tRespUs);

            delayMicroseconds(REACTION_PULSE_US);
            digitalWrite(REACTION_PIN, LOW);
        }
    }
}
