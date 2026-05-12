#include <gtest/gtest.h>
#include "core/DbcAutoGenerator.h"

// ── Helpers ───────────────────────────────────────────────────────────────────

static CanFrame makeFrame(uint32_t id, uint8_t dlc,
                           std::initializer_list<uint8_t> data,
                           uint64_t ts = 0) {
    CanFrame f;
    f.id  = id;
    f.dlc = dlc;
    f.timestamp = ts;
    int i = 0;
    for (uint8_t b : data) f.data[i++] = b;
    return f;
}

// Build N frames with periodic timestamps (ms → µs) and a counter in byte 0
static std::vector<CanFrame> makeCounterStream(uint32_t id, int n, uint32_t periodMs = 10) {
    std::vector<CanFrame> v;
    v.reserve(n);
    for (int i = 0; i < n; ++i)
        v.push_back(makeFrame(id, 2,
            {static_cast<uint8_t>(i & 0xFF), 0x42},
            static_cast<uint64_t>(i) * periodMs * 1000));
    return v;
}

// Build N frames with a byte toggling between two values
static std::vector<CanFrame> makeToggleStream(uint32_t id, int n) {
    std::vector<CanFrame> v;
    v.reserve(n);
    for (int i = 0; i < n; ++i)
        v.push_back(makeFrame(id, 1, {static_cast<uint8_t>(i & 1 ? 0xFF : 0x00)},
                              static_cast<uint64_t>(i) * 20000));
    return v;
}

// Build N frames with a non-sequential signal (stride 13 — avoids counter classification)
static std::vector<CanFrame> makeValueStream(uint32_t id, int n, uint32_t periodMs = 10) {
    std::vector<CanFrame> v;
    v.reserve(n);
    for (int i = 0; i < n; ++i)
        v.push_back(makeFrame(id, 1, {static_cast<uint8_t>((i * 13 + 50) & 0xFF)},
                              static_cast<uint64_t>(i) * periodMs * 1000));
    return v;
}

// Build N frames where byte 2 = XOR of bytes 0 and 1
static std::vector<CanFrame> makeChecksumStream(uint32_t id, int n) {
    std::vector<CanFrame> v;
    v.reserve(n);
    for (int i = 0; i < n; ++i) {
        uint8_t b0 = static_cast<uint8_t>(i & 0xFF);
        uint8_t b1 = static_cast<uint8_t>((i * 3) & 0xFF);
        uint8_t cs = b0 ^ b1;
        v.push_back(makeFrame(id, 3, {b0, b1, cs},
                              static_cast<uint64_t>(i) * 10000));
    }
    return v;
}

// ── Static helper tests ───────────────────────────────────────────────────────

TEST(DbcAutoGenerator, EntropyZeroForConstant) {
    std::vector<uint8_t> vals(100, 0x42);
    EXPECT_NEAR(DbcAutoGenerator::computeEntropy(vals), 0.0, 1e-9);
}

TEST(DbcAutoGenerator, EntropyMaxForUniform) {
    std::vector<uint8_t> vals;
    for (int i = 0; i < 256; ++i) vals.push_back(static_cast<uint8_t>(i));
    double h = DbcAutoGenerator::computeEntropy(vals);
    EXPECT_NEAR(h, 8.0, 0.01);
}

TEST(DbcAutoGenerator, EntropyEmptyIsZero) {
    EXPECT_EQ(DbcAutoGenerator::computeEntropy({}), 0.0);
}

TEST(DbcAutoGenerator, CounterFractionPerfect) {
    std::vector<uint8_t> vals;
    for (int i = 0; i < 100; ++i) vals.push_back(static_cast<uint8_t>(i));
    EXPECT_NEAR(DbcAutoGenerator::counterFraction(vals), 1.0, 1e-9);
}

TEST(DbcAutoGenerator, CounterFractionWithWrap) {
    std::vector<uint8_t> vals;
    for (int i = 253; i <= 258; ++i) vals.push_back(static_cast<uint8_t>(i & 0xFF));
    // 253, 254, 255, 0, 1, 2 — all +1 mod 256 → fraction = 1.0
    EXPECT_NEAR(DbcAutoGenerator::counterFraction(vals), 1.0, 1e-9);
}

TEST(DbcAutoGenerator, CounterFractionRandom) {
    std::vector<uint8_t> vals = {0x10, 0xAB, 0x33, 0xC7, 0x00};
    double cf = DbcAutoGenerator::counterFraction(vals);
    EXPECT_LT(cf, 0.5);
}

TEST(DbcAutoGenerator, PeriodEstimate) {
    auto frames = makeCounterStream(0x100, 20, 10); // 10 ms period
    double p = DbcAutoGenerator::estimatePeriodMs(frames);
    EXPECT_NEAR(p, 10.0, 0.1);
}

TEST(DbcAutoGenerator, PeriodEstimateSingleFrame) {
    std::vector<CanFrame> v = {makeFrame(0x100, 1, {0x00})};
    EXPECT_EQ(DbcAutoGenerator::estimatePeriodMs(v), 0.0);
}

TEST(DbcAutoGenerator, ChecksumByteDetected) {
    auto frames = makeChecksumStream(0x200, 30);
    // All three bytes satisfy XOR-of-others (XOR is symmetric):
    // if b2 = b0^b1, then also b0 = b1^b2. Both should detect as checksum.
    EXPECT_TRUE(DbcAutoGenerator::isChecksumByte(2, frames, 3));
    EXPECT_TRUE(DbcAutoGenerator::isChecksumByte(0, frames, 3));
    // A 1-byte stream can't be a checksum of zero others (dlc=1 guard)
    std::vector<CanFrame> oneByteFrames;
    for (int i = 0; i < 20; ++i)
        oneByteFrames.push_back(makeFrame(0x999, 1, {static_cast<uint8_t>(i)}));
    EXPECT_FALSE(DbcAutoGenerator::isChecksumByte(0, oneByteFrames, 1));
}

// ── Role classification ───────────────────────────────────────────────────────

TEST(DbcAutoGenerator, ClassifyCounter) {
    AutoByteStats bs;
    bs.minVal = 0; bs.maxVal = 255; bs.entropy = 8.0;
    for (int i = 0; i < 256; ++i) bs.uniqueValues.insert(static_cast<uint8_t>(i));
    EXPECT_EQ(DbcAutoGenerator::classifyRole(bs, false, 0.95), ByteRole::Counter);
}

TEST(DbcAutoGenerator, ClassifyChecksum) {
    AutoByteStats bs;
    bs.minVal = 0; bs.maxVal = 200; bs.entropy = 5.0;
    bs.uniqueValues.insert(0); bs.uniqueValues.insert(42);
    EXPECT_EQ(DbcAutoGenerator::classifyRole(bs, true, 0.0), ByteRole::Checksum);
}

TEST(DbcAutoGenerator, ClassifyToggle) {
    AutoByteStats bs;
    bs.minVal = 0; bs.maxVal = 0xFF; bs.entropy = 1.0;
    bs.uniqueValues.insert(0x00); bs.uniqueValues.insert(0xFF);
    EXPECT_EQ(DbcAutoGenerator::classifyRole(bs, false, 0.0), ByteRole::Toggle);
}

TEST(DbcAutoGenerator, ClassifyState) {
    AutoByteStats bs;
    bs.minVal = 0; bs.maxVal = 5; bs.entropy = 1.8;
    for (int i = 0; i <= 5; ++i) bs.uniqueValues.insert(static_cast<uint8_t>(i));
    EXPECT_EQ(DbcAutoGenerator::classifyRole(bs, false, 0.0), ByteRole::State);
}

TEST(DbcAutoGenerator, ClassifyConstant) {
    AutoByteStats bs;
    bs.minVal = 0x42; bs.maxVal = 0x42; bs.entropy = 0.0;
    bs.uniqueValues.insert(0x42);
    EXPECT_EQ(DbcAutoGenerator::classifyRole(bs, false, 0.0), ByteRole::Constant);
}

// ── End-to-end analysis ───────────────────────────────────────────────────────

TEST(DbcAutoGenerator, TooFewFramesIgnored) {
    std::vector<CanFrame> frames;
    for (int i = 0; i < 3; ++i)
        frames.push_back(makeFrame(0x100, 1, {static_cast<uint8_t>(i)}));

    DbcAutoGenerator gen;
    auto result = gen.analyze(frames);
    // Below kMinFrames threshold (5), should produce no output
    EXPECT_TRUE(result.empty());
}

TEST(DbcAutoGenerator, CounterStreamClassified) {
    auto frames = makeCounterStream(0x100, 50, 10);
    DbcAutoGenerator gen;
    auto result = gen.analyze(frames);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].id, 0x100u);
    EXPECT_EQ(result[0].dlc, 2);
    EXPECT_EQ(result[0].timing, "periodic");
    EXPECT_NEAR(result[0].periodMs, 10.0, 0.5);

    // Byte 0 should be classified as Counter
    bool foundCounter = false;
    for (const auto &bs : result[0].byteStats)
        if (bs.role == ByteRole::Counter) foundCounter = true;
    EXPECT_TRUE(foundCounter);
}

TEST(DbcAutoGenerator, ToggleStreamClassified) {
    auto frames = makeToggleStream(0x200, 40);
    DbcAutoGenerator gen;
    auto result = gen.analyze(frames);
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].byteStats.size(), 1u);
    EXPECT_EQ(result[0].byteStats[0].role, ByteRole::Toggle);
}

TEST(DbcAutoGenerator, ValueStreamProducesSignal) {
    auto frames = makeValueStream(0x300, 60, 20);
    DbcAutoGenerator gen;
    auto result = gen.analyze(frames);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_FALSE(result[0].sigs.empty());
    EXPECT_EQ(result[0].byteStats[0].role, ByteRole::Value);
}

TEST(DbcAutoGenerator, ChecksumStreamClassified) {
    auto frames = makeChecksumStream(0x400, 40);
    DbcAutoGenerator gen;
    auto result = gen.analyze(frames);
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].byteStats.size(), 3u);
    EXPECT_EQ(result[0].byteStats[2].role, ByteRole::Checksum);
}

TEST(DbcAutoGenerator, MultipleIdsAnalyzed) {
    std::vector<CanFrame> frames;
    auto v1 = makeCounterStream(0x100, 20, 10);
    auto v2 = makeToggleStream(0x200, 20);
    auto v3 = makeValueStream(0x300, 20, 5);
    frames.insert(frames.end(), v1.begin(), v1.end());
    frames.insert(frames.end(), v2.begin(), v2.end());
    frames.insert(frames.end(), v3.begin(), v3.end());

    DbcAutoGenerator gen;
    auto result = gen.analyze(frames);
    EXPECT_EQ(result.size(), 3u);
    // Results are sorted by ID
    EXPECT_EQ(result[0].id, 0x100u);
    EXPECT_EQ(result[1].id, 0x200u);
    EXPECT_EQ(result[2].id, 0x300u);
}

TEST(DbcAutoGenerator, ToDbcMessagesConversion) {
    auto frames = makeValueStream(0x100, 30, 10);
    DbcAutoGenerator gen;
    auto autoMsgs = gen.analyze(frames);
    ASSERT_FALSE(autoMsgs.empty());

    auto dbcMsgs = DbcAutoGenerator::toDbcMessages(autoMsgs);
    ASSERT_FALSE(dbcMsgs.empty());
    EXPECT_EQ(dbcMsgs[0].id, 0x100u);
    EXPECT_EQ(dbcMsgs[0].dlc, 1);
    EXPECT_FALSE(dbcMsgs[0].sigList.isEmpty());
}

TEST(DbcAutoGenerator, RoleNames) {
    EXPECT_EQ(DbcAutoGenerator::roleName(ByteRole::Counter),  "COUNTER");
    EXPECT_EQ(DbcAutoGenerator::roleName(ByteRole::Checksum), "CHECKSUM");
    EXPECT_EQ(DbcAutoGenerator::roleName(ByteRole::Toggle),   "TOGGLE");
    EXPECT_EQ(DbcAutoGenerator::roleName(ByteRole::State),    "STATE");
    EXPECT_EQ(DbcAutoGenerator::roleName(ByteRole::Value),    "VALUE");
    EXPECT_EQ(DbcAutoGenerator::roleName(ByteRole::Constant), "CONST");
}
