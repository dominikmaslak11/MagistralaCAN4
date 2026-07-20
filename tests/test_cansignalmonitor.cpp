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
    auto sv = mon.allValues()[0];
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

TEST(CanSignalMonitor, ValueForAfterReset_Null) {
    DbcSignal sig;
    sig.name="Sig"; sig.startBit=0; sig.length=8;
    sig.isLittleEndian=true; sig.isSigned=false; sig.scale=1.0; sig.offset=0.0;

    DbcParser dbc = makeDbcWith(0x100, "Msg", sig);
    CanSignalMonitor mon; mon.setDbc(&dbc);
    mon.update(makeFrame(0x100, {0x05}));
    EXPECT_NE(mon.valueFor("Sig"), nullptr);

    mon.reset();
    EXPECT_EQ(mon.valueFor("Sig"), nullptr);
}

TEST(CanSignalMonitor, SetAlarmPreservesOnUpdate) {
    DbcSignal sig;
    sig.name="Speed"; sig.startBit=0; sig.length=8;
    sig.isLittleEndian=true; sig.isSigned=false; sig.scale=1.0; sig.offset=0.0;

    DbcParser dbc = makeDbcWith(0x200, "Veh", sig);
    CanSignalMonitor mon; mon.setDbc(&dbc);

    // Set alarm before any update — value will exceed max
    mon.setAlarm("Speed", 0.0, 100.0);

    // Update with value 150 → above max
    mon.update(makeFrame(0x200, {150}));
    const auto *sv = mon.valueFor("Speed");
    ASSERT_NE(sv, nullptr);
    EXPECT_NEAR(sv->physValue, 150.0, 0.01);
    EXPECT_TRUE(sv->alarmActive()); // alarm should trigger
}

TEST(CanSignalMonitor, ActiveAlarmCountZeroInitially) {
    CanSignalMonitor mon;
    EXPECT_EQ(mon.activeAlarmCount(), 0);
}

TEST(CanSignalMonitor, ActiveAlarmCountMultipleViolations) {
    auto makeSig = [](const QString &name) {
        DbcSignal s; s.name=name; s.startBit=0; s.length=8;
        s.isLittleEndian=true; s.isSigned=false; s.scale=1.0; s.offset=0.0;
        return s;
    };

    DbcMessage msg; msg.id=0x300; msg.name="M"; msg.dlc=8;
    msg.sigList.append(makeSig("A"));
    msg.sigList.append(makeSig("B"));
    DbcParser dbc; dbc.setMessages({msg});

    CanSignalMonitor mon; mon.setDbc(&dbc);
    // A at bit0, B also at bit0 (they overlap — both decode from same byte)
    mon.update(makeFrame(0x300, {200, 0,0,0,0,0,0,0})); // raw=200

    mon.setAlarm("A", 0.0, 100.0); // 200 > 100 → alarm
    mon.setAlarm("B", 0.0, 100.0); // 200 > 100 → alarm

    EXPECT_EQ(mon.activeAlarmCount(), 2);
}

TEST(CanSignalMonitor, AlarmBoundaryExactNotActive) {
    // Strict inequality: exactly at boundary → not active
    SignalValue sv;
    sv.physValue = 100.0;
    sv.alarmMin  = 0.0;
    sv.alarmMax  = 100.0;
    EXPECT_FALSE(sv.alarmActive()); // physValue == alarmMax → not active

    sv.physValue = 0.0;
    EXPECT_FALSE(sv.alarmActive()); // physValue == alarmMin → not active
}

TEST(CanSignalMonitor, AllValuesSortedByName) {
    auto makeSig = [](const QString &name, int startBit) {
        DbcSignal s; s.name=name; s.startBit=startBit; s.length=8;
        s.isLittleEndian=true; s.isSigned=false; s.scale=1.0; s.offset=0.0;
        return s;
    };

    DbcMessage msg; msg.id=0x400; msg.name="M"; msg.dlc=8;
    msg.sigList.append(makeSig("Zebra", 16));
    msg.sigList.append(makeSig("Apple", 8));
    msg.sigList.append(makeSig("Mango", 0));
    DbcParser dbc; dbc.setMessages({msg});

    CanSignalMonitor mon; mon.setDbc(&dbc);
    mon.update(makeFrame(0x400, {1,2,3,0,0,0,0,0}));

    auto vals = mon.allValues();
    ASSERT_EQ(vals.size(), 3u);
    EXPECT_LT(vals[0].signalName, vals[1].signalName);
    EXPECT_LT(vals[1].signalName, vals[2].signalName);
}
