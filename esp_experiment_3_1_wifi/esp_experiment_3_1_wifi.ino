// esp_experiment_3_1_wifi.ino — Eksperyment 3.1: Wplyw odleglosci i przeszkod
// na opoznienia i stratnosc pakietow (wariant WiFi), wg
// "Pomiary dla CAN-Edge AI.md" (Grupa 3).
//
// ESP32 pracuje jako wlasny punkt dostepowy (SoftAP) - telefon laczy sie
// bezposrednio do niego, bez posredniczacego routera. Mierzymy w ten sposob
// surowy zasieg radia ESP32 (patrz
// Pytania_Do_Wykladowcy_Eksperyment_3.1_20260729.md, pkt 1, dla omowienia tej
// decyzji z wykladowca).
//
// Protokol: telefon wysyla przez UDP tekstowy pakiet "PING:<seq>", ESP32
// natychmiast odsyla echo "PONG:<seq>" do tego samego adresu/portu nadawcy.
// Telefon liczy RTT na WLASNYM zegarze (czas wyslania -> czas odebrania
// PONG), wiec nie ma potrzeby synchronizacji zegarow obu urzadzen. Brakujace
// numery sekwencyjne (po uplywie timeoutu po stronie telefonu) = pakiety
// utracone.
//
// Polacz telefon z siecia AP_SSID/AP_PASSWORD ponizej PRZED startem testu
// w aplikacji Android.

#include <WiFi.h>
#include <WiFiUdp.h>
#include <string.h>

static const char*    AP_SSID     = "MagistralaCAN4_Exp31";
static const char*    AP_PASSWORD = "can-edge-ai";   // min. 8 znakow (wymog WPA2)
static const uint16_t UDP_PORT    = 3131;

static WiFiUDP g_udp;
static char    g_packetBuf[128];

static uint32_t g_pingCount     = 0;
static uint32_t g_lastReportMs  = 0;

void setup() {
    Serial.begin(115200);
    delay(200);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);

    Serial.print("[SETUP] SoftAP uruchomiony, SSID=");
    Serial.print(AP_SSID);
    Serial.print(" IP=");
    Serial.println(WiFi.softAPIP());

    g_udp.begin(UDP_PORT);
    Serial.print("[SETUP] Nasluch UDP na porcie ");
    Serial.println(UDP_PORT);
}

void loop() {
    int packetSize = g_udp.parsePacket();
    if (packetSize > 0) {
        int len = g_udp.read(g_packetBuf, sizeof(g_packetBuf) - 1);
        if (len > 0) {
            g_packetBuf[len] = '\0';

            if (strncmp(g_packetBuf, "PING:", 5) == 0) {
                g_udp.beginPacket(g_udp.remoteIP(), g_udp.remotePort());
                g_udp.print("PONG:");
                g_udp.print(g_packetBuf + 5);
                g_udp.endPacket();

                g_pingCount++;
            }
        }
    }

    if (millis() - g_lastReportMs >= 5000) {
        g_lastReportMs = millis();
        Serial.print("[REPORT] pingCount=");
        Serial.print(g_pingCount);
        Serial.print(" stacje=");
        Serial.println(WiFi.softAPgetStationNum());
    }
}
