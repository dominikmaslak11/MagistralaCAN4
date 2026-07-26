// esp_experiment_1_2.ino — Eksperyment 1.2: Hot Execution Latency
// Sprzet: ESP32 + MCP2515 (SPI, CS=GPIO5) — ten sam modul co esp_experiment_1_1.
//
// Rola tego firmware ("Pomiary dla CAN-Edge AI.md", 1.2):
//   Zmierzenie czasu reakcji SYSTEMU JUZ NAUCZONEGO (reguła jest znana z gory,
//   nie trzeba pytac LLM) na znana ramke CAN.
//
// POMIAR WEWNETRZNY (bez zewnetrznego analizatora stanow logicznych —
// sprzet ATK-Logic okazal sie trwale niesprawny/zawieszony po wczesniejszych
// nieudanych transferach, patrz historia sesji). ESP32 mierzy sam siebie:
// t_resp = czas(reakcja GPIO) - czas(przerwanie od MCP2515 INT), z
// rozdzielczoscia mikrosekundowa (esp_timer_get_time()), raportowany przez
// USB Serial po kazdej probie.
//
// Architektura minimalizujaca jitter pomiaru:
//   - MCP2515 INT (aktywny stan niski przy odebraniu ramki) -> GPIO4, obsluga
//     przez attachInterrupt() (przerwanie sprzetowe, nie polling w loop()).
//     Znacznik czasu brany NA SAMYM POCZATKU ISR.
//   - ISR jest CELOWO minimalna (tylko flaga+timestamp volatile) — odczyt SPI
//     ramki z MCP2515 NIE jest bezpieczny/zalecany bezposrednio w ISR na
//     ESP32, wiec faktyczny odczyt+porownanie ID+reakcja dzieje sie w loop(),
//     ktora dzieki brakowi WiFi/innych zadan w tle kreci sie bardzo szybko.
//     Znacznik czasu reakcji brany TUZ PRZED digitalWrite(REACTION_PIN,HIGH).
//   - Brak WiFi = brak radiowego zrodla jittera w torze pomiarowym.
//   - Serial.println() (raportowanie wyniku) wykonywane PO obu znacznikach
//     czasu i po samym przelaczeniu GPIO — nie wplywa na mierzony odcinek.
//
// Wymagane biblioteki: "MCP2515" by autowp (ten sam co esp_experiment_1_1).

#include <SPI.h>
#include <mcp2515.h>

// ──────────────────────────────────────────────────────────────
// KONFIGURACJA
// ──────────────────────────────────────────────────────────────
#define CAN_CS         5
#define CAN_SPEED_BPS  250000UL
#define CAN_OSC_MHZ    8                 // kwarc na module MCP2515 (8 lub 16)

#define MCP_INT_PIN    4                 // MCP2515 INT -> ESP32 GPIO4
#define REACTION_PIN   13                // ESP32 GPIO13 -> kanal 1 analizatora
#define REACTION_PULSE_US 50             // dlugosc impulsu reakcji (widocznosc na analizatorze)

// CAN ID "znanej" ramki wyzwalajacej reakcje (odpowiednik reguly juz
// wdrozonej po Eksperymencie 1.1 — tutaj zaszyty na sztywno, bo 1.2 testuje
// wylacznie fazę Hot Execution, nie proces uczenia).
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
    mcp2515.setBitrate(CAN_250KBPS, CAN_OSC_MHZ >= 16 ? MCP_16MHZ : MCP_8MHZ);
    mcp2515.setNormalMode();

    Serial.println("[SETUP] Eksperyment 1.2 gotowy — oczekiwanie na CAN ID 0x123");
}

void loop() {
    if (!g_frameReady) return;
    g_frameReady = false;
    uint64_t isrTs = g_isrTimestampUs;

    // Odsacz wszystkie ramki oczekujace w buforach RX MCP2515 (INT zostaje
    // niski dopoki cos zostalo nieodczytane).
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
