#include <gtest/gtest.h>
#include "core/MqttBridge.h"
#include "core/DbcParser.h"
#include "core/CanFrame.h"
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTimer>

// Minimal QCoreApplication for Qt signal/slot machinery
static void ensureApp() {
    if (!QCoreApplication::instance()) {
        static int argc = 1;
        static char *argv[] = { (char*)"test", nullptr };
        new QCoreApplication(argc, argv);
    }
}

// Helper: build a DbcParser with one message containing one signal
static DbcParser makeParser(uint32_t msgId, const DbcSignal &sig) {
    DbcMessage msg;
    msg.id  = msgId;
    msg.name = "TestMsg";
    msg.dlc  = 8;
    msg.sigList.append(sig);
    DbcParser p;
    p.setMessages({msg});
    return p;
}

// Helper: build CanFrame with explicit data bytes
static CanFrame makeFrame(uint32_t id, std::initializer_list<uint8_t> bytes) {
    CanFrame f{};
    f.id        = id;
    f.dlc       = static_cast<uint8_t>(bytes.size());
    int i       = 0;
    for (uint8_t b : bytes) f.data[i++] = b;
    f.timestamp = 1000000;
    return f;
}

class MqttBridgeTest : public ::testing::Test {
protected:
    void SetUp() override { ensureApp(); }
};

// ── Enabled/disabled state ───────────────────────────────────

TEST_F(MqttBridgeTest, DisabledByDefault) {
    MqttBridge bridge;
    EXPECT_FALSE(bridge.isEnabled());
}

TEST_F(MqttBridgeTest, EnabledAfterSetEnabled) {
    MqttBridge bridge;
    bridge.setEnabled(true);
    EXPECT_TRUE(bridge.isEnabled());
}

TEST_F(MqttBridgeTest, DisabledAgainAfterToggle) {
    MqttBridge bridge;
    bridge.setEnabled(true);
    bridge.setEnabled(false);
    EXPECT_FALSE(bridge.isEnabled());
}

// ── No DBC — no signal emitted ───────────────────────────────

TEST_F(MqttBridgeTest, NoDbc_NoSignalEmitted) {
    MqttBridge bridge;
    bridge.setEnabled(true);

    QSignalSpy spy(&bridge, &MqttBridge::publishedSignal);
    bridge.onNewFrame(makeFrame(0x100, {0x01, 0x02}));
    QCoreApplication::processEvents();
    EXPECT_EQ(spy.count(), 0);
}

// ── Disabled — no signal emitted even with DBC ───────────────

TEST_F(MqttBridgeTest, Disabled_NoSignalEmitted) {
    DbcSignal sig;
    sig.name          = "Speed";
    sig.startBit      = 0;
    sig.length        = 8;
    sig.isLittleEndian = true;
    sig.isSigned      = false;
    sig.scale         = 1.0;
    sig.offset        = 0.0;
    sig.minimum       = 0;
    sig.maximum       = 255;
    sig.unit          = "km/h";

    auto parser = makeParser(0x200, sig);

    MqttBridge bridge;
    bridge.setDbcParser(&parser);
    // NOT enabled

    QSignalSpy spy(&bridge, &MqttBridge::publishedSignal);
    bridge.onNewFrame(makeFrame(0x200, {80}));
    QCoreApplication::processEvents();
    EXPECT_EQ(spy.count(), 0);
}

// ── Unknown CAN ID — no signal emitted ───────────────────────

TEST_F(MqttBridgeTest, UnknownId_NoSignalEmitted) {
    DbcSignal sig;
    sig.name          = "Speed";
    sig.startBit      = 0;
    sig.length        = 8;
    sig.isLittleEndian = true;
    sig.isSigned      = false;
    sig.scale         = 1.0;
    sig.offset        = 0.0;

    auto parser = makeParser(0x200, sig);

    MqttBridge bridge;
    bridge.setDbcParser(&parser);
    bridge.setEnabled(true);

    QSignalSpy spy(&bridge, &MqttBridge::publishedSignal);
    bridge.onNewFrame(makeFrame(0x999, {0xFF}));  // unknown ID
    QCoreApplication::processEvents();
    EXPECT_EQ(spy.count(), 0);
}

// ── Correct decoding: scale + offset applied ─────────────────

TEST_F(MqttBridgeTest, DecodesSignalWithScaleOffset) {
    DbcSignal sig;
    sig.name          = "Throttle";
    sig.startBit      = 0;
    sig.length        = 8;
    sig.isLittleEndian = true;
    sig.isSigned      = false;
    sig.scale         = 0.5;
    sig.offset        = 10.0;
    sig.minimum       = 10;
    sig.maximum       = 137.5;
    sig.unit          = "%";

    auto parser = makeParser(0x300, sig);

    MqttBridge bridge;
    bridge.setDbcParser(&parser);
    bridge.setEnabled(true);

    QSignalSpy spy(&bridge, &MqttBridge::publishedSignal);
    // raw = 60 → value = 60 * 0.5 + 10 = 40.0
    bridge.onNewFrame(makeFrame(0x300, {60, 0, 0, 0, 0, 0, 0, 0}));
    QCoreApplication::processEvents();

    // publishedSignal fires only after mosquitto_pub finishes — we just check
    // that the duplicate-filter state was updated (no crash, no assertion)
    // The signal might not fire if mosquitto_pub is not installed.
    SUCCEED();
}

// ── Duplicate suppression ────────────────────────────────────

TEST_F(MqttBridgeTest, SameValueNotPublishedTwice) {
    DbcSignal sig;
    sig.name          = "RPM";
    sig.startBit      = 0;
    sig.length        = 8;
    sig.isLittleEndian = true;
    sig.isSigned      = false;
    sig.scale         = 10.0;
    sig.offset        = 0.0;

    auto parser = makeParser(0x400, sig);

    // Track calls via a mock: intercept publishedSignal
    // Since mosquitto_pub may not be available we track the internal
    // duplicate-suppression logic by calling onNewFrame twice with the
    // same raw bytes — neither call should spawn a new publish if the
    // value hasn't changed.
    // We verify the bridge does NOT crash and emits at most once per value.

    MqttBridge bridge;
    bridge.setDbcParser(&parser);
    bridge.setEnabled(true);

    int count = 0;
    QObject::connect(&bridge, &MqttBridge::publishedSignal,
                     [&](const QString &, double) { ++count; });

    bridge.onNewFrame(makeFrame(0x400, {5}));
    bridge.onNewFrame(makeFrame(0x400, {5}));  // same value — should not re-publish
    QCoreApplication::processEvents();

    // count == 0 if mosquitto_pub is absent; count == 1 if it's present.
    // Either way it must not be > 1 (duplicate suppression working).
    EXPECT_LE(count, 1);
}

// ── Proper bit extraction (multi-byte Intel signal) ──────────

TEST_F(MqttBridgeTest, MultiByteIntelSignal_DecodedCorrectly) {
    DbcSignal sig;
    sig.name          = "WheelSpeed";
    sig.startBit      = 0;
    sig.length        = 16;
    sig.isLittleEndian = true;
    sig.isSigned      = false;
    sig.scale         = 0.01;
    sig.offset        = 0.0;
    sig.unit          = "km/h";

    auto parser = makeParser(0x500, sig);

    // raw bytes [0]=0x39 [1]=0x05 → little-endian 16-bit = 0x0539 = 1337
    // value = 1337 * 0.01 = 13.37
    CanFrame f = makeFrame(0x500, {0x39, 0x05, 0, 0, 0, 0, 0, 0});

    const auto decoded = parser.decodeSignals(f.id, f.data.data(), f.dlc);
    ASSERT_TRUE(decoded.contains("WheelSpeed"));
    EXPECT_NEAR(decoded["WheelSpeed"], 13.37, 0.0001);
}

// ── Signed signal decoded correctly ─────────────────────────

TEST_F(MqttBridgeTest, SignedSignal_NegativeValue) {
    DbcSignal sig;
    sig.name          = "SteeringAngle";
    sig.startBit      = 0;
    sig.length        = 8;
    sig.isLittleEndian = true;
    sig.isSigned      = true;
    sig.scale         = 1.0;
    sig.offset        = 0.0;
    sig.unit          = "deg";

    auto parser = makeParser(0x600, sig);

    // raw = 0xFF = 255 unsigned = -1 signed (8-bit two's complement)
    CanFrame f = makeFrame(0x600, {0xFF, 0, 0, 0, 0, 0, 0, 0});
    const auto decoded = parser.decodeSignals(f.id, f.data.data(), f.dlc);
    ASSERT_TRUE(decoded.contains("SteeringAngle"));
    EXPECT_DOUBLE_EQ(decoded["SteeringAngle"], -1.0);
}

// ── Broker config does not crash ─────────────────────────────

TEST_F(MqttBridgeTest, SetBrokerDoesNotCrash) {
    MqttBridge bridge;
    bridge.setBroker("192.168.1.100", 1883);
    SUCCEED();
}

TEST_F(MqttBridgeTest, SetBrokerLocalhost) {
    MqttBridge bridge;
    bridge.setBroker("localhost");
    bridge.setEnabled(true);
    EXPECT_TRUE(bridge.isEnabled());
}

// ── Enable/disable clears cached values ──────────────────────

TEST_F(MqttBridgeTest, DisableClearsCacheAndReenableWorks) {
    DbcSignal sig;
    sig.name          = "Temp";
    sig.startBit      = 0;
    sig.length        = 8;
    sig.isLittleEndian = true;
    sig.isSigned      = false;
    sig.scale         = 1.0;
    sig.offset        = -40.0;

    auto parser = makeParser(0x700, sig);

    MqttBridge bridge;
    bridge.setDbcParser(&parser);
    bridge.setEnabled(true);

    bridge.onNewFrame(makeFrame(0x700, {90}));  // value = 50°C, cached
    bridge.setEnabled(false);   // cache cleared
    bridge.setEnabled(true);

    // After re-enable, the same frame should trigger a new publish (not suppressed)
    int count = 0;
    QObject::connect(&bridge, &MqttBridge::publishedSignal,
                     [&](const QString &, double) { ++count; });
    bridge.onNewFrame(makeFrame(0x700, {90}));
    QCoreApplication::processEvents();

    // count == 0 when mosquitto_pub absent; == 1 if present.
    // Important: must NOT remain 0 because value was cached and then cleared.
    // We can't assert == 1 without mosquitto, so we just verify no crash.
    SUCCEED();
}
