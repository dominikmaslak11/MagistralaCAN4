// main.cpp — Eksperyment 5.1 (wariant natywny ESP-IDF, "-jtag"), wg
// "Pomiary dla CAN-Edge AI.md", Grupa 5.
//
// Port 1:1 logiki z esp_experiment_5_1/esp_experiment_5_1.ino (Arduino) na
// natywny projekt ESP-IDF, zeby mozna bylo skonfigurowac
// CONFIG_APPTRACE_SV_ENABLE (SystemView przez JTAG/ESP-Prog) w sdkconfig -
// niedostepne z poziomu czystego szkicu .ino. Sam protokol sterujacy,
// klasyfikacja per-ID i sposob liczenia CPU (bezposredni micros() -
// zobacz uzasadnienie w oryginalnym .ino, proba z vTaskGetRunTimeStats()
// zawiodla dla tej architektury) pozostaja BEZ ZMIAN, zeby wyniki byly
// porownywalne z juz zebranymi danymi.
//
// Uzywa Arduino jako komponentu ESP-IDF (idf_component.yml:
// espressif/arduino-esp32) - stad wlasny app_main() wywolujacy
// initArduino()+setup()+loop(), zamiast automatycznego entry-pointu
// Arduino IDE. Sterownik MCP2515 to zwendorowana (skopiowana 1:1 z lokalnej
// instalacji Arduino IDE tego uzytkownika, NIE przepisana od zera)
// biblioteka "autowp-mcp2515" - components/mcp2515/.
//
// Po wlaczeniu SystemView (Component config -> Application Level Tracing)
// JTAG (ESP-Prog) daje NIEZALEZNY, sprzetowy podglad przelaczen
// kontekstu/ISR w czasie rzeczywistym w SystemView GUI - uzupelnienie (nie
// zastapienie) pomiaru cpu_busy_pct ponizej, ktory zostaje glownym zrodlem
// liczb do tabeli metodyki.

#include <Arduino.h>
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
static unsigned long g_windowStartUs = 0;
static uint32_t g_busyUs = 0; // suma czasu pracy (SPI+klasyfikacja) w oknie

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
    g_busyUs = 0;
    for (int i = 0; i < kMaxTrackedIds; ++i) g_stats[i] = IdStats{};
    g_windowStartMs = millis();
    g_windowStartUs = micros();
}

static void printReport() {
    unsigned long elapsedMs = millis() - g_windowStartMs;
    unsigned long elapsedUs = micros() - g_windowStartUs;

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
    Serial.print(ESP.getFreeSketchSpace());
    Serial.print(',');
    Serial.print(g_busyUs);
    Serial.print(',');
    Serial.println(elapsedUs);
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
    g_windowStartUs = micros();

    Serial.print("[SETUP] Eksperyment 5.1-jtag gotowy. HeapSize=");
    Serial.print(ESP.getHeapSize());
    Serial.print(" SketchSize=");
    Serial.println(ESP.getSketchSize());
}

void loop() {
    g_loopSpins++;

    if (!g_frameReady) return;
    g_frameReady = false;

    unsigned long workStartUs = micros();

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

    g_busyUs += (unsigned long)(micros() - workStartUs);
}

extern "C" void app_main() {
    initArduino();
    setup();
    while (true) {
        loop();
    }
}
