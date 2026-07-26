// esp_experiment_1_2_hwfilter.ino — Wariant Eksperymentu 1.2 z filtrem
// sprzętowym MCP2515 i natychmiastową reakcją w ISR.
//
// Roznica wzgledem esp_experiment_1_2.ino (wersja bazowa):
//   Bazowa: ISR tylko znacznik czasu -> loop() odczytuje ramke przez SPI ->
//           sprawdza ID -> reaguje. t_resp ~110us zdominowany przez SPI+firmware.
//   Ta wersja: filtr sprzetowy MCP2515 (RXF/RXM) skonfigurowany tak, ze TYLKO
//           TRIGGER_CAN_ID moze wywolac przerwanie - kazde przerwanie JUZ
//           GWARANTUJE wlasciwa ramke, wiec reagujemy NATYCHMIAST w samym ISR,
//           PRZED odczytem ramki przez SPI (ktory i tak musi nastapic pozniej
//           w loop(), zeby zwolnic bufor RX i linie INT, ale juz PO reakcji).
//
// Cel: zmierzyc teoretyczna "podloge" opoznienia przerwanie->reakcja GPIO,
// odizolowana od kosztu SPI/odczytu ramki/sprawdzania ID w softcie - do
// porownania z wersja bazowa (pokazuje ile z ~110us to koszt SPI+firmware,
// a ile to czysty narzut ISR/GPIO ESP32).
//
// UWAGA: to zmienia SENS pomiaru wzgledem wersji bazowej - to juz nie jest
// "czas systemu decyzyjnego ktory musi zajrzec w tresc ramki", tylko czysty
// prog sprzetowy przerwania. Oba pomiary sa wartosciowe, ale mierza co innego.

#include <SPI.h>
#include <mcp2515.h>

#define CAN_CS         5
#define CAN_OSC_MHZ    16                // kwarc na module MCP2515 (8 lub 16)

#define MCP_INT_PIN    4                 // MCP2515 INT -> ESP32 GPIO4
#define REACTION_PIN   13                // ESP32 GPIO13 -> kanal 2 analizatora
#define REACTION_PULSE_US 50             // dlugosc impulsu reakcji (widocznosc na analizatorze)

#define TRIGGER_CAN_ID 0x123

static MCP2515 mcp2515(CAN_CS);
static can_frame rxMsg;

volatile bool g_frameReady = false;
volatile uint64_t g_tRespUs = 0;
static uint32_t g_trialCount = 0;

void IRAM_ATTR onMcpInterrupt() {
    uint64_t isrTs = (uint64_t)esp_timer_get_time();
    // Filtr sprzetowy MCP2515 (ustawiony w setup()) gwarantuje, ze KAZDE
    // przerwanie na tym pinie to TRIGGER_CAN_ID - reagujemy natychmiast,
    // bez czekania na odczyt tresci ramki przez SPI.
    digitalWrite(REACTION_PIN, HIGH);
    uint64_t reactTs = (uint64_t)esp_timer_get_time();
    g_tRespUs = reactTs - isrTs;
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

    // Filtr sprzetowy: TYLKO TRIGGER_CAN_ID trafia do RXB0/RXB1.
    // Obie maski "pelne dopasowanie" (0x7FF, standard ID 11-bit), wszystkie
    // filtry obu buforow ustawione na TRIGGER_CAN_ID - inaczej RXB1 (ktory ma
    // wlasna maske MASK1 + 4 filtry RXF2..RXF5) przyjalby WSZYSTKO i wywolal
    // przerwanie tez dla innych ID, psujac zalozenie "kazde INT = nasz ID".
    mcp2515.setFilterMask(MCP2515::MASK0, false, 0x7FF);
    mcp2515.setFilter(MCP2515::RXF0, false, TRIGGER_CAN_ID);
    mcp2515.setFilter(MCP2515::RXF1, false, TRIGGER_CAN_ID);
    mcp2515.setFilterMask(MCP2515::MASK1, false, 0x7FF);
    mcp2515.setFilter(MCP2515::RXF2, false, TRIGGER_CAN_ID);
    mcp2515.setFilter(MCP2515::RXF3, false, TRIGGER_CAN_ID);
    mcp2515.setFilter(MCP2515::RXF4, false, TRIGGER_CAN_ID);
    mcp2515.setFilter(MCP2515::RXF5, false, TRIGGER_CAN_ID);

    mcp2515.setBitrate(CAN_250KBPS, CAN_OSC_MHZ >= 16 ? MCP_16MHZ : MCP_8MHZ);
    mcp2515.setNormalMode();

    Serial.println("[SETUP] Wariant HW-filter gotowy - filtr sprzetowy na 0x123, reakcja natychmiast w ISR");
}

void loop() {
    if (!g_frameReady) return;
    g_frameReady = false;

    uint64_t tRespUs = g_tRespUs;
    g_trialCount++;
    Serial.print(g_trialCount);
    Serial.print(',');
    Serial.println((unsigned long)tRespUs);

    // Dopiero TERAZ (po zaraportowaniu reakcji) czyscimy bufor RX przez SPI -
    // zwalnia to linie INT (z powrotem HIGH) i przygotowuje MCP2515 na kolejna
    // probe. To nie wplywa na juz zmierzony t_resp.
    while (mcp2515.readMessage(&rxMsg) == MCP2515::ERROR_OK) { /* drain */ }

    delayMicroseconds(REACTION_PULSE_US);
    digitalWrite(REACTION_PIN, LOW);
}
