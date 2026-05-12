#include <gtest/gtest.h>
#include "core/CanSignalMonitor.h"
#include "core/DbcParser.h"

// Helper: build a minimal DbcParser in memory
static DbcParser makeDbcWith(uint32_t id, const QString &msgName, DbcSignal sig) {
    DbcMessage msg;
    msg.id   = id;
    msg.name = msgName;
    msg.dlc  = 8;
    msg.sigList.append(sig);
    DbcParser p;
    p.setMessages({msg});
    return p;
}

static CanFrame makeFrame(uint32_t id, std::initializer_list<uint8_t> data, uint64_t ts = 0) {
    CanFrame f;
    f.id        = id;
    f.dlc       = static_cast<uint8_t>(data.size());
    f.timestamp = ts;
    int i = 0;
    for (uint8_t b : data) f.data[i++] = b;
    return f;
}

// ── Basic signal tracking ────────────────────────────────────────────────────

TEST(CanSignalMonitor, NoDbc) {
    CanSignalMonitor mon;
    // No DBC set — update should be a no-op
    mon.update(makeFrame(0x100, {0x01}));
    EXPECT_EQ(mon.allValues().size(), 0u);
}

TEST(CanSignalMonitor, UnknownIdIgnored) {
    DbcSignal sig;
    sig.name          = "Speed";
    sig.startBit      = 0;
    sig.length        = 8;
    sig.isLittleEndian= true;
    sig.isSigned      = false;
    sig.scale         = 1.0;
    sig.offset        = 0.0;
    sig.unit          = "km/h";

    DbcParser dbc = makeDbcWith(0x100, "VehicleSpeed", sig);
    CanSignalMonitor mon;
    mon.setDbc(&dbc);
    mon.update(makeFrame(0x200, {0x50})); // wrong ID
    EXPECT_EQ(mon.allValues().size(), 0u);
}

TEST(CanSignalMonitor, SingleSignalDecoded) {
    DbcSignal sig;
    sig.name           = "Throttle";
    sig.startBit       = 0;
    sig.length         = 8;
    sig.isLittleEndian = true;
    sig.isSigned       = false;
    sig.scale          = 0.5;
    sig.offset         = 0.0;
    sig.unit           = "%";

    DbcParser dbc = makeDbcWith(0x300, "Engine", sig);
    CanSignalMonitor mon;
    mon.setDbc(&dbc);
    mon.update(makeFrame(0x300, {0x64, 0,0,0,0,0,0,0}, 1000)); // 0x64=100, phys=50.0

    ASSERT_EQ(mon.allValues().size(), 1u);
    auto &sv = mon.allValues()[0];
    EXPECT_EQ(sv.signalName, "Throttle");
    EXPECT_NEAR(sv.physValue, 50.0, 0.01);
    EXPECT_EQ(sv.unit, "%");
    EXPECT_EQ(sv.updateCount, 1u);
    EXPECT_EQ(sv.lastUpdateUs, 1000u);
}

TEST(CanSignalMonitor, UpdateCount) {
    DbcSignal sig;
    sig.name="RPM"; sig.startBit=0; sig.length=8;
    sig.isLittleEndian=true; sig.isSigned=false;
    sig.scale=10.0; sig.offset=0.0;

    DbcParser dbc = makeDbcWith(0x400, "Engine", sig);
    CanSignalMonitor mon;
    mon.setDbc(&dbc);
    mon.update(makeFrame(0x400, {0x0A}));
    mon.update(makeFrame(0x400, {0x14}));
    mon.update(makeFrame(0x400, {0x1E}));

    ASSERT_EQ(mon.allValues().size(), 1u);
    EXPECT_EQ(mon.allValues()[0].updateCount, 3u);
}

TEST(CanSignalMonitor, LatestValueKept) {
    DbcSignal sig;
    sig.name="Temp"; sig.startBit=0; sig.length=8;
    sig.isLittleEndian=true; sig.isSigned=false;
    sig.scale=1.0; sig.offset=-40.0; // typical temperature encoding

    DbcParser dbc = makeDbcWith(0x500, "Temps", sig);
    CanSignalMonitor mon;
    mon.setDbc(&dbc);
    mon.update(makeFrame(0x500, {0x58})); // 88 - 40 = 48°C
    mon.update(makeFrame(0x500, {0x6E})); // 110 - 40 = 70°C

    EXPECT_NEAR(mon.allValues()[0].physValue, 70.0, 0.01);
}

// ── valueFor ─────────────────────────────────────────────────────────────────

TEST(CanSignalMonitor, ValueForFound) {
    DbcSignal sig;
    sig.name="BrakeP"; sig.startBit=0; sig.length=8;
    sig.isLittleEndian=true; sig.isSigned=false;
    sig.scale=0.1; sig.offset=0.0; sig.unit="bar";

    DbcParser dbc = makeDbcWith(0x600, "Brakes", sig);
    CanSignalMonitor mon; mon.setDbc(&dbc);
    mon.update(makeFrame(0x600, {0x32})); // 50 * 0.1 = 5.0 bar

    const auto *sv = mon.valueFor("BrakeP");
    ASSERT_NE(sv, nullptr);
    EXPECT_NEAR(sv->physValue, 5.0, 0.01);
}

TEST(CanSignalMonitor, ValueForNotFound) {
    CanSignalMonitor mon;
    EXPECT_EQ(mon.valueFor("NoSuchSignal"), nullptr);
}

// ── Alarm thresholds ──────────────────────────────────────────────────────────

TEST(CanSignalMonitor, AlarmNotActive) {
    SignalValue sv;
    sv.physValue = 50.0;
    sv.alarmMin  = 0.0;
    sv.alarmMax  = 100.0;
    EXPECT_FALSE(sv.alarmActive());
}

TEST(CanSignalMonitor, AlarmActiveLow) {
    SignalValue sv;
    sv.physValue = -5.0;
    sv.alarmMin  = 0.0;
    sv.alarmMax  = 100.0;
    EXPECT_TRUE(sv.alarmActive());
}

TEST(CanSignalMonitor, AlarmActiveHigh) {
    SignalValue sv;
    sv.physValue = 150.0;
    sv.alarmMin  = 0.0;
    sv.alarmMax  = 100.0;
    EXPECT_TRUE(sv.alarmActive());
}

TEST(CanSignalMonitor, AlarmNoThreshold) {
    SignalValue sv;
    sv.physValue = 9999.0; // extreme value
    // No alarm thresholds set → NaN
    EXPECT_FALSE(sv.alarmActive());
}

// ── Stale signals ─────────────────────────────────────────────────────────────

TEST(CanSignalMonitor, StaleDetection) {
    DbcSignal sig;
    sig.name="Stale"; sig.startBit=0; sig.length=8;
    sig.isLittleEndian=true; sig.isSigned=false; sig.scale=1.0; sig.offset=0.0;

    DbcParser dbc = makeDbcWith(0x700, "Test", sig);
    CanSignalMonitor mon; mon.setDbc(&dbc);
    mon.update(makeFrame(0x700, {0x01}, /*ts=*/1000)); // updated at 1000 µs

    uint64_t nowUs = 10000; // 9000 µs since update
    auto stale = mon.staleSignals(nowUs, 5000); // threshold = 5000 µs
    EXPECT_EQ(stale.size(), 1u);

    auto fresh = mon.staleSignals(nowUs, 20000); // threshold = 20000 µs (not stale)
    EXPECT_EQ(fresh.size(), 0u);
}

// ── Reset ─────────────────────────────────────────────────────────────────────

TEST(CanSignalMonitor, Reset) {
    DbcSignal sig;
    sig.name="X"; sig.startBit=0; sig.length=8;
    sig.isLittleEndian=true; sig.isSigned=false; sig.scale=1.0; sig.offset=0.0;

    DbcParser dbc = makeDbcWith(0x800, "Test", sig);
    CanSignalMonitor mon; mon.setDbc(&dbc);
    mon.update(makeFrame(0x800, {0x01}));
    EXPECT_EQ(mon.allValues().size(), 1u);

    mon.reset();
    EXPECT_EQ(mon.allValues().size(), 0u);
}
