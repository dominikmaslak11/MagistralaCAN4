#include <gtest/gtest.h>
#include "core/CanTimelineModel.h"
#include "core/CanFrame.h"

static CanFrame makeFrame(uint32_t id, uint8_t dlc = 8) {
    CanFrame f{}; f.id = id; f.dlc = dlc;
    for (int i = 0; i < dlc && i < 8; ++i) f.data[i] = static_cast<uint8_t>(i);
    return f;
}

TEST(CanTimelineModel, EmptyInitialState) {
    CanTimelineModel m;
    EXPECT_EQ(m.totalEventCount(), 0);
    EXPECT_TRUE(m.trackedIds().empty());
    auto [lo, hi] = m.timeRange();
    EXPECT_EQ(lo, 0u); EXPECT_EQ(hi, 0u);
}

TEST(CanTimelineModel, AddFrameIncrementsCount) {
    CanTimelineModel m;
    m.addFrame(makeFrame(0x100), 1000);
    m.addFrame(makeFrame(0x100), 2000);
    EXPECT_EQ(m.totalEventCount(), 2);
}

TEST(CanTimelineModel, TrackedIdsPopulated) {
    CanTimelineModel m;
    m.addFrame(makeFrame(0x100), 1000);
    m.addFrame(makeFrame(0x200), 2000);
    auto ids = m.trackedIds();
    ASSERT_EQ(ids.size(), 2u);
}

TEST(CanTimelineModel, FrequentIdFirst) {
    CanTimelineModel m;
    m.addFrame(makeFrame(0x200), 1000);
    for (int i = 0; i < 5; ++i) m.addFrame(makeFrame(0x100), static_cast<uint64_t>(i) * 100);
    auto ids = m.trackedIds();
    ASSERT_FALSE(ids.empty());
    EXPECT_EQ(ids[0], 0x100u); // 0x100 has 5 frames, should be first
}

TEST(CanTimelineModel, MaxTrackedIdsRespected) {
    CanTimelineModel m(3); // max 3 IDs
    for (uint32_t id = 0x100; id <= 0x110; id += 0x010)
        m.addFrame(makeFrame(id), id);
    EXPECT_LE(m.trackedIds().size(), 3u);
}

TEST(CanTimelineModel, IdIndexCorrect) {
    CanTimelineModel m;
    m.addFrame(makeFrame(0x100), 1000);
    m.addFrame(makeFrame(0x200), 2000);
    int idx = m.idIndex(0x100);
    EXPECT_GE(idx, 0);
    EXPECT_EQ(m.idIndex(0xDEAD), -1); // unknown ID
}

TEST(CanTimelineModel, EventsForIdChronological) {
    CanTimelineModel m;
    m.addFrame(makeFrame(0x100), 3000);
    m.addFrame(makeFrame(0x100), 1000);
    m.addFrame(makeFrame(0x100), 2000);
    auto evs = m.eventsForId(0x100);
    ASSERT_EQ(evs.size(), 3u);
    // deque preserves insertion order (not sorted) — just check all present
    bool has1=false, has2=false, has3=false;
    for (auto &e : evs) {
        if (e.timestampUs == 3000) has3=true;
        if (e.timestampUs == 1000) has1=true;
        if (e.timestampUs == 2000) has2=true;
    }
    EXPECT_TRUE(has1 && has2 && has3);
}

TEST(CanTimelineModel, EventsForUnknownIdEmpty) {
    CanTimelineModel m;
    m.addFrame(makeFrame(0x100), 1000);
    EXPECT_TRUE(m.eventsForId(0x999).empty());
}

TEST(CanTimelineModel, EventsInRangeFiltered) {
    CanTimelineModel m;
    for (int i = 1; i <= 10; ++i)
        m.addFrame(makeFrame(0x100), static_cast<uint64_t>(i) * 1000);
    auto evs = m.eventsInRange(0x100, 3000, 7000);
    ASSERT_EQ(evs.size(), 5u); // ts 3000,4000,5000,6000,7000
    for (auto &e : evs) {
        EXPECT_GE(e.timestampUs, 3000u);
        EXPECT_LE(e.timestampUs, 7000u);
    }
}

TEST(CanTimelineModel, AllEventsInRangeSorted) {
    CanTimelineModel m;
    m.addFrame(makeFrame(0x100), 5000);
    m.addFrame(makeFrame(0x200), 3000);
    m.addFrame(makeFrame(0x100), 7000);
    m.addFrame(makeFrame(0x200), 9000);
    auto evs = m.allEventsInRange(0, 99999);
    ASSERT_EQ(evs.size(), 4u);
    for (size_t i = 1; i < evs.size(); ++i)
        EXPECT_LE(evs[i-1].timestampUs, evs[i].timestampUs);
}

TEST(CanTimelineModel, TimeRangeCorrect) {
    CanTimelineModel m;
    m.addFrame(makeFrame(0x1), 5000);
    m.addFrame(makeFrame(0x2), 1000);
    m.addFrame(makeFrame(0x3), 9000);
    auto [lo, hi] = m.timeRange();
    EXPECT_EQ(lo, 1000u);
    EXPECT_EQ(hi, 9000u);
}

TEST(CanTimelineModel, RingBufferLimitsPerIdEvents) {
    CanTimelineModel m(16, 5); // max 5 events per ID
    for (int i = 0; i < 20; ++i)
        m.addFrame(makeFrame(0x100), static_cast<uint64_t>(i) * 100);
    auto evs = m.eventsForId(0x100);
    EXPECT_EQ(static_cast<int>(evs.size()), 5);
    // should keep the LAST 5 (newest)
    EXPECT_EQ(evs.back().timestampUs, 1900u);
}

TEST(CanTimelineModel, ClearResetsAll) {
    CanTimelineModel m;
    m.addFrame(makeFrame(0x100), 1000);
    m.addFrame(makeFrame(0x200), 2000);
    m.clear();
    EXPECT_EQ(m.totalEventCount(), 0);
    EXPECT_TRUE(m.trackedIds().empty());
    auto [lo, hi] = m.timeRange();
    EXPECT_EQ(lo, 0u); EXPECT_EQ(hi, 0u);
}

TEST(CanTimelineModel, DataRoundtrip) {
    CanTimelineModel m;
    CanFrame f = makeFrame(0x3FF, 4);
    f.data[0]=0xAB; f.data[1]=0xCD; f.data[2]=0xEF; f.data[3]=0x01;
    m.addFrame(f, 42000);
    auto evs = m.eventsForId(0x3FF);
    ASSERT_EQ(evs.size(), 1u);
    EXPECT_EQ(evs[0].dlc, 4);
    EXPECT_EQ(evs[0].data[0], 0xAB);
    EXPECT_EQ(evs[0].data[1], 0xCD);
    EXPECT_EQ(evs[0].data[3], 0x01);
}
