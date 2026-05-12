#include <gtest/gtest.h>
#include "core/CanIdStats.h"

static CanFrame makeFrame(uint32_t id, uint64_t tsUs, std::initializer_list<uint8_t> data, bool ext = false) {
    CanFrame f;
    f.id        = id;
    f.extended  = ext;
    f.timestamp = tsUs;
    f.dlc       = static_cast<uint8_t>(data.size());
    int i = 0;
    for (uint8_t b : data) f.data[i++] = b;
    return f;
}

// ── Basic accumulation ────────────────────────────────────────────────────────

TEST(CanIdStats, SingleFrameCount) {
    CanIdStats s;
    s.feed(makeFrame(0x100, 0, {0x01, 0x02}));
    const auto *p = s.profileFor(0x100);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->frameCount, 1u);
}

TEST(CanIdStats, MultipleFramesSameId) {
    CanIdStats s;
    s.feed(makeFrame(0x200, 0, {0x10}));
    s.feed(makeFrame(0x200, 1000, {0x20}));
    s.feed(makeFrame(0x200, 2000, {0x30}));
    const auto *p = s.profileFor(0x200);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->frameCount, 3u);
}

TEST(CanIdStats, TwoDistinctIds) {
    CanIdStats s;
    s.feed(makeFrame(0x001, 0, {0xFF}));
    s.feed(makeFrame(0x002, 0, {0x00}));
    EXPECT_EQ(s.uniqueIdCount(), 2u);
    EXPECT_EQ(s.totalFrameCount(), 2u);
}

// ── Byte statistics ───────────────────────────────────────────────────────────

TEST(CanIdStats, ByteMinMax) {
    CanIdStats s;
    s.feed(makeFrame(0x10, 0,    {0x10, 0xFF}));
    s.feed(makeFrame(0x10, 1000, {0x80, 0x00}));
    s.feed(makeFrame(0x10, 2000, {0x50, 0x80}));
    const auto *p = s.profileFor(0x10);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->bytes[0].minVal, 0x10u);
    EXPECT_EQ(p->bytes[0].maxVal, 0x80u);
    EXPECT_EQ(p->bytes[1].minVal, 0x00u);
    EXPECT_EQ(p->bytes[1].maxVal, 0xFFu);
}

TEST(CanIdStats, ByteAvg) {
    CanIdStats s;
    s.feed(makeFrame(0x20, 0,    {0x00}));
    s.feed(makeFrame(0x20, 1000, {0x60}));
    s.feed(makeFrame(0x20, 2000, {0x60}));
    const auto *p = s.profileFor(0x20);
    ASSERT_NE(p, nullptr);
    // avg = (0 + 96 + 96) / 3 = 64 = 0x40
    EXPECT_NEAR(p->byteAvg(0), 64.0, 0.01);
}

TEST(CanIdStats, MaxDlcTracked) {
    CanIdStats s;
    s.feed(makeFrame(0x30, 0, {0x01, 0x02}));          // dlc=2
    s.feed(makeFrame(0x30, 1000, {0x01, 0x02, 0x03, 0x04})); // dlc=4
    const auto *p = s.profileFor(0x30);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->maxDlc, 4u);
}

// ── Interval statistics ───────────────────────────────────────────────────────

TEST(CanIdStats, IntervalMinMaxAvg) {
    CanIdStats s;
    // intervals: 1000, 3000 µs → min=1000, max=3000, avg=2000
    s.feed(makeFrame(0x40, 0,    {0x00}));
    s.feed(makeFrame(0x40, 1000, {0x00}));
    s.feed(makeFrame(0x40, 4000, {0x00}));
    const auto *p = s.profileFor(0x40);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->intervalMin, 1000u);
    EXPECT_EQ(p->intervalMax, 3000u);
    EXPECT_NEAR(p->avgIntervalUs(), 2000.0, 0.01);
    EXPECT_NEAR(p->avgIntervalMs(), 2.0,    0.001);
}

TEST(CanIdStats, SingleFrameNoInterval) {
    CanIdStats s;
    s.feed(makeFrame(0x50, 0, {0x00}));
    const auto *p = s.profileFor(0x50);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->avgIntervalUs(), 0.0);
    EXPECT_EQ(p->freqHz(), 0.0);
}

TEST(CanIdStats, FreqHz) {
    CanIdStats s;
    // Two frames 10000 µs apart → interval = 10ms → freq = 100 Hz
    s.feed(makeFrame(0x60, 0,     {0x00}));
    s.feed(makeFrame(0x60, 10000, {0x00}));
    const auto *p = s.profileFor(0x60);
    ASSERT_NE(p, nullptr);
    EXPECT_NEAR(p->freqHz(), 100.0, 0.1);
}

// ── Timestamps ───────────────────────────────────────────────────────────────

TEST(CanIdStats, FirstLastTimestamp) {
    CanIdStats s;
    s.feed(makeFrame(0x70, 500,  {0x00}));
    s.feed(makeFrame(0x70, 1500, {0x00}));
    s.feed(makeFrame(0x70, 3000, {0x00}));
    const auto *p = s.profileFor(0x70);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->firstTs, 500u);
    EXPECT_EQ(p->lastTs,  3000u);
}

// ── Reset ────────────────────────────────────────────────────────────────────

TEST(CanIdStats, Reset) {
    CanIdStats s;
    s.feed(makeFrame(0x80, 0, {0x00}));
    s.feed(makeFrame(0x81, 0, {0x00}));
    s.reset();
    EXPECT_EQ(s.uniqueIdCount(), 0u);
    EXPECT_EQ(s.totalFrameCount(), 0u);
    EXPECT_EQ(s.profileFor(0x80), nullptr);
}

// ── Profiles sorting ──────────────────────────────────────────────────────────

TEST(CanIdStats, ProfilesSortedByCount) {
    CanIdStats s;
    s.feed(makeFrame(0x01, 0, {0x00}));
    s.feed(makeFrame(0x02, 0, {0x00}));
    s.feed(makeFrame(0x02, 1000, {0x00}));
    s.feed(makeFrame(0x03, 0, {0x00}));
    s.feed(makeFrame(0x03, 1000, {0x00}));
    s.feed(makeFrame(0x03, 2000, {0x00}));
    auto ps = s.profiles();
    ASSERT_EQ(ps.size(), 3u);
    EXPECT_EQ(ps[0].id, 0x03u); // 3 frames
    EXPECT_EQ(ps[1].id, 0x02u); // 2 frames
    EXPECT_EQ(ps[2].id, 0x01u); // 1 frame
}

// ── CSV export ────────────────────────────────────────────────────────────────

TEST(CanIdStats, CsvHeaderPresent) {
    CanIdStats s;
    s.feed(makeFrame(0x1FF, 0, {0xAB}));
    std::string csv = s.toCsv();
    EXPECT_NE(csv.find("id,extended,frames"), std::string::npos);
    EXPECT_NE(csv.find("avg_interval_ms"), std::string::npos);
    EXPECT_NE(csv.find("b0_min"), std::string::npos);
}

TEST(CanIdStats, CsvContainsId) {
    CanIdStats s;
    s.feed(makeFrame(0xABC, 0, {0x01}));
    std::string csv = s.toCsv();
    EXPECT_NE(csv.find("0xABC"), std::string::npos);
}

TEST(CanIdStats, CsvByteValues) {
    CanIdStats s;
    s.feed(makeFrame(0x100, 0,    {0x10}));
    s.feed(makeFrame(0x100, 1000, {0x20}));
    std::string csv = s.toCsv();
    // b0_min=16, b0_max=32, b0_avg=24
    EXPECT_NE(csv.find("16"), std::string::npos);
    EXPECT_NE(csv.find("32"), std::string::npos);
}

TEST(CanIdStats, ExtendedFlagInCsv) {
    CanIdStats s;
    s.feed(makeFrame(0x18FEEE00, 0, {0x01}, true));
    std::string csv = s.toCsv();
    EXPECT_NE(csv.find(",1,"), std::string::npos); // extended=1
}

TEST(CanIdStats, UnknownIdReturnsNull) {
    CanIdStats s;
    EXPECT_EQ(s.profileFor(0xDEAD), nullptr);
}
