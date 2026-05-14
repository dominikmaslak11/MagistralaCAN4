/**
 * @file test_canratefilter.cpp
 * @brief Unit tests for CanRateFilter – per-ID frame-rate limiting.
 */
#include <gtest/gtest.h>
#include "core/CanRateFilter.h"

static CanFrame frame(uint32_t id) {
    CanFrame f{};
    f.id = id; f.dlc = 1;
    return f;
}

// ── Default: unlimited rate (globalHz = 0) ────────────────────────────────────

TEST(CanRateFilter, DefaultUnlimited_AllFramesPass) {
    CanRateFilter rf;
    for (int i = 0; i < 10; ++i)
        EXPECT_TRUE(rf.shouldPass(frame(0x100), static_cast<uint64_t>(i) * 1000));
}

TEST(CanRateFilter, DefaultUnlimited_CountsAllAccepted) {
    CanRateFilter rf;
    rf.shouldPass(frame(0x100), 1000);
    rf.shouldPass(frame(0x100), 2000);
    rf.shouldPass(frame(0x200), 3000);
    EXPECT_EQ(rf.accepted(), 3);
    EXPECT_EQ(rf.rejected(), 0);
}

// ── Global rate limiting ──────────────────────────────────────────────────────

TEST(CanRateFilter, GlobalRate10Hz_FirstFrameAlwaysPasses) {
    CanRateFilter rf;
    rf.setGlobalMaxHz(10.0);  // min interval = 100ms = 100'000µs
    EXPECT_TRUE(rf.shouldPass(frame(0x100), 0));
}

TEST(CanRateFilter, GlobalRate10Hz_SecondFrameBeforeIntervalRejected) {
    CanRateFilter rf;
    rf.setGlobalMaxHz(10.0);  // interval = 100'000µs
    rf.shouldPass(frame(0x100), 0);
    EXPECT_FALSE(rf.shouldPass(frame(0x100), 50'000));  // only 50ms elapsed
}

TEST(CanRateFilter, GlobalRate10Hz_SecondFrameAfterIntervalPasses) {
    CanRateFilter rf;
    rf.setGlobalMaxHz(10.0);
    rf.shouldPass(frame(0x100), 0);
    EXPECT_TRUE(rf.shouldPass(frame(0x100), 100'000));  // exactly 100ms elapsed
}

TEST(CanRateFilter, GlobalRate_DifferentIdsIndependent) {
    CanRateFilter rf;
    rf.setGlobalMaxHz(10.0);  // interval = 100'000µs
    rf.shouldPass(frame(0x100), 0);
    rf.shouldPass(frame(0x200), 0);
    // At t=50ms, 0x100 would be rejected but 0x200 was not seen before t=0
    // — but 0x200 was also just accepted at t=0, so it too is rejected at t=50ms
    EXPECT_FALSE(rf.shouldPass(frame(0x100), 50'000));
    EXPECT_FALSE(rf.shouldPass(frame(0x200), 50'000));
    // At t=110ms, both should pass again
    EXPECT_TRUE(rf.shouldPass(frame(0x100), 110'000));
    EXPECT_TRUE(rf.shouldPass(frame(0x200), 110'000));
}

// ── Per-ID override ───────────────────────────────────────────────────────────

TEST(CanRateFilter, IdOverride_HigherRate) {
    CanRateFilter rf;
    rf.setGlobalMaxHz(10.0);  // 100ms
    rf.setIdMaxHz(0x100, 100.0);  // 10ms for 0x100
    rf.shouldPass(frame(0x100), 0);
    EXPECT_TRUE(rf.shouldPass(frame(0x100), 10'000));  // 10ms: passes with override
    EXPECT_FALSE(rf.shouldPass(frame(0x100), 15'000)); // 5ms: rejected
}

TEST(CanRateFilter, IdOverride_ZeroHz_BlocksId) {
    CanRateFilter rf;
    rf.setGlobalMaxHz(10.0);
    rf.setIdMaxHz(0x200, 0.0);  // block 0x200 entirely
    EXPECT_FALSE(rf.shouldPass(frame(0x200), 0));
    EXPECT_FALSE(rf.shouldPass(frame(0x200), 1'000'000));
    // 0x100 is not blocked
    EXPECT_TRUE(rf.shouldPass(frame(0x100), 0));
}

TEST(CanRateFilter, IdOverride_ClearFallsBackToGlobal) {
    CanRateFilter rf;
    rf.setGlobalMaxHz(10.0);  // 100ms
    rf.setIdMaxHz(0x100, 1.0);   // 1 Hz = 1s interval
    rf.clearIdOverride(0x100);   // back to global 10Hz
    rf.shouldPass(frame(0x100), 0);
    // With global 10Hz, should pass after 100ms
    EXPECT_TRUE(rf.shouldPass(frame(0x100), 100'000));
}

// ── Counters ──────────────────────────────────────────────────────────────────

TEST(CanRateFilter, Counters_AcceptedPlusRejectedEqualsTotal) {
    CanRateFilter rf;
    rf.setGlobalMaxHz(10.0);
    for (int i = 0; i < 20; ++i)
        rf.shouldPass(frame(0x100), static_cast<uint64_t>(i) * 10'000);
    EXPECT_GT(rf.accepted(), 0);
    EXPECT_GT(rf.rejected(), 0);
    // With 10Hz and frames every 10ms, first passes then every other 10th
    // Just verify total
    int total = rf.accepted() + rf.rejected();
    EXPECT_EQ(total, 20);
}

TEST(CanRateFilter, Reset_ClearsCountersAndTimestamps) {
    CanRateFilter rf;
    rf.setGlobalMaxHz(10.0);
    rf.shouldPass(frame(0x100), 0);
    rf.shouldPass(frame(0x100), 50'000);  // rejected
    ASSERT_EQ(rf.rejected(), 1);

    rf.reset();
    EXPECT_EQ(rf.accepted(), 0);
    EXPECT_EQ(rf.rejected(), 0);
    // After reset, first frame passes again immediately
    EXPECT_TRUE(rf.shouldPass(frame(0x100), 50'000));
}

TEST(CanRateFilter, ResetTimestamps_KeepsCounters) {
    CanRateFilter rf;
    rf.setGlobalMaxHz(10.0);
    rf.shouldPass(frame(0x100), 0);
    rf.shouldPass(frame(0x100), 50'000);  // rejected
    ASSERT_EQ(rf.accepted(), 1);
    ASSERT_EQ(rf.rejected(), 1);

    rf.resetTimestamps();
    EXPECT_EQ(rf.accepted(), 1);  // counter preserved
    EXPECT_EQ(rf.rejected(), 1);  // counter preserved
    // But next frame should pass (timestamps cleared)
    EXPECT_TRUE(rf.shouldPass(frame(0x100), 50'000));
}

// ── Edge cases ────────────────────────────────────────────────────────────────

TEST(CanRateFilter, NegativeHz_TreatedAsUnlimited) {
    CanRateFilter rf;
    rf.setGlobalMaxHz(-5.0);  // negative → unlimited
    EXPECT_TRUE(rf.shouldPass(frame(0x100), 0));
    EXPECT_TRUE(rf.shouldPass(frame(0x100), 0));  // same timestamp, still passes
}

TEST(CanRateFilter, VeryHighRate_EveryFramePasses) {
    CanRateFilter rf;
    rf.setGlobalMaxHz(1'000'000.0);  // 1MHz → 1µs interval
    EXPECT_TRUE(rf.shouldPass(frame(0x100), 0));
    EXPECT_FALSE(rf.shouldPass(frame(0x100), 0));  // same µs: rejected
    EXPECT_TRUE(rf.shouldPass(frame(0x100), 1));   // 1µs later: passes
}

TEST(CanRateFilter, MultipleIds_EachTrackedSeparately) {
    CanRateFilter rf;
    rf.setGlobalMaxHz(10.0);  // 100ms
    for (uint32_t id : {0x100u, 0x200u, 0x300u})
        EXPECT_TRUE(rf.shouldPass(frame(id), 0));  // first of each ID always passes
    EXPECT_EQ(rf.accepted(), 3);
}
