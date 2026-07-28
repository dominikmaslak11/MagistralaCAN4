// esp_experiment_5_1.ino — Eksperyment 5.1: Profilowanie pamieci i CPU,
// wg "Pomiary dla CAN-Edge AI.md", Grupa 5.
//
// Mierzy zuzycie RAM/Flash/CPU ESP32 w DWOCH stanach pracy (trzeci stan,
// "OTA Update - moment aktualizacji i kompilacji nowej reguly w pamieci",
// NIE jest realizowany - taki mechanizm jeszcze nie istnieje nigdzie w tym
// projekcie; reguly LLM zyja wylacznie po stronie PC, patrz
// Eksperyment_4.2_Propozycja_Dalszej_Optymalizacji_LLM):
//
//   IDLE    - nasluchiwanie magistrali, ramki tylko zliczane, zero
//             dodatkowej pracy per ramka.
//   PARSING - "aktywna filtracja i klasyfikacja ruchu": dla kazdej ramki
//             (oprocz ramki sterujacej) prowadzona jest lekka, ale
//             PRAWDZIWA klasyfikacja per-ID (min/max/liczba przelaczen
//             bajtu 0) w tabeli do kMaxTrackedIds wpisow - analogiczny w
//             duchu (choc prostszy) rodzaj analizy co hybrydowy override
//             po stronie PC (DecodingAccuracyRunner::looksLikeBitFlags()).
//
// Miary:
//   RAM   - ESP.getFreeHeap()/getHeapSize() (biezace), ESP.getMinFreeHeap()
//           (najnizszy poziom od resetu - "worst case").
//   Flash - ESP.getSketchSize()/getFreeSketchSpace() - STATYCZNE (nie
//           zmieniaja sie w zaleznosci od stanu pracy w czasie dzialania,
//           raportowane dla kompletnosci tabeli, nie dla porownania
//           miedzystanowego).
//   CPU   - PROXY: g_loopSpins zlicza kazda iteracje glownej petli loop()
//           w oknie miedzy RESET a REPORT. Interpretacja (spins/s ->
//           %obciazenia, wzgledem kalibracji na pustej magistrali w IDLE)
//           odbywa sie po stronie hosta (run_experiment_5_1.py) - to
//           swiadomie uproszczony, jawnie opisany zastepnik pelnego
//           profilera FreeRTOS (vTaskGetRunTimeStats wymaga zmiany
//           sdkconfig, niedostepnej wprost z poziomu szkicu Arduino) -
//           mierzy obciazenie GLOWNEJ petli sterujacej (tam, gdzie faktycznie
//           dzieje sie cala logika CAN w tej architekturze), nie calego
//           systemu wielozadaniowego.
//
// Protokol sterujacy (CAN ID CONTROL_CAN_ID=0x7FE), ten sam wzorzec co
// Eksperyment 2.2:
//   data[0]=0x01              -> RESET (zeruje liczniki, znaczniki czasu)
//   data[0]=0x04, data[1]=tryb -> SET_MODE (0=IDLE, 1=PARSING)
//   data[0]=0x03              -> REPORT (wypisz wynik przez Serial)

#include <SPI.h>
#include <mcp2515.h>

#define CAN_CS         5
#define CAN_OSC_MHZ    16
#define CAN_BITRATE    CAN_250KBPS
#define MCP_INT_PIN    4

#define CONTROL_CAN_ID 0x7FE
#define CTRL_RESET     0x01
#define CTRL_REPORT    0x03
#define CTRL_SET_MODE  0x04

#define MODE_IDLE      0
#define MODE_PARSING   1

static const int kMaxTrackedIds = 8;

static MCP2515 mcp2515(CAN_CS);
static can_frame rxMsg;

volatile bool g_frameReady = false;
static uint8_t  g_mode = MODE_IDLE;
static uint32_t g_frameCount = 0;
static uint32_t g_loopSpins = 0;
static unsigned long g_windowStartMs = 0;

// Tabela klasyfikacji per-ID uzywana WYLACZNIE w trybie PARSING - realna
// praca obliczeniowa (nie no-op), zeby stan roznil sie od IDLE czyms
// wiecej niz samym zliczaniem.
struct IdStats {
    uint32_t canId = 0;
    bool     used = false;
    uint8_t  minByte0 = 0xFF;
    uint8_t  maxByte0 = 0x00;
    uint8_t  lastByte0 = 0;
    bool     hasLast = false;
    uint32_t toggleCount = 0;
};
static IdStats g_stats[kMaxTrackedIds];

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

    g_windowStartMs = millis();

    Serial.print("[SETUP] Eksperyment 5.1 gotowy. HeapSize=");
    Serial.print(ESP.getHeapSize());
    Serial.print(" SketchSize=");
    Serial.println(ESP.getSketchSize());
}

static IdStats *findOrCreateStats(uint32_t canId) {
    for (int i = 0; i < kMaxTrackedIds; ++i) {
        if (g_stats[i].used && g_stats[i].canId == canId) return &g_stats[i];
    }
    for (int i = 0; i < kMaxTrackedIds; ++i) {
        if (!g_stats[i].used) {
            g_stats[i] = IdStats{};
            g_stats[i].used = true;
            g_stats[i].canId = canId;
            return &g_stats[i];
        }
    }
    return nullptr; // tabela pelna - w PARSING po prostu pomijamy klasyfikacje tej ramki
}

// Realna praca "filtracji i klasyfikacji" per ramka - uzywana WYLACZNIE
// w trybie PARSING. Aktualizuje min/max/przelaczenia bajtu 0 dla danego ID.
static void classifyFrame(const can_frame &f) {
    bool extended = (f.can_id & CAN_EFF_FLAG) != 0;
    uint32_t id = f.can_id & (extended ? CAN_EFF_MASK : CAN_SFF_MASK);
    IdStats *st = findOrCreateStats(id);
    if (!st) return;

    uint8_t v = f.can_dlc > 0 ? f.data[0] : 0;
    if (v < st->minByte0) st->minByte0 = v;
    if (v > st->maxByte0) st->maxByte0 = v;
    if (st->hasLast && v != st->lastByte0) st->toggleCount++;
    st->lastByte0 = v;
    st->hasLast = true;
}

static void resetCounters() {
    g_frameCount = 0;
    g_loopSpins = 0;
    for (int i = 0; i < kMaxTrackedIds; ++i) g_stats[i] = IdStats{};
    g_windowStartMs = millis();
}

static void printReport() {
    unsigned long elapsedMs = millis() - g_windowStartMs;
    Serial.print("RESULT,");
    Serial.print(g_mode);
    Serial.print(',');
    Serial.print(elapsedMs);
    Serial.print(',');
    Serial.print(g_frameCount);
    Serial.print(',');
    Serial.print(g_loopSpins);
    Serial.print(',');
    Serial.print(ESP.getFreeHeap());
    Serial.print(',');
    Serial.print(ESP.getMinFreeHeap());
    Serial.print(',');
    Serial.print(ESP.getHeapSize());
    Serial.print(',');
    Serial.print(ESP.getSketchSize());
    Serial.print(',');
    Serial.println(ESP.getFreeSketchSpace());
}

void loop() {
    g_loopSpins++;

    if (!g_frameReady) return;
    g_frameReady = false;

    while (mcp2515.readMessage(&rxMsg) == MCP2515::ERROR_OK) {
        bool extended = (rxMsg.can_id & CAN_EFF_FLAG) != 0;
        uint32_t id = rxMsg.can_id & (extended ? CAN_EFF_MASK : CAN_SFF_MASK);

        if (id == CONTROL_CAN_ID && rxMsg.can_dlc >= 1) {
            if (rxMsg.data[0] == CTRL_RESET) {
                resetCounters();
            } else if (rxMsg.data[0] == CTRL_SET_MODE && rxMsg.can_dlc >= 2) {
                g_mode = rxMsg.data[1] == MODE_PARSING ? MODE_PARSING : MODE_IDLE;
            } else if (rxMsg.data[0] == CTRL_REPORT) {
                printReport();
            }
            continue;
        }

        g_frameCount++;
        if (g_mode == MODE_PARSING) {
            classifyFrame(rxMsg);
        }
        // w IDLE: ramka tylko zliczona, zero dodatkowej pracy
    }
}
