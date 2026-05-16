// esp_slcan_relay.ino — SLCAN bridge + advanced relay control v2
// Protocol: Lawicel SLCAN over USB Serial (115200 baud)
// Compatible with MagistralaCAN4 SlCanDriver and SavvyCAN
//
// Features:
//   1. Multi-relay rules     — każdy z 6 przekaźników ma własną regułę CAN
//   2. Watchdog bezpieczeństwa — brak ramki → relay OFF po watchdogMs
//   3. Debounce              — min czas między zmianami stanu
//   4. NVS persistence       — konfiguracja w flash, przeżywa restart
//   5. Status CAN TX         — bitmapa przekaźników po każdej zmianie stanu
//   6. Pulse mode            — relay ON na pulseMs, potem auto-OFF
//   7. Minimalny czas ON     — ochrona styków przed zbyt szybkim wyłączeniem
//   8. Stan startowy         — konfigurowalny stan po zaniku zasilania (ON/OFF)
//   9. Heartbeat TX          — cykliczna ramka "żyję" z bitmapą przekaźników
//
// Komendy serial (poza standardowymi SLCAN):
//   SETRELAY <id> <canId_hex> <EXT|STD> <byteIdx> <bitMask_hex> [wdog_ms] [dbnc_ms]
//   SETMODE  <id> LATCH | PULSE <ms>
//   SETMINON <id> <ms>
//   SETBOOT  <id> ON | OFF
//   SETSTATUS    <canId_hex> <EXT|STD>  |  SETSTATUS OFF
//   SETHEARTBEAT <canId_hex> <EXT|STD> <interval_ms>  |  SETHEARTBEAT OFF
//   SAVECONFIG   — zapisz konfigurację do NVS
//   SHOWCONFIG   — wydrukuj konfigurację
//   RELAY  <id> <0|1|ON|OFF>   — ręczne sterowanie
//   STATUS                      — stan wszystkich przekaźników

#include <SPI.h>
#include <mcp2515.h>
#include <Preferences.h>

// ──────────────────────────────────────────────────────────────
// Pinout
// ──────────────────────────────────────────────────────────────
#define CAN_CS      5
#define RELAY_COUNT 6
const int relayPins[RELAY_COUNT] = {2, 15, 13, 12, 25, 26};

// ──────────────────────────────────────────────────────────────
// Typy pomocnicze
// ──────────────────────────────────────────────────────────────
enum RelayMode : uint8_t { LATCH = 0, PULSE = 1 };
enum BootState : uint8_t { BOOT_OFF = 0, BOOT_ON = 1 };

// ──────────────────────────────────────────────────────────────
// Konfiguracja reguły (NVS-persistent)
// ──────────────────────────────────────────────────────────────
struct RelayRule {
    uint32_t  canId;        // 0 = reguła wyłączona
    bool      isExtended;   // EXT (29-bit) / STD (11-bit)
    uint8_t   byteIndex;    // który bajt danych (0-7)
    uint8_t   bitMask;      // które bity (np. 0x01 = bit 0)
    uint32_t  watchdogMs;   // 0=wyłączony; inaczej relay OFF po braku ramki
    uint32_t  debounceMs;   // min czas [ms] między zmianami stanu
    RelayMode mode;         // LATCH: trzyma stan | PULSE: ON na pulseMs, potem OFF
    uint32_t  pulseMs;      // czas impulsu w trybie PULSE
    uint32_t  minOnMs;      // min czas ON zanim będzie możliwe wyłączenie
    BootState bootState;    // stan po zaniku zasilania
};

// ──────────────────────────────────────────────────────────────
// Stan runtime (nie jest persistentny)
// ──────────────────────────────────────────────────────────────
struct RelayState {
    bool     current;        // aktualny stan GPIO
    bool     watchdogArmed;  // true po pierwszej pasującej ramce CAN
    uint32_t lastMatchMs;    // millis() ostatniej pasującej ramki
    uint32_t lastChangeMs;   // millis() ostatniej zmiany stanu
    bool     inPulse;        // trwa impuls (tryb PULSE)
    uint32_t pulseEndMs;     // millis() końca impulsu
    uint32_t onSinceMs;      // millis() kiedy relay się załączył
};

// ──────────────────────────────────────────────────────────────
// Konfiguracja ramki statusowej i heartbeat
// ──────────────────────────────────────────────────────────────
struct StatusConfig {
    uint32_t canId;      // 0 = wyłączona
    bool     isExtended;
};

struct HeartbeatConfig {
    uint32_t canId;       // 0 = wyłączony
    bool     isExtended;
    uint32_t intervalMs;  // co ile ms wysyłać
};

// ──────────────────────────────────────────────────────────────
// Globale
// ──────────────────────────────────────────────────────────────
static RelayRule      rules[RELAY_COUNT];
static RelayState     states[RELAY_COUNT];
static StatusConfig   statusCfg;
static HeartbeatConfig hbCfg;
static uint32_t       lastHbMs = 0;
static Preferences    prefs;

static MCP2515   mcp(CAN_CS);
static can_frame rxMsg, txMsg;
static bool      channelOpen  = false;
static bool      listenOnly   = false;
static CAN_SPEED currentSpeed = CAN_250KBPS;

#define SERIAL_BUF_SIZE 128
static char    serialBuf[SERIAL_BUF_SIZE];
static uint8_t serialIdx = 0;

// ──────────────────────────────────────────────────────────────
// Domyślna konfiguracja
// ──────────────────────────────────────────────────────────────
static void defaultConfig() {
    rules[0] = {0x1421003FUL, true, 1, 0x01, 5000, 50, LATCH, 500, 0, BOOT_OFF};
    for (uint8_t i = 1; i < RELAY_COUNT; i++)
        rules[i] = {0, false, 0, 0x01, 0, 50, LATCH, 500, 0, BOOT_OFF};
    statusCfg = {0x7FFUL, false};
    hbCfg     = {0, false, 1000};  // disabled — device operates anonymously
}

// ──────────────────────────────────────────────────────────────
// NVS
// ──────────────────────────────────────────────────────────────
static void loadConfig() {
    defaultConfig();
    if (!prefs.begin("relay", true)) return;
    char k[10];
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        snprintf(k,sizeof(k),"r%did",   i); rules[i].canId      = prefs.getUInt(k,  rules[i].canId);
        snprintf(k,sizeof(k),"r%dext",  i); rules[i].isExtended = prefs.getBool(k,  rules[i].isExtended);
        snprintf(k,sizeof(k),"r%dbyte", i); rules[i].byteIndex  = prefs.getUChar(k, rules[i].byteIndex);
        snprintf(k,sizeof(k),"r%dmask", i); rules[i].bitMask    = prefs.getUChar(k, rules[i].bitMask);
        snprintf(k,sizeof(k),"r%dwdog", i); rules[i].watchdogMs = prefs.getUInt(k,  rules[i].watchdogMs);
        snprintf(k,sizeof(k),"r%ddbnc", i); rules[i].debounceMs = prefs.getUInt(k,  rules[i].debounceMs);
        snprintf(k,sizeof(k),"r%dmode", i); rules[i].mode       = (RelayMode)prefs.getUChar(k, rules[i].mode);
        snprintf(k,sizeof(k),"r%dpuls", i); rules[i].pulseMs    = prefs.getUInt(k,  rules[i].pulseMs);
        snprintf(k,sizeof(k),"r%dmnon", i); rules[i].minOnMs    = prefs.getUInt(k,  rules[i].minOnMs);
        snprintf(k,sizeof(k),"r%dboot", i); rules[i].bootState  = (BootState)prefs.getUChar(k, rules[i].bootState);
    }
    statusCfg.canId      = prefs.getUInt("stid",  statusCfg.canId);
    statusCfg.isExtended = prefs.getBool("stext", statusCfg.isExtended);
    hbCfg.canId          = prefs.getUInt("htid",  hbCfg.canId);
    hbCfg.isExtended     = prefs.getBool("htext", hbCfg.isExtended);
    hbCfg.intervalMs     = prefs.getUInt("htint", hbCfg.intervalMs);
    prefs.end();
}

static bool saveConfig() {
    if (!prefs.begin("relay", false)) return false;
    char k[10];
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        snprintf(k,sizeof(k),"r%did",   i); prefs.putUInt(k,  rules[i].canId);
        snprintf(k,sizeof(k),"r%dext",  i); prefs.putBool(k,  rules[i].isExtended);
        snprintf(k,sizeof(k),"r%dbyte", i); prefs.putUChar(k, rules[i].byteIndex);
        snprintf(k,sizeof(k),"r%dmask", i); prefs.putUChar(k, rules[i].bitMask);
        snprintf(k,sizeof(k),"r%dwdog", i); prefs.putUInt(k,  rules[i].watchdogMs);
        snprintf(k,sizeof(k),"r%ddbnc", i); prefs.putUInt(k,  rules[i].debounceMs);
        snprintf(k,sizeof(k),"r%dmode", i); prefs.putUChar(k, (uint8_t)rules[i].mode);
        snprintf(k,sizeof(k),"r%dpuls", i); prefs.putUInt(k,  rules[i].pulseMs);
        snprintf(k,sizeof(k),"r%dmnon", i); prefs.putUInt(k,  rules[i].minOnMs);
        snprintf(k,sizeof(k),"r%dboot", i); prefs.putUChar(k, (uint8_t)rules[i].bootState);
    }
    prefs.putUInt("stid",  statusCfg.canId);
    prefs.putBool("stext", statusCfg.isExtended);
    prefs.putUInt("htid",  hbCfg.canId);
    prefs.putBool("htext", hbCfg.isExtended);
    prefs.putUInt("htint", hbCfg.intervalMs);
    prefs.end();
    return true;
}

// ──────────────────────────────────────────────────────────────
// Ramka statusowa CAN — bitmapa przekaźników, wysyłana przy każdej zmianie
// DLC=1, data[0] bit N = relay N
// ──────────────────────────────────────────────────────────────
static void sendStatusFrame() {
    if (statusCfg.canId == 0 || listenOnly) return;
    uint8_t mask = 0;
    for (uint8_t i = 0; i < RELAY_COUNT; i++)
        if (states[i].current) mask |= (uint8_t)(1u << i);
    can_frame f;
    f.can_id  = statusCfg.isExtended ? (statusCfg.canId | CAN_EFF_FLAG) : statusCfg.canId;
    f.can_dlc = 1;
    f.data[0] = mask;
    mcp.sendMessage(&f);
}

// ──────────────────────────────────────────────────────────────
// Heartbeat CAN — wysyłany cyklicznie co hbCfg.intervalMs
// DLC=2, data[0]=bitmapa, data[1]=0xAA (alive marker)
// ──────────────────────────────────────────────────────────────
static void checkHeartbeat() {
    if (hbCfg.canId == 0 || listenOnly) return;
    uint32_t now = millis();
    if (now - lastHbMs < hbCfg.intervalMs) return;
    lastHbMs = now;
    uint8_t mask = 0;
    for (uint8_t i = 0; i < RELAY_COUNT; i++)
        if (states[i].current) mask |= (uint8_t)(1u << i);
    can_frame f;
    f.can_id  = hbCfg.isExtended ? (hbCfg.canId | CAN_EFF_FLAG) : hbCfg.canId;
    f.can_dlc = 2;
    f.data[0] = mask;
    f.data[1] = 0xAA;  // alive marker
    mcp.sendMessage(&f);
}

// ──────────────────────────────────────────────────────────────
// Ustaw stan GPIO przekaźnika z obsługą debounce i minOnMs
//   force=true → komenda ręczna, pomija debounce i minOnMs
// ──────────────────────────────────────────────────────────────
static void setRelayGpio(uint8_t idx, bool newState, bool force = false) {
    if (idx >= RELAY_COUNT) return;
    RelayRule  &rule = rules[idx];
    RelayState &st   = states[idx];
    if (newState == st.current) return;

    uint32_t now = millis();

    if (!force) {
        // Debounce
        if ((now - st.lastChangeMs) < rule.debounceMs) return;
        // Minimalny czas ON — blokuj wyłączenie jeśli zbyt wcześnie
        if (!newState && rule.minOnMs > 0 && st.current)
            if ((now - st.onSinceMs) < rule.minOnMs) return;
    }

    digitalWrite(relayPins[idx], newState ? HIGH : LOW);
    if (newState) st.onSinceMs = now;
    st.current      = newState;
    st.lastChangeMs = now;
    sendStatusFrame();
}

// ──────────────────────────────────────────────────────────────
// Zastosowanie reguły do ramki CAN (wywoływane dla każdej ramki)
// ──────────────────────────────────────────────────────────────
static void applyRelayLogic(const can_frame &f) {
    bool     isExt = (f.can_id & CAN_EFF_FLAG) != 0;
    uint32_t id    = f.can_id & (isExt ? CAN_EFF_MASK : CAN_SFF_MASK);
    uint32_t now   = millis();

    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        RelayRule  &rule = rules[i];
        RelayState &st   = states[i];
        if (rule.canId == 0) continue;
        if (rule.isExtended != isExt || rule.canId != id) continue;
        if (f.can_dlc <= rule.byteIndex) continue;

        st.lastMatchMs   = now;
        st.watchdogArmed = true;

        bool bitSet = (f.data[rule.byteIndex] & rule.bitMask) != 0;

        if (rule.mode == PULSE) {
            // PULSE: narastające zbocze (0→1) startuje impuls
            if (bitSet && !st.inPulse) {
                digitalWrite(relayPins[i], HIGH);
                st.current      = true;
                st.inPulse      = true;
                st.pulseEndMs   = now + rule.pulseMs;
                st.onSinceMs    = now;
                st.lastChangeMs = now;
                sendStatusFrame();
            }
        } else {
            // LATCH: śledź bit na bieżąco
            setRelayGpio(i, bitSet);
        }
    }
}

// ──────────────────────────────────────────────────────────────
// Watchdog — relay OFF gdy brak pasującej ramki przez watchdogMs
// ──────────────────────────────────────────────────────────────
static void checkWatchdogs() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        if (!rules[i].watchdogMs || !states[i].watchdogArmed) continue;
        if (states[i].current && (now - states[i].lastMatchMs) > rules[i].watchdogMs)
            setRelayGpio(i, false, true);  // force OFF — bezpieczeństwo
    }
}

// ──────────────────────────────────────────────────────────────
// Pulse — automatyczne wyłączenie po upływie pulseMs
// ──────────────────────────────────────────────────────────────
static void checkPulses() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        RelayState &st = states[i];
        if (!st.inPulse || now < st.pulseEndMs) continue;
        st.inPulse      = false;
        digitalWrite(relayPins[i], LOW);
        st.current      = false;
        st.lastChangeMs = now;
        sendStatusFrame();
    }
}

// ──────────────────────────────────────────────────────────────
// SLCAN helpers
// ──────────────────────────────────────────────────────────────
static void slOK()  { Serial.write('\r'); }
static void slERR() { Serial.write('\a'); }

static int8_t hexNibble(char c) {
    if (c>='0'&&c<='9') return c-'0';
    if (c>='A'&&c<='F') return c-'A'+10;
    if (c>='a'&&c<='f') return c-'a'+10;
    return -1;
}
static bool parseHex(const char *s, uint8_t digits, uint32_t &out) {
    out = 0;
    for (uint8_t i = 0; i < digits; i++) {
        int8_t n = hexNibble(s[i]);
        if (n < 0) return false;
        out = (out << 4) | (uint32_t)n;
    }
    return true;
}

static void sendSlcanFrame(const can_frame &f) {
    bool     ext = f.can_id & CAN_EFF_FLAG;
    uint32_t id  = f.can_id & (ext ? CAN_EFF_MASK : CAN_SFF_MASK);
    uint8_t  dlc = f.can_dlc & 0x0F;
    char buf[12];
    if (ext) snprintf(buf,sizeof(buf),"T%08lX%u",(unsigned long)id,dlc);
    else     snprintf(buf,sizeof(buf),"t%03lX%u", (unsigned long)id,dlc);
    Serial.print(buf);
    for (uint8_t i = 0; i < dlc; i++) {
        snprintf(buf,sizeof(buf),"%02X",f.data[i]);
        Serial.print(buf);
    }
    Serial.write('\r');
}

static void applySpeed(CAN_SPEED speed, bool lo) {
    mcp.reset();
    mcp.setFilterMask(MCP2515::MASK0, true, 0x00000000);
    mcp.setFilterMask(MCP2515::MASK1, true, 0x00000000);
    mcp.setBitrate(speed);
    if (lo) mcp.setListenOnlyMode();
    else    mcp.setNormalMode();
}

// ──────────────────────────────────────────────────────────────
// Wydruk konfiguracji
// ──────────────────────────────────────────────────────────────
static void printConfig() {
    Serial.println("=== RELAY CONFIGURATION v2 ===");
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        RelayRule &r = rules[i];
        Serial.print("Relay "); Serial.print(i);
        Serial.print(" GPIO="); Serial.print(relayPins[i]);
        if (r.canId == 0) { Serial.println(" [DISABLED]"); continue; }
        Serial.print(r.isExtended ? " EXT" : " STD");
        Serial.print(" ID=0x"); Serial.print(r.canId, HEX);
        Serial.print(" byte="); Serial.print(r.byteIndex);
        Serial.print(" mask=0x"); Serial.print(r.bitMask, HEX);
        Serial.print(" wdog="); Serial.print(r.watchdogMs); Serial.print("ms");
        Serial.print(" dbnc="); Serial.print(r.debounceMs); Serial.print("ms");
        Serial.print(" mode="); Serial.print(r.mode == PULSE ? "PULSE" : "LATCH");
        if (r.mode == PULSE) { Serial.print(" pulse="); Serial.print(r.pulseMs); Serial.print("ms"); }
        Serial.print(" minOn="); Serial.print(r.minOnMs); Serial.print("ms");
        Serial.print(" boot="); Serial.print(r.bootState == BOOT_ON ? "ON" : "OFF");
        Serial.print(" state="); Serial.println(states[i].current ? "ON" : "OFF");
    }
    Serial.print("Status frame : ");
    if (!statusCfg.canId) { Serial.println("OFF"); }
    else { Serial.print(statusCfg.isExtended?"EXT ":"STD "); Serial.print("0x"); Serial.println(statusCfg.canId,HEX); }
    Serial.print("Heartbeat    : ");
    if (!hbCfg.canId) { Serial.println("OFF"); }
    else { Serial.print(hbCfg.isExtended?"EXT ":"STD "); Serial.print("0x"); Serial.print(hbCfg.canId,HEX); Serial.print(" every "); Serial.print(hbCfg.intervalMs); Serial.println("ms"); }
    Serial.println("==============================");
}

// ──────────────────────────────────────────────────────────────
// Parser komend serial
// ──────────────────────────────────────────────────────────────
static void processCommand(char *cmd) {
    uint8_t len = (uint8_t)strlen(cmd);
    if (!len) return;

    // SETRELAY <id> <canId_hex> <EXT|STD> <byte> <mask_hex> [wdog] [dbnc]
    if (strncasecmp(cmd,"SETRELAY",8)==0) {
        char *p=cmd+8; while(*p==' ')p++;
        char *idS=strtok(p," "),*ciS=strtok(nullptr," "),*exS=strtok(nullptr," ");
        char *byS=strtok(nullptr," "),*mkS=strtok(nullptr," ");
        char *wdS=strtok(nullptr," "),*dbS=strtok(nullptr," ");
        if(!idS||!ciS||!exS||!byS||!mkS){Serial.println("ERR SETRELAY <id> <canId> <EXT|STD> <byte> <mask> [wdog] [dbnc]");return;}
        uint8_t rid=(uint8_t)atoi(idS);
        if(rid>=RELAY_COUNT){Serial.println("ERR relay 0-5");return;}
        uint32_t cid; if(!parseHex(ciS,(uint8_t)strlen(ciS),cid)){Serial.println("ERR canId");return;}
        bool ext=strcasecmp(exS,"EXT")==0;
        uint8_t bi=(uint8_t)atoi(byS);
        uint32_t mk; if(!parseHex(mkS,(uint8_t)strlen(mkS),mk)){Serial.println("ERR mask");return;}
        uint32_t wdog=wdS?(uint32_t)atol(wdS):rules[rid].watchdogMs;
        uint32_t dbnc=dbS?(uint32_t)atol(dbS):rules[rid].debounceMs;
        rules[rid].canId=cid; rules[rid].isExtended=ext; rules[rid].byteIndex=bi;
        rules[rid].bitMask=(uint8_t)mk; rules[rid].watchdogMs=wdog; rules[rid].debounceMs=dbnc;
        states[rid].watchdogArmed=false;
        Serial.print("OK SETRELAY "); Serial.print(rid);
        Serial.print(" 0x"); Serial.print(cid,HEX); Serial.print(ext?" EXT":" STD");
        Serial.print(" byte="); Serial.print(bi); Serial.print(" mask=0x"); Serial.print((uint8_t)mk,HEX);
        Serial.print(" wdog="); Serial.print(wdog); Serial.print("ms dbnc="); Serial.print(dbnc); Serial.println("ms");
        return;
    }

    // SETMODE <id> LATCH | PULSE <ms>
    if (strncasecmp(cmd,"SETMODE",7)==0) {
        char *p=cmd+7; while(*p==' ')p++;
        char *idS=strtok(p," "),*mS=strtok(nullptr," "),*msS=strtok(nullptr," ");
        if(!idS||!mS){Serial.println("ERR SETMODE <id> LATCH | PULSE <ms>");return;}
        uint8_t rid=(uint8_t)atoi(idS);
        if(rid>=RELAY_COUNT){Serial.println("ERR relay 0-5");return;}
        if(strcasecmp(mS,"LATCH")==0){
            rules[rid].mode=LATCH;
            Serial.print("OK SETMODE "); Serial.print(rid); Serial.println(" LATCH");
        } else if(strcasecmp(mS,"PULSE")==0){
            uint32_t ms=msS?(uint32_t)atol(msS):rules[rid].pulseMs;
            if(!ms){Serial.println("ERR PULSE requires <ms>");return;}
            rules[rid].mode=PULSE; rules[rid].pulseMs=ms;
            Serial.print("OK SETMODE "); Serial.print(rid); Serial.print(" PULSE "); Serial.print(ms); Serial.println("ms");
        } else {Serial.println("ERR mode: LATCH or PULSE"); return;}
        return;
    }

    // SETMINON <id> <ms>
    if (strncasecmp(cmd,"SETMINON",8)==0) {
        char *p=cmd+8; while(*p==' ')p++;
        char *idS=strtok(p," "),*msS=strtok(nullptr," ");
        if(!idS||!msS){Serial.println("ERR SETMINON <id> <ms>");return;}
        uint8_t rid=(uint8_t)atoi(idS);
        if(rid>=RELAY_COUNT){Serial.println("ERR relay 0-5");return;}
        rules[rid].minOnMs=(uint32_t)atol(msS);
        Serial.print("OK SETMINON "); Serial.print(rid); Serial.print(" "); Serial.print(rules[rid].minOnMs); Serial.println("ms");
        return;
    }

    // SETBOOT <id> ON | OFF
    if (strncasecmp(cmd,"SETBOOT",7)==0) {
        char *p=cmd+7; while(*p==' ')p++;
        char *idS=strtok(p," "),*stS=strtok(nullptr," ");
        if(!idS||!stS){Serial.println("ERR SETBOOT <id> ON|OFF");return;}
        uint8_t rid=(uint8_t)atoi(idS);
        if(rid>=RELAY_COUNT){Serial.println("ERR relay 0-5");return;}
        bool on=strcasecmp(stS,"ON")==0;
        rules[rid].bootState=on?BOOT_ON:BOOT_OFF;
        Serial.print("OK SETBOOT "); Serial.print(rid); Serial.println(on?" ON":" OFF");
        return;
    }

    // SETSTATUS <canId_hex> <EXT|STD>  |  SETSTATUS OFF
    if (strncasecmp(cmd,"SETSTATUS",9)==0) {
        char *p=cmd+9; while(*p==' ')p++;
        char *idS=strtok(p," ");
        if(!idS){Serial.println("ERR SETSTATUS <canId> <EXT|STD> | OFF");return;}
        if(strcasecmp(idS,"OFF")==0){statusCfg.canId=0;Serial.println("OK SETSTATUS OFF");return;}
        char *exS=strtok(nullptr," ");
        uint32_t cid; if(!parseHex(idS,(uint8_t)strlen(idS),cid)){Serial.println("ERR canId");return;}
        statusCfg={cid,exS?strcasecmp(exS,"EXT")==0:false};
        Serial.print("OK SETSTATUS 0x"); Serial.print(cid,HEX); Serial.println(statusCfg.isExtended?" EXT":" STD");
        return;
    }

    // SETHEARTBEAT <canId_hex> <EXT|STD> <intervalMs>  |  SETHEARTBEAT OFF
    if (strncasecmp(cmd,"SETHEARTBEAT",12)==0) {
        char *p=cmd+12; while(*p==' ')p++;
        char *idS=strtok(p," ");
        if(!idS){Serial.println("ERR SETHEARTBEAT <canId> <EXT|STD> <ms> | OFF");return;}
        if(strcasecmp(idS,"OFF")==0){hbCfg.canId=0;Serial.println("OK SETHEARTBEAT OFF");return;}
        char *exS=strtok(nullptr," "),*msS=strtok(nullptr," ");
        uint32_t cid; if(!parseHex(idS,(uint8_t)strlen(idS),cid)){Serial.println("ERR canId");return;}
        bool ext=exS?strcasecmp(exS,"EXT")==0:false;
        uint32_t ms=msS?(uint32_t)atol(msS):hbCfg.intervalMs;
        if(!ms){Serial.println("ERR interval > 0");return;}
        hbCfg={cid,ext,ms};
        Serial.print("OK SETHEARTBEAT 0x"); Serial.print(cid,HEX);
        Serial.print(ext?" EXT":" STD"); Serial.print(" every "); Serial.print(ms); Serial.println("ms");
        return;
    }

    // SAVECONFIG
    if (strncasecmp(cmd,"SAVECONFIG",10)==0) {
        Serial.println(saveConfig()?"OK SAVECONFIG":"ERR NVS write failed");
        return;
    }

    // SHOWCONFIG
    if (strncasecmp(cmd,"SHOWCONFIG",10)==0) { printConfig(); return; }

    // RELAY <id> <0|1|ON|OFF>
    if (strncasecmp(cmd,"RELAY",5)==0) {
        char *p=cmd+5; while(*p==' ')p++;
        char *idS=strtok(p," "),*stS=strtok(nullptr," ");
        if(!idS||!stS){Serial.println("ERR RELAY <id> <0|1|ON|OFF>");return;}
        uint8_t rid=(uint8_t)atoi(idS);
        if(rid>=RELAY_COUNT){Serial.println("ERR relay 0-5");return;}
        bool on=strcmp(stS,"1")==0||strcasecmp(stS,"ON")==0;
        // force=true: ręczne sterowanie, natychmiast bez debounce/minOn
        digitalWrite(relayPins[rid], on?HIGH:LOW);
        if(on) states[rid].onSinceMs=millis();
        states[rid].current=on; states[rid].lastChangeMs=millis();
        states[rid].inPulse=false;
        sendStatusFrame();
        Serial.print("OK RELAY "); Serial.print(rid); Serial.println(on?" ON":" OFF");
        return;
    }

    // STATUS
    if (strncasecmp(cmd,"STATUS",6)==0) {
        Serial.print("STATUS relays: ");
        for(uint8_t i=0;i<RELAY_COUNT;i++) Serial.print(states[i].current?"1":"0");
        Serial.println();
        return;
    }

    // ── SLCAN single-char commands ──────────────────────────────
    char c=cmd[0];
    if(c=='S'&&len==2){
        const CAN_SPEED t[]={CAN_10KBPS,CAN_20KBPS,CAN_50KBPS,CAN_100KBPS,CAN_125KBPS,CAN_250KBPS,CAN_500KBPS,CAN_500KBPS,CAN_1000KBPS};
        uint8_t idx=(uint8_t)(cmd[1]-'0'); if(idx>8){slERR();return;} currentSpeed=t[idx]; slOK(); return;
    }
    if(c=='O'&&len==1){listenOnly=false;channelOpen=true;applySpeed(currentSpeed,false);slOK();return;}
    if(c=='L'&&len==1){listenOnly=true; channelOpen=true;applySpeed(currentSpeed,true); slOK();return;}
    if(c=='C'&&len==1){channelOpen=false;slOK();return;}
    if(c=='V'&&len==1){Serial.print("V1010\r");return;}
    if(c=='N'&&len==1){Serial.print("NA123\r");return;}
    if(c=='F'&&len==1){Serial.print("F00\r");  return;}
    if(c=='Z'&&len==2){slOK();return;}

    if(c=='t'&&len>=5){
        uint32_t id; if(!parseHex(cmd+1,3,id)){slERR();return;}
        uint8_t dlc=(uint8_t)(cmd[4]-'0');
        if(dlc>8||len<(uint8_t)(5+dlc*2)){slERR();return;}
        txMsg.can_id=id&CAN_SFF_MASK; txMsg.can_dlc=dlc;
        for(uint8_t i=0;i<dlc;i++){uint32_t b;if(!parseHex(cmd+5+i*2,2,b)){slERR();return;}txMsg.data[i]=(uint8_t)b;}
        if(!listenOnly&&mcp.sendMessage(&txMsg)==MCP2515::ERROR_OK){applyRelayLogic(txMsg);slOK();}else slERR();
        return;
    }
    if(c=='T'&&len>=10){
        uint32_t id; if(!parseHex(cmd+1,8,id)){slERR();return;}
        uint8_t dlc=(uint8_t)(cmd[9]-'0');
        if(dlc>8||len<(uint8_t)(10+dlc*2)){slERR();return;}
        txMsg.can_id=(id&CAN_EFF_MASK)|CAN_EFF_FLAG; txMsg.can_dlc=dlc;
        for(uint8_t i=0;i<dlc;i++){uint32_t b;if(!parseHex(cmd+10+i*2,2,b)){slERR();return;}txMsg.data[i]=(uint8_t)b;}
        if(!listenOnly&&mcp.sendMessage(&txMsg)==MCP2515::ERROR_OK){applyRelayLogic(txMsg);slOK();}else slERR();
        return;
    }

    slERR();
}

// ──────────────────────────────────────────────────────────────
// Setup
// ──────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);

    // GPIO — wszystkie OFF
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        pinMode(relayPins[i], OUTPUT);
        digitalWrite(relayPins[i], LOW);
        states[i] = {false, false, 0, 0, false, 0, 0};
    }

    // Wczytaj konfigurację z NVS
    loadConfig();

    // Zastosuj stan startowy
    for (uint8_t i = 0; i < RELAY_COUNT; i++) {
        if (rules[i].bootState == BOOT_ON && rules[i].canId != 0) {
            digitalWrite(relayPins[i], HIGH);
            states[i].current   = true;
            states[i].onSinceMs = millis();
        }
    }

    // MCP2515
    SPI.begin();
    while (mcp.reset() != MCP2515::ERROR_OK) delay(500);
    mcp.setFilterMask(MCP2515::MASK0, true, 0x00000000);
    mcp.setFilterMask(MCP2515::MASK1, true, 0x00000000);
    mcp.setBitrate(CAN_250KBPS);
    mcp.setNormalMode();

    lastHbMs = millis();
}

// ──────────────────────────────────────────────────────────────
// Main loop
// ──────────────────────────────────────────────────────────────
void loop() {
    // CAN RX — relay logic zawsze; SLCAN forward tylko gdy kanał otwarty
    if (mcp.readMessage(&rxMsg) == MCP2515::ERROR_OK) {
        applyRelayLogic(rxMsg);
        if (channelOpen) sendSlcanFrame(rxMsg);
    }

    checkWatchdogs();   // watchdog bezpieczeństwa
    checkPulses();      // auto-OFF po impulsie
    // checkHeartbeat();   // disabled — device operates anonymously (enable with SETHEARTBEAT)

    // Serial → command parser
    while (Serial.available()) {
        char ch = (char)Serial.read();
        if (ch == '\r' || ch == '\n') {
            if (serialIdx > 0) {
                serialBuf[serialIdx] = '\0';
                processCommand(serialBuf);
                serialIdx = 0;
            }
        } else if (serialIdx < SERIAL_BUF_SIZE - 1) {
            serialBuf[serialIdx++] = ch;
        }
    }
}
