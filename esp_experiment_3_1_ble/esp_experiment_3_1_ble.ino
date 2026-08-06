// esp_experiment_3_1_ble.ino — Eksperyment 3.1: Wplyw odleglosci i przeszkod
// na opoznienia i stratnosc pakietow (wariant BLE), wg
// "Pomiary dla CAN-Edge AI.md" (Grupa 3). Analogiczny protokol do
// esp_experiment_3_1_wifi.ino, ale przez BLE GATT zamiast UDP/SoftAP.
//
// ESP32 dziala jako peryferyjne urzadzenie BLE (GATT server), reklamujac sie
// pod nazwa BLE_DEVICE_NAME. Telefon laczy sie, wlacza notyfikacje na
// charakterystyce ping-pong, po czym zapisuje do niej "PING:<seq>"
// (write-with-response). ESP32 natychmiast w callbacku onWrite odsyla
// "PONG:<seq>" przez notify na TEJ SAMEJ charakterystyce. Telefon liczy RTT
// na WLASNYM zegarze (write() -> onCharacteristicChanged), tak samo jak w
// wariancie WiFi - bez potrzeby synchronizacji zegarow.

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

static const char* BLE_DEVICE_NAME  = "MagistralaCAN4_Exp31_BLE";
static const char* SERVICE_UUID     = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static const char* CHAR_PINGPONG_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";

static BLECharacteristic* g_pingPongChar = nullptr;
static bool     g_deviceConnected = false;
static uint32_t g_pingCount    = 0;
static uint32_t g_lastReportMs = 0;

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* server) override {
        g_deviceConnected = true;
        Serial.println("[BLE] Klient polaczony");
    }
    void onDisconnect(BLEServer* server) override {
        g_deviceConnected = false;
        Serial.println("[BLE] Klient rozlaczony, wznawiam advertising");
        // Bez tego ESP32 przestaje byc widoczne po pierwszym rozlaczeniu.
        delay(200);
        server->getAdvertising()->start();
    }
};

class PingPongCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        String value = characteristic->getValue();
        if (value.startsWith("PING:")) {
            String resp = "PONG:" + value.substring(5);
            characteristic->setValue((uint8_t*)resp.c_str(), resp.length());
            characteristic->notify();
            g_pingCount++;
        }
    }
};

void setup() {
    Serial.begin(115200);
    delay(200);

    BLEDevice::init(BLE_DEVICE_NAME);
    BLEServer* server = BLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    BLEService* service = server->createService(SERVICE_UUID);
    g_pingPongChar = service->createCharacteristic(
        CHAR_PINGPONG_UUID,
        BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
    );
    g_pingPongChar->addDescriptor(new BLE2902());
    g_pingPongChar->setCallbacks(new PingPongCallbacks());
    service->start();

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(SERVICE_UUID);
    advertising->setScanResponse(true);
    // Wartosci zalecane w dokumentacji, zmniejszaja szanse na problemy z
    // polaczeniem z niektorymi telefonami (m.in. starszymi Androidami).
    advertising->setMinPreferred(0x06);
    advertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    Serial.print("[SETUP] BLE advertising uruchomiony, nazwa=");
    Serial.println(BLE_DEVICE_NAME);
}

void loop() {
    if (millis() - g_lastReportMs >= 5000) {
        g_lastReportMs = millis();
        Serial.print("[REPORT] pingCount=");
        Serial.print(g_pingCount);
        Serial.print(" connected=");
        Serial.println(g_deviceConnected ? "yes" : "no");
    }
    delay(10);
}
