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
//
// TRZECI STAN METODYKI ("OTA Update - moment aktualizacji i kompilacji
// nowej reguly w pamieci", dodany 2026-07-30): regula ma TE SAME pola co
// LlmSignalRule w src/core/DecodingAccuracyRunner.h (byteIdx, byteLen,
// littleEndian, isSigned, bitMask, scale, offset) - swiadoma decyzja
// spojnosci architektonicznej z reszta projektu (reguly LLM generowane po
// stronie PC), zamiast wymyslania nowego formatu. Ramka CAN ma tylko 8
// bajtow, wiec dostarczenie 1 reguly wymaga 4 ramek sterujacych (naturalne
// ograniczenie magistrali, warte odnotowania w pracy). "Kompilacja" =
// realny zapis do tabeli aktywnych regul (nie no-op) - ten krok jest
// mierzony tym samym mechanizmem g_busyUs co klasyfikacja w PARSING.

#include <Arduino.h>
#include <SPI.h>
#include <mcp2515.h>
#include <string.h>

#define CAN_CS         5
#define CAN_OSC_MHZ    16
#define CAN_BITRATE    CAN_250KBPS
#define MCP_INT_PIN    4

#define CONTROL_CAN_ID   0x7FE
#define CTRL_RESET       0x01
#define CTRL_REPORT      0x03
#define CTRL_SET_MODE    0x04
#define CTRL_OTA_LOAD1   0x05 // canId(4B) + byteIdx(1B) + byteLen(1B) + flags(1B)
#define CTRL_OTA_LOAD2   0x06 // bitMask(4B)
#define CTRL_OTA_LOAD3   0x07 // scale jako float32 (4B)
#define CTRL_OTA_COMMIT  0x08 // offset jako float32 (4B) + wyzwolenie kompilacji

#define MODE_IDLE      0
#define MODE_PARSING   1
#define MODE_OTA       2

static const int kMaxTrackedIds = 8;
static const int kMaxOtaRules = 8;

static MCP2515 mcp2515(CAN_CS);
static can_frame rxMsg;

volatile bool g_frameReady = false;
static uint8_t  g_mode = MODE_IDLE;
static uint32_t g_frameCount = 0;
static uint32_t g_loopSpins = 0;
static unsigned long g_windowStartMs = 0;
static unsigned long g_windowStartUs = 0;
static uint32_t g_busyUs = 0; // suma czasu pracy (SPI+klasyfikacja/OTA) w oknie
static uint32_t g_otaCommitCount = 0;

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

// Reguly LLM "skompilowane" na urzadzeniu - pola 1:1 z LlmSignalRule
// (src/core/DecodingAccuracyRunner.h), bez `name` (niepotrzebne do decode()
// na urzadzeniu, tylko do diagnostyki po stronie PC).
struct OtaRule {
    uint32_t canId = 0;
    bool     valid = false;
    uint8_t  byteIdx = 0;
    uint8_t  byteLen = 1;
    bool     littleEndian = true;
    bool     isSigned = false;
    uint32_t bitMask = 0xFFFFFFFFu;
    float    scale = 1.0f;
    float    offset = 0.0f;
};
static OtaRule g_otaRules[kMaxOtaRules];

// Bufor stagingowy skladajacy reguly z 4 ramek LOAD1/LOAD2/LOAD3/COMMIT -
// odzwierciedla realne ograniczenie 8-bajtowej ramki CAN (jedna reguła
// z polami j.w. NIE miesci sie w jednej ramce).
struct OtaStaging {
    uint32_t canId = 0;
    uint8_t  byteIdx = 0;
    uint8_t  byteLen = 1;
    bool     littleEndian = true;
    bool     isSigned = false;
    uint32_t bitMask = 0xFFFFFFFFu;
    float    scale = 1.0f;
};
static OtaStaging g_otaStaging;

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

static OtaRule *findOrCreateOtaRule(uint32_t canId) {
    for (int i = 0; i < kMaxOtaRules; ++i) {
        if (g_otaRules[i].valid && g_otaRules[i].canId == canId) return &g_otaRules[i];
    }
    for (int i = 0; i < kMaxOtaRules; ++i) {
        if (!g_otaRules[i].valid) return &g_otaRules[i];
    }
    return nullptr; // tabela pelna - pomijamy (analogicznie do findOrCreateStats)
}

// Realna praca "kompilacji nowej reguly w pamieci" - zapisuje reguly
// zestawione z 4 ramek stagingowych (LOAD1/LOAD2/LOAD3/COMMIT) do tabeli
// aktywnych regul. Uzywana WYLACZNIE w trybie OTA, mierzona tym samym
// mechanizmem co classifyFrame() w PARSING.
static void commitOtaRule(float offset) {
    OtaRule *r = findOrCreateOtaRule(g_otaStaging.canId);
    if (!r) return;
    r->canId        = g_otaStaging.canId;
    r->byteIdx       = g_otaStaging.byteIdx;
    r->byteLen       = g_otaStaging.byteLen;
    r->littleEndian  = g_otaStaging.littleEndian;
    r->isSigned      = g_otaStaging.isSigned;
    r->bitMask       = g_otaStaging.bitMask;
    r->scale         = g_otaStaging.scale;
    r->offset        = offset;
    r->valid         = true;
    g_otaCommitCount++;
}

static void resetCounters() {
    g_frameCount = 0;
    g_loopSpins = 0;
    g_busyUs = 0;
    g_otaCommitCount = 0;
    for (int i = 0; i < kMaxTrackedIds; ++i) g_stats[i] = IdStats{};
    for (int i = 0; i < kMaxOtaRules; ++i) g_otaRules[i] = OtaRule{};
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
    Serial.print(elapsedUs);
    Serial.print(',');
    Serial.println(g_otaCommitCount);
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
                uint8_t m = rxMsg.data[1];
                g_mode = (m == MODE_PARSING) ? MODE_PARSING : (m == MODE_OTA ? MODE_OTA : MODE_IDLE);
            } else if (rxMsg.data[0] == CTRL_REPORT) {
                printReport();
            } else if (rxMsg.data[0] == CTRL_OTA_LOAD1 && rxMsg.can_dlc >= 8) {
                memcpy(&g_otaStaging.canId, &rxMsg.data[1], 4);
                g_otaStaging.byteIdx = rxMsg.data[5];
                g_otaStaging.byteLen = rxMsg.data[6];
                g_otaStaging.littleEndian = (rxMsg.data[7] & 0x01) != 0;
                g_otaStaging.isSigned = (rxMsg.data[7] & 0x02) != 0;
            } else if (rxMsg.data[0] == CTRL_OTA_LOAD2 && rxMsg.can_dlc >= 5) {
                memcpy(&g_otaStaging.bitMask, &rxMsg.data[1], 4);
            } else if (rxMsg.data[0] == CTRL_OTA_LOAD3 && rxMsg.can_dlc >= 5) {
                memcpy(&g_otaStaging.scale, &rxMsg.data[1], 4);
            } else if (rxMsg.data[0] == CTRL_OTA_COMMIT && rxMsg.can_dlc >= 5) {
                float offset = 0.0f;
                memcpy(&offset, &rxMsg.data[1], 4);
                if (g_mode == MODE_OTA) {
                    commitOtaRule(offset); // realna praca "kompilacji" - mierzona ponizej
                }
            }
            continue;
        }

        g_frameCount++;
        if (g_mode == MODE_PARSING) {
            classifyFrame(rxMsg);
        }
        // w IDLE i OTA (poza samym CTRL_OTA_COMMIT): ramka tylko zliczona
    }

    g_busyUs += (unsigned long)(micros() - workStartUs);
}

// app_main() CELOWO usuniety - dostarcza go komponent arduino-esp32
// (CONFIG_AUTOSTART_ARDUINO=y w sdkconfig.defaults), ktory tworzy
// dedykowane zadanie "loopTask" na ARDUINO_RUNNING_CORE (domyslnie CPU1),
// zamiast uruchamiac petle loop() bezposrednio w zadaniu ESP-IDF "main"
// (przypietym do CPU0) - ta druga wersja GLODZILA zadanie IDLE0 (CPU0) bo
// nigdy nie oddawala sterowania, wywolujac powtarzalny Task Watchdog
// Trigger (zdiagnozowane 2026-07-30, patrz README.md).
