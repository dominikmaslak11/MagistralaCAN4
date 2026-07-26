// esp_experiment_1_1.ino — Eksperyment 1.1: Cold Start Latency Breakdown
// Sprzet: ESP32 + MCP2515 (SPI, CS=GPIO5) — ten sam modul co esp_mcp.ino.
//
// Rola tego firmware w eksperymencie ("Pomiary dla CAN-Edge AI.md", 1.1):
//   1. Sniffuje magistrale CAN przez MCP2515.
//   2. Znakuje kazda ramke znacznikiem czasu w mikrosekundach (esp_timer_get_time()).
//   3. Wysyla KAZDA ramke bezprzewodowo (WiFi + WebSocket) do MagistralaCAN4
//      (WebSocketServer, patrz src/core/WebSocketServer.cpp) — to serwer decyduje,
//      czy dana ramka to "Cold Start" (ColdStartDetector) i odpytuje LLM.
//   4. Po odpowiedzi LLM serwer wysyla regule ("apply_rule") z powrotem — firmware
//      "instaluje" ja lokalnie i natychmiast odsyla potwierdzenie ("rule_ack"),
//      co pozwala serwerowi (ExperimentRunner) zmierzyc realny czas t_ota.
//   5. Kalibracja zegara (time_sync, styl NTP: pojedyncza probka + korekta RTT/2)
//      pozwala serwerowi zmierzyc realny czas transmisji bezprzewodowej t_tx_up
//      (zegar ESP32 i zegar serwera nie sa fizycznie zsynchronizowane inaczej).
//
// Wymagane biblioteki (Arduino Library Manager):
//   - "MCP2515" by autowp                (ten sam co esp_mcp.ino)
//   - "WebSockets" by Markus Sattler      (Links2004/arduinoWebSockets)
//   - "ArduinoJson" by Benoit Blanchon    (v6.x)
//   - rdzen "ESP32" (Espressif) — WiFi.h wbudowane
//
// Protokol WebSocket zgodny z src/core/WebSocketServer.cpp:
//   ESP32 -> serwer:
//     {"type":"time_sync","espTime":<us>}
//     {"type":"send_frame","id":N,"extended":bool,"rtr":bool,"fd":false,
//      "dlc":N,"timestamp":<us, skorygowany o offset>,"data":"<hex>"}
//     {"type":"rule_ack","canId":N}
//   serwer -> ESP32:
//     {"type":"time_sync_ack","espTime":<echo>,"serverTime":<us>}
//     {"type":"frame_ack","id":N,"status":"ok"}
//     {"type":"apply_rule","canId":N,"ruleText":"<surowa odpowiedz LLM>"}

#include <SPI.h>
#include <mcp2515.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

// ──────────────────────────────────────────────────────────────
// KONFIGURACJA — utworz esp_experiment_1_1/secrets.h (patrz secrets.h.example)
// z prawdziwymi danymi WiFi. Plik secrets.h jest w .gitignore — dzieki temu
// prawdziwe haslo WiFi nigdy nie trafia do repozytorium (w odroznieniu od
// kluczy API w archiwum_python_v1, ktore trzeba bylo pozniej redagowac).
// Jesli secrets.h nie istnieje, uzywane sa ponizsze wartosci domyslne/placeholdery.
// ──────────────────────────────────────────────────────────────
#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID      "TWOJE_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD  "TWOJE_HASLO"
#endif
#ifndef WS_SERVER_IP
#define WS_SERVER_IP   "192.168.1.100"   // adres IP komputera z MagistralaCAN4
#endif
#ifndef WS_SERVER_PORT
#define WS_SERVER_PORT 9000              // patrz komunikat startowy --run-experiment-hw
#endif
#ifndef WS_SERVER_PATH
#define WS_SERVER_PATH "/"
#endif

#define CAN_CS         5
#define CAN_SPEED_BPS  250000UL          // 250 kbps — jak w metodyce (Grupa 2)
#define CAN_OSC_MHZ    16                // czestotliwosc kwarcu na module MCP2515 (8 lub 16)

#define STATUS_LED     2                 // wbudowana LED na wiekszosci devkitow (opcjonalne)

// ──────────────────────────────────────────────────────────────
// Stan globalny
// ──────────────────────────────────────────────────────────────
static MCP2515       mcp2515(CAN_CS);
static can_frame      rxMsg;
static WebSocketsClient webSocket;

static bool     wsConnected  = false;
static bool     timeSynced   = false;
static int64_t  clockOffsetUs = 0;       // korekta: t_server ~= esp_timer_get_time() + offset
static uint64_t timeSyncSentAtUs = 0;
static unsigned long lastTimeSyncAttemptMs = 0;

// Prosta lokalna "tabela regul" (efekt uboczny "instalacji" reguly z LLM —
// realna praca wykonywana podczas t_ota, nie placeholder).
struct StoredRule {
    uint32_t canId = 0;
    bool     valid = false;
    char     ruleTextTrunc[128] = {0};
};
#define RULE_TABLE_SIZE 32
static StoredRule ruleTable[RULE_TABLE_SIZE];

// ──────────────────────────────────────────────────────────────
// Pomocnicze
// ──────────────────────────────────────────────────────────────
static void blinkStatus(int times, int ms) {
    for (int i = 0; i < times; i++) {
        digitalWrite(STATUS_LED, HIGH);
        delay(ms);
        digitalWrite(STATUS_LED, LOW);
        if (i < times - 1) delay(ms);
    }
}

static String bytesToHex(const uint8_t *data, uint8_t len) {
    static const char hexDigits[] = "0123456789abcdef";
    String out;
    out.reserve(len * 2);
    for (uint8_t i = 0; i < len; i++) {
        out += hexDigits[(data[i] >> 4) & 0x0F];
        out += hexDigits[data[i] & 0x0F];
    }
    return out;
}

// Zwraca timestamp "porownywalny z zegarem serwera" — surowy esp_timer_get_time()
// skorygowany o offset wyliczony podczas time_sync. Dopoki brak kalibracji,
// zwraca surowy czas ESP32 (t_tx_up bedzie wtedy bledny do czasu zsynchronizowania).
static uint64_t serverEquivalentTimeUs() {
    return static_cast<uint64_t>(static_cast<int64_t>(esp_timer_get_time()) + clockOffsetUs);
}

// Znajduje slot w tabeli regul dla danego CAN ID (istniejacy lub pierwszy wolny/LRU).
static StoredRule *findRuleSlot(uint32_t canId) {
    for (auto &r : ruleTable)
        if (r.valid && r.canId == canId) return &r;
    for (auto &r : ruleTable)
        if (!r.valid) return &r;
    return &ruleTable[0]; // tabela pelna — nadpisz najstarszy slot (uproszczenie)
}

// ──────────────────────────────────────────────────────────────
// WebSocket: wysylanie wiadomosci
// ──────────────────────────────────────────────────────────────
static void sendTimeSync() {
    StaticJsonDocument<128> doc;
    doc["type"] = "time_sync";
    timeSyncSentAtUs = esp_timer_get_time();
    doc["espTime"] = timeSyncSentAtUs;
    String out;
    serializeJson(doc, out);
    webSocket.sendTXT(out);
    lastTimeSyncAttemptMs = millis();
}

static void sendCanFrame(const can_frame &f) {
    bool extended = (f.can_id & CAN_EFF_FLAG) != 0;
    bool rtr      = (f.can_id & CAN_RTR_FLAG) != 0;
    uint32_t id   = f.can_id & (extended ? CAN_EFF_MASK : CAN_SFF_MASK);
    uint8_t dlc   = f.can_dlc > 8 ? 8 : f.can_dlc;

    StaticJsonDocument<256> doc;
    doc["type"]      = "send_frame";
    doc["id"]        = id;
    doc["extended"]  = extended;
    doc["rtr"]       = rtr;
    doc["fd"]        = false;
    doc["dlc"]       = dlc;
    doc["timestamp"] = serverEquivalentTimeUs();
    doc["data"]      = bytesToHex(f.data, dlc);

    String out;
    serializeJson(doc, out);
    webSocket.sendTXT(out);
}

static void sendRuleAck(uint32_t canId) {
    StaticJsonDocument<64> doc;
    doc["type"]  = "rule_ack";
    doc["canId"] = canId;
    String out;
    serializeJson(doc, out);
    webSocket.sendTXT(out);
}

// ──────────────────────────────────────────────────────────────
// WebSocket: obsluga wiadomosci przychodzacych
// ──────────────────────────────────────────────────────────────
static void handleTimeSyncAck(JsonDocument &doc) {
    uint64_t echoedEspTime = doc["espTime"].as<uint64_t>();
    int64_t  serverTime    = doc["serverTime"].as<int64_t>();
    uint64_t nowUs         = esp_timer_get_time();

    if (echoedEspTime != timeSyncSentAtUs) return; // spozniona/nieodpowiadajaca odpowiedz

    int64_t rttUs = static_cast<int64_t>(nowUs) - static_cast<int64_t>(echoedEspTime);
    if (rttUs < 0) rttUs = 0;
    // Klasyczna jednopróbkowa estymacja NTP: offset = t_server - t_esp - RTT/2
    clockOffsetUs = serverTime - static_cast<int64_t>(echoedEspTime) - rttUs / 2;
    timeSynced = true;

    Serial.printf("[time_sync] rtt=%lldus offset=%lldus\n",
                  (long long)rttUs, (long long)clockOffsetUs);
}

// Instaluje regule lokalnie (realna praca liczona jako t_ota) i potwierdza.
static void handleApplyRule(JsonDocument &doc) {
    uint32_t canId = doc["canId"].as<uint32_t>();
    const char *ruleText = doc["ruleText"] | "";

    StoredRule *slot = findRuleSlot(canId);
    slot->canId = canId;
    slot->valid = true;
    strncpy(slot->ruleTextTrunc, ruleText, sizeof(slot->ruleTextTrunc) - 1);
    slot->ruleTextTrunc[sizeof(slot->ruleTextTrunc) - 1] = '\0';

    Serial.printf("[apply_rule] CAN ID 0x%03X zainstalowana, ruleText[:40]=%.40s\n",
                  canId, ruleText);

    sendRuleAck(canId); // im szybciej, tym mniejszy (bardziej realny) t_ota
    blinkStatus(1, 30);
}

static void onWsEvent(WStype_t type, uint8_t *payload, size_t length) {
    switch (type) {
    case WStype_CONNECTED:
        wsConnected = true;
        timeSynced  = false;
        Serial.println("[WS] Polaczono z MagistralaCAN4 — wysylam time_sync");
        sendTimeSync();
        blinkStatus(2, 100);
        break;

    case WStype_DISCONNECTED:
        wsConnected = false;
        Serial.println("[WS] Rozlaczono");
        break;

    case WStype_TEXT: {
        StaticJsonDocument<1024> doc;
        DeserializationError err = deserializeJson(doc, payload, length);
        if (err) {
            Serial.printf("[WS] Blad parsowania JSON: %s\n", err.c_str());
            return;
        }
        const char *msgType = doc["type"] | "";
        if (strcmp(msgType, "time_sync_ack") == 0) {
            handleTimeSyncAck(doc);
        } else if (strcmp(msgType, "apply_rule") == 0) {
            handleApplyRule(doc);
        } else if (strcmp(msgType, "frame_ack") == 0) {
            // potwierdzenie odbioru ramki — nic do zrobienia
        } else if (strcmp(msgType, "auth_error") == 0) {
            Serial.println("[WS] Blad autoryzacji (serwer w trybie WSS wymaga tokenu)");
        }
        break;
    }

    default:
        break;
    }
}

// ──────────────────────────────────────────────────────────────
// Setup
// ──────────────────────────────────────────────────────────────
static void setupCan() {
    SPI.begin();
    while (mcp2515.reset() != MCP2515::ERROR_OK) delay(500);
    mcp2515.setFilterMask(MCP2515::MASK0, true, 0x00000000);
    mcp2515.setFilterMask(MCP2515::MASK1, true, 0x00000000);

    CAN_SPEED spd = CAN_250KBPS;
    if (CAN_SPEED_BPS >= 900000)      spd = CAN_1000KBPS;
    else if (CAN_SPEED_BPS >= 350000) spd = CAN_500KBPS;
    else if (CAN_SPEED_BPS >= 150000) spd = CAN_250KBPS;
    else                              spd = CAN_125KBPS;

    mcp2515.setBitrate(spd, CAN_OSC_MHZ >= 16 ? MCP_16MHZ : MCP_8MHZ);
    mcp2515.setNormalMode();
    Serial.println("[CAN] MCP2515 gotowy");
}

static void setupWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("[WiFi] Laczenie");
    while (WiFi.status() != WL_CONNECTED) {
        delay(300);
        Serial.print(".");
    }
    Serial.println();
    Serial.print("[WiFi] Polaczono, IP: ");
    Serial.println(WiFi.localIP());
}

void setup() {
    Serial.begin(115200);
    delay(200);
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);

    setupCan();
    setupWifi();

    webSocket.begin(WS_SERVER_IP, WS_SERVER_PORT, WS_SERVER_PATH);
    webSocket.onEvent(onWsEvent);
    webSocket.setReconnectInterval(2000);

    Serial.println("[SETUP] Gotowe — oczekiwanie na ramki CAN");
}

// ──────────────────────────────────────────────────────────────
// Main loop
// ──────────────────────────────────────────────────────────────
void loop() {
    webSocket.loop();

    // Retry time_sync co 3s, dopoki nie potwierdzone (np. po (re)polaczeniu)
    if (wsConnected && !timeSynced && (millis() - lastTimeSyncAttemptMs > 3000)) {
        sendTimeSync();
    }

    // Sniffuj magistrale CAN — kazda ramka wyslana natychmiast do serwera.
    // (Serwer, nie ESP32, decyduje czy to "Cold Start" — patrz ColdStartDetector.)
    if (mcp2515.readMessage(&rxMsg) == MCP2515::ERROR_OK) {
        if (wsConnected && timeSynced) {
            sendCanFrame(rxMsg);
        }
        // Jesli WiFi/WS jeszcze nie gotowe lub zegar niezsynchronizowany,
        // ramka jest celowo pomijana — bez wiarygodnego timestampu t_tx_up
        // byłaby bezuzyteczna dla pomiaru.
    }
}
