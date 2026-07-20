#include <gtest/gtest.h>
#include "core/CanByteHeatmapModel.h"
#include "core/CanFrame.h"

static CanFrame makeFrame(uint32_t id, uint8_t dlc, uint8_t byteVal) {
    CanFrame f{};
    f.id  = id;
    f.dlc = dlc;
    for (int i = 0; i < dlc && i < 8; ++i)
        f.data[i] = byteVal;
    return f;
}

TEST(CanByteHeatmapModel, EmptyInitialState) {
    CanByteHeatmapModel m;
    EXPECT_EQ(m.totalIds(), 0);
    EXPECT_TRUE(m.trackedIds().empty());
    EXPECT_EQ(m.latestTimestamp(), 0u);
}

TEST(CanByteHeatmapModel, AddFrameTracksId) {
    CanByteHeatmapModel m;
    m.addFrame(makeFrame(0x100, 4, 0xAA), 1000);
    ASSERT_EQ(m.totalIds(), 1);
    EXPECT_EQ(m.trackedIds()[0], 0x100u);
    EXPECT_EQ(m.frameCount(0x100), 1);
    EXPECT_EQ(m.latestTimestamp(), 1000u);
}

TEST(CanByteHeatmapModel, MultipleIds) {
    CanByteHeatmapModel m;
    m.addFrame(makeFrame(0x100, 4, 0x00), 1000);
    m.addFrame(makeFrame(0x200, 4, 0x00), 2000);
    m.addFrame(makeFrame(0x300, 4, 0x00), 3000);
    EXPECT_EQ(m.totalIds(), 3);
    EXPECT_EQ(m.frameCount(0x100), 1);
    EXPECT_EQ(m.frameCount(0x400), 0); // unknown ID
}

TEST(CanByteHeatmapModel, InsertionOrderPreserved) {
    CanByteHeatmapModel m;
    m.addFrame(makeFrame(0x300, 1, 0), 1000);
    m.addFrame(makeFrame(0x100, 1, 0), 2000);
    m.addFrame(makeFrame(0x200, 1, 0), 3000);
    const auto &ids = m.trackedIds();
    ASSERT_EQ(ids.size(), 3u);
    EXPECT_EQ(ids[0], 0x300u);
    EXPECT_EQ(ids[1], 0x100u);
    EXPECT_EQ(ids[2], 0x200u);
}

TEST(CanByteHeatmapModel, RingBufferCapsPerid) {
    CanByteHeatmapModel m(5);
    for (int i = 0; i < 20; ++i)
        m.addFrame(makeFrame(0x100, 1, static_cast<uint8_t>(i)), static_cast<uint64_t>(i) * 100);
    EXPECT_EQ(m.frameCount(0x100), 5);
}

TEST(CanByteHeatmapModel, HeatmapEmptyForUnknownId) {
    CanByteHeatmapModel m;
    m.addFrame(makeFrame(0x100, 4, 0x55), 1000);
    auto data = m.heatmapData(0x999, 5'000'000, 10);
    ASSERT_EQ(static_cast<int>(data.size()), 10);
    for (const auto &row : data)
        for (double v : row)
            EXPECT_DOUBLE_EQ(v, -1.0);
}

TEST(CanByteHeatmapModel, HeatmapSingleFrame) {
    CanByteHeatmapModel m;
    // One frame with all bytes = 0x80 at t = 500000 us
    m.addFrame(makeFrame(0x100, 8, 0x80), 500'000);
    // Window: 1s, 10 buckets of 100ms each
    auto data = m.heatmapData(0x100, 1'000'000, 10);
    ASSERT_EQ(static_cast<int>(data.size()), 10);

    // The frame at 500ms should land in the 5th bucket (index 4 or 5)
    bool found = false;
    for (const auto &row : data) {
        if (row[0] >= 0.0) {
            EXPECT_NEAR(row[0], 128.0, 1.0);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(CanByteHeatmapModel, HeatmapAveragesWithinBucket) {
    CanByteHeatmapModel m;
    // Two frames in the same bucket: byte 0 = 0x00 and 0xFF → avg = 0x7F
    m.addFrame(makeFrame(0x100, 1, 0x00),   100'000);
    m.addFrame(makeFrame(0x100, 1, 0xFF),   150'000);
    // Window: 1s, 10 buckets → each bucket 100ms
    // Both frames are in bucket 1 (100ms-200ms)
    auto data = m.heatmapData(0x100, 1'000'000, 10);

    bool found = false;
    for (const auto &row : data) {
        if (row[0] >= 0.0) {
            EXPECT_NEAR(row[0], 127.5, 1.0);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(CanByteHeatmapModel, HeatmapNoBucketCount) {
    CanByteHeatmapModel m;
    m.addFrame(makeFrame(0x100, 1, 0x42), 1000);
    auto data = m.heatmapData(0x100, 1'000'000, 0);
    EXPECT_TRUE(data.empty());
}

TEST(CanByteHeatmapModel, HeatmapOnlyBytesUpToDlc) {
    CanByteHeatmapModel m;
    // DLC = 2: only bytes 0 and 1 have data; bytes 2-7 are zero (zero-init)
    CanFrame f{};
    f.id = 0x100; f.dlc = 2;
    f.data[0] = 0xAA; f.data[1] = 0xBB;
    m.addFrame(f, 500'000);
    auto data = m.heatmapData(0x100, 1'000'000, 10);
    for (const auto &row : data) {
        if (row[0] >= 0.0) {
            EXPECT_NEAR(row[0], 0xAA, 1.0);
            EXPECT_NEAR(row[1], 0xBB, 1.0);
            EXPECT_NEAR(row[2], 0.0,  1.0);  // DLC=2: byte 2 was zero-init
        }
    }
}

TEST(CanByteHeatmapModel, ClearResetsAll) {
    CanByteHeatmapModel m;
    m.addFrame(makeFrame(0x100, 4, 0x42), 1000);
    m.addFrame(makeFrame(0x200, 4, 0x42), 2000);
    m.clear();
    EXPECT_EQ(m.totalIds(), 0);
    EXPECT_TRUE(m.trackedIds().empty());
    EXPECT_EQ(m.latestTimestamp(), 0u);
    EXPECT_EQ(m.frameCount(0x100), 0);
}

TEST(CanByteHeatmapModel, LatestTimestampUpdates) {
    CanByteHeatmapModel m;
    m.addFrame(makeFrame(0x100, 1, 0), 5000);
    m.addFrame(makeFrame(0x100, 1, 0), 1000);
    m.addFrame(makeFrame(0x100, 1, 0), 9000);
    EXPECT_EQ(m.latestTimestamp(), 9000u);
}

TEST(CanByteHeatmapModel, NoDataOutsideWindow) {
    CanByteHeatmapModel m;
    // Frame at t=0; window covers last 1s from maxTs=5s
    m.addFrame(makeFrame(0x100, 1, 0xFF),         0);       // outside window
    m.addFrame(makeFrame(0x100, 1, 0x00), 5'000'000);       // latest ts → now
    // Window: 1s → covers [4s, 5s]. Only second frame should appear.
    auto data = m.heatmapData(0x100, 1'000'000, 10);
    bool sawFF = false;
    for (const auto &row : data)
        if (row[0] > 100.0) sawFF = true;
    // 0xFF frame is outside [4s,5s] window — should NOT appear
    EXPECT_FALSE(sawFF);
}

TEST(CanByteHeatmapModel, MultipleFramesSameId_CountGrows) {
    CanByteHeatmapModel m(10);
    for (int i = 0; i < 7; ++i)
        m.addFrame(makeFrame(0x100, 2, static_cast<uint8_t>(i)), static_cast<uint64_t>(i) * 1000);
    EXPECT_EQ(m.frameCount(0x100), 7);
    EXPECT_EQ(m.totalIds(), 1);
}

TEST(CanByteHeatmapModel, TotalIds_AfterClear_IsZero) {
    CanByteHeatmapModel m;
    m.addFrame(makeFrame(0x1, 1, 0), 1000);
    m.addFrame(makeFrame(0x2, 1, 0), 2000);
    ASSERT_EQ(m.totalIds(), 2);
    m.clear();
    EXPECT_EQ(m.totalIds(), 0);
}

TEST(CanByteHeatmapModel, HeatmapZeroBytes_ShowsZero) {
    CanByteHeatmapModel m;
    m.addFrame(makeFrame(0x100, 4, 0x00), 500'000);
    auto data = m.heatmapData(0x100, 1'000'000, 10);
    for (const auto &row : data) {
        if (row[0] >= 0.0)
            EXPECT_NEAR(row[0], 0.0, 0.01);
    }
}

TEST(CanByteHeatmapModel, HeatmapOneBucket_AllFramesInWindow) {
    CanByteHeatmapModel m;
    m.addFrame(makeFrame(0x100, 1, 0x80), 100'000);
    m.addFrame(makeFrame(0x100, 1, 0x80), 200'000);
    // Window 1s, 1 bucket → both frames in single bucket
    auto data = m.heatmapData(0x100, 1'000'000, 1);
    ASSERT_EQ(static_cast<int>(data.size()), 1);
    EXPECT_GE(data[0][0], 0.0);
    EXPECT_NEAR(data[0][0], 128.0, 1.0);
}

TEST(CanByteHeatmapModel, FrameCountForUnknownId_IsZero) {
    CanByteHeatmapModel m;
    m.addFrame(makeFrame(0x100, 4, 0x42), 1000);
    EXPECT_EQ(m.frameCount(0xDEAD), 0);
}

TEST(CanByteHeatmapModel, HeatmapDifferentBytesInSameFrame) {
    CanByteHeatmapModel m;
    CanFrame f{};
    f.id = 0x100; f.dlc = 2;
    f.data[0] = 0x00;
    f.data[1] = 0xFF;
    m.addFrame(f, 500'000);
    auto data = m.heatmapData(0x100, 1'000'000, 10);
    bool found = false;
    for (const auto &row : data) {
        if (row[0] >= 0.0) {
            EXPECT_NEAR(row[0],   0.0, 1.0);
            EXPECT_NEAR(row[1], 255.0, 1.0);
            found = true;
        }
    }
    EXPECT_TRUE(found);
}
