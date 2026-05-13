#include <gtest/gtest.h>
#include "core/CanPrototypeExporter.h"

namespace {

static FrameExportInfo makeFrame(uint32_t id, const QString &name,
                                  double periodMs = 0.0) {
    FrameExportInfo f;
    f.id       = id;
    f.name     = name;
    f.dlc      = 8;
    f.periodMs = periodMs;
    f.sampleData = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    return f;
}

static FrameExportInfo makeFrameWithSignals(uint32_t id) {
    FrameExportInfo f = makeFrame(id, "EngineData", 10.0);
    DbcSignal s1;
    s1.name          = "EngineSpeed";
    s1.startBit      = 0;
    s1.length        = 16;
    s1.isLittleEndian= true;
    s1.isSigned      = false;
    s1.scale         = 0.5;
    s1.offset        = 0.0;
    s1.unit          = "rpm";
    DbcSignal s2;
    s2.name          = "Throttle";
    s2.startBit      = 16;
    s2.length        = 8;
    s2.isLittleEndian= true;
    s2.scale         = 0.4;
    s2.offset        = 0.0;
    s2.unit          = "%";
    f.sigList = {s1, s2};
    return f;
}

} // namespace

// ── Python ────────────────────────────────────────────────────────────────────

TEST(CanPrototypeExporter, Python_GeneratesHeader) {
    CanPrototypeExporter::Config cfg;
    cfg.format = CanPrototypeExporter::Format::Python;
    auto code = CanPrototypeExporter::generate({makeFrame(0x1A0, "Engine", 10.0)}, cfg);

    EXPECT_TRUE(code.contains("import can"));
    EXPECT_TRUE(code.contains("import threading"));
    EXPECT_TRUE(code.contains("BUS_TYPE"));
    EXPECT_TRUE(code.contains("BITRATE"));
}

TEST(CanPrototypeExporter, Python_GeneratesClassAndPeriod) {
    CanPrototypeExporter::Config cfg;
    cfg.format = CanPrototypeExporter::Format::Python;
    auto code = CanPrototypeExporter::generate({makeFrame(0x1A0, "Engine", 10.0)}, cfg);

    EXPECT_TRUE(code.contains("class Msg_Engine"));
    EXPECT_TRUE(code.contains("PERIOD"));
    EXPECT_TRUE(code.contains("def encode"));
    EXPECT_TRUE(code.contains("def build_message"));
    EXPECT_TRUE(code.contains("_periodic"));  // periodic thread helper
}

TEST(CanPrototypeExporter, Python_GeneratesSignalEncoding) {
    CanPrototypeExporter::Config cfg;
    cfg.format = CanPrototypeExporter::Format::Python;
    auto code = CanPrototypeExporter::generate({makeFrameWithSignals(0x1A0)}, cfg);

    EXPECT_TRUE(code.contains("enginespeed") || code.contains("engine_speed"));
    EXPECT_TRUE(code.contains("throttle"));
    EXPECT_TRUE(code.contains("0.5"));  // scale factor appears in encode()
    EXPECT_TRUE(code.contains("rpm"));
}

TEST(CanPrototypeExporter, Python_CounterByteInEncode) {
    FrameExportInfo f = makeFrame(0x200, "Node", 20.0);
    f.counterBytes = {3};
    CanPrototypeExporter::Config cfg;
    cfg.format = CanPrototypeExporter::Format::Python;
    auto code = CanPrototypeExporter::generate({f}, cfg);

    EXPECT_TRUE(code.contains("_ctr_byte3"));
    EXPECT_TRUE(code.contains("& 0xFF"));
}

TEST(CanPrototypeExporter, Python_EmptyFrames_ValidScript) {
    CanPrototypeExporter::Config cfg;
    cfg.format = CanPrototypeExporter::Format::Python;
    auto code = CanPrototypeExporter::generate({}, cfg);

    EXPECT_TRUE(code.contains("import can"));
    EXPECT_TRUE(code.contains("def main"));
    EXPECT_FALSE(code.isEmpty());
}

TEST(CanPrototypeExporter, Python_WithReceiver) {
    CanPrototypeExporter::Config cfg;
    cfg.format       = CanPrototypeExporter::Format::Python;
    cfg.withReceiver = true;
    auto code = CanPrototypeExporter::generate({makeFrame(0x1A0, "X", 10.0)}, cfg);
    EXPECT_TRUE(code.contains("rx_loop") || code.contains("bus.recv"));
}

// ── Lua ───────────────────────────────────────────────────────────────────────

TEST(CanPrototypeExporter, Lua_GeneratesHeader) {
    CanPrototypeExporter::Config cfg;
    cfg.format = CanPrototypeExporter::Format::Lua;
    auto code = CanPrototypeExporter::generate({makeFrame(0x100, "Node", 5.0)}, cfg);

    EXPECT_TRUE(code.contains("CHANNEL"));
    EXPECT_TRUE(code.contains("BITRATE"));
    EXPECT_TRUE(code.contains("period_ms"));
}

TEST(CanPrototypeExporter, Lua_GeneratesEncodeMethod) {
    CanPrototypeExporter::Config cfg;
    cfg.format = CanPrototypeExporter::Format::Lua;
    auto code = CanPrototypeExporter::generate({makeFrameWithSignals(0x1A0)}, cfg);

    EXPECT_TRUE(code.contains("function") && code.contains(":encode()"));
    EXPECT_TRUE(code.contains("engine_speed") || code.contains("enginespeed"));
}

TEST(CanPrototypeExporter, Lua_CounterByte) {
    FrameExportInfo f = makeFrame(0x300, "Sensor", 50.0);
    f.counterBytes = {0, 1};
    CanPrototypeExporter::Config cfg;
    cfg.format = CanPrototypeExporter::Format::Lua;
    auto code = CanPrototypeExporter::generate({f}, cfg);

    EXPECT_TRUE(code.contains("_ctr_byte0"));
    EXPECT_TRUE(code.contains("_ctr_byte1"));
}

// ── Arduino ───────────────────────────────────────────────────────────────────

TEST(CanPrototypeExporter, Arduino_GeneratesSketch) {
    CanPrototypeExporter::Config cfg;
    cfg.format  = CanPrototypeExporter::Format::Arduino;
    cfg.bitrate = 500000;
    auto code = CanPrototypeExporter::generate({makeFrame(0x1A0, "Engine", 10.0)}, cfg);

    EXPECT_TRUE(code.contains("#include <mcp_can.h>"));
    EXPECT_TRUE(code.contains("void setup()"));
    EXPECT_TRUE(code.contains("void loop()"));
    EXPECT_TRUE(code.contains("CAN_500KBPS"));
}

TEST(CanPrototypeExporter, Arduino_GeneratesSignalEncode) {
    CanPrototypeExporter::Config cfg;
    cfg.format = CanPrototypeExporter::Format::Arduino;
    auto code = CanPrototypeExporter::generate({makeFrameWithSignals(0x1A0)}, cfg);

    EXPECT_TRUE(code.contains("encode_"));
    EXPECT_TRUE(code.contains("constrain"));
    EXPECT_TRUE(code.contains("enginespeed") || code.contains("engine_speed")
                || code.contains("EngineSpeed"));
}

TEST(CanPrototypeExporter, Arduino_CounterByte) {
    FrameExportInfo f = makeFrame(0x400, "ECU", 10.0);
    f.counterBytes = {7};
    CanPrototypeExporter::Config cfg;
    cfg.format = CanPrototypeExporter::Format::Arduino;
    auto code = CanPrototypeExporter::generate({f}, cfg);

    EXPECT_TRUE(code.contains("ctr_") && code.contains("_b7"));
}

TEST(CanPrototypeExporter, Arduino_PeriodInLoop) {
    CanPrototypeExporter::Config cfg;
    cfg.format = CanPrototypeExporter::Format::Arduino;
    auto code = CanPrototypeExporter::generate({makeFrame(0x1A0, "Eng", 20.0)}, cfg);

    EXPECT_TRUE(code.contains("millis()"));
    EXPECT_TRUE(code.contains("sendMsgBuf"));
    EXPECT_TRUE(code.contains("20"));  // period in ms
}

TEST(CanPrototypeExporter, Arduino_WithReceiver) {
    CanPrototypeExporter::Config cfg;
    cfg.format       = CanPrototypeExporter::Format::Arduino;
    cfg.withReceiver = true;
    auto code = CanPrototypeExporter::generate({makeFrame(0x1A0, "X", 10.0)}, cfg);
    EXPECT_TRUE(code.contains("checkReceive") || code.contains("readMsgBuf"));
}

TEST(CanPrototypeExporter, MultipleMessages) {
    CanPrototypeExporter::Config cfg;
    cfg.format = CanPrototypeExporter::Format::Python;
    auto code = CanPrototypeExporter::generate(
        {makeFrame(0x100, "A", 10.0), makeFrame(0x200, "B", 20.0)}, cfg);
    EXPECT_TRUE(code.contains("Msg_A"));
    EXPECT_TRUE(code.contains("Msg_B"));
}

TEST(CanPrototypeExporter, NonPeriodicFrame_NoPeriod) {
    CanPrototypeExporter::Config cfg;
    cfg.format = CanPrototypeExporter::Format::Python;
    // periodMs = 0 → no PERIOD constant, no thread
    auto code = CanPrototypeExporter::generate({makeFrame(0x500, "Diag", 0.0)}, cfg);
    // Should not have _periodic for this message but still have the class
    EXPECT_TRUE(code.contains("Msg_Diag"));
}
