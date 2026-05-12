#include <gtest/gtest.h>
#include "core/BusLoadAnalyzer.h"

// ── Helpers ───────────────────────────────────────────────────────────────────

static CanFrame makeFrame(uint32_t id, uint8_t dlc, bool extended = false,
                           bool fd = false, uint64_t tsUs = 0) {
    CanFrame f{};
    f.id       = id;
    f.dlc      = dlc;
    f.extended = extended;
    f.fd       = fd;
    f.timestamp = tsUs;
    return f;
}

// ── frameBits ─────────────────────────────────────────────────────────────────

TEST(BusLoadAnalyzer, FrameBitsClassicStandard) {
    // 11-bit ID, DLC=0: 44 bits
    EXPECT_EQ(BusLoadAnalyzer::frameBits(false, 0, false), 44u);
    // DLC=8: 44 + 64 = 108
    EXPECT_EQ(BusLoadAnalyzer::frameBits(false, 8, false), 108u);
}

TEST(BusLoadAnalyzer, FrameBitsClassicExtended) {
    // 29-bit ID, DLC=0: 64 bits
    EXPECT_EQ(BusLoadAnalyzer::frameBits(true, 0, false), 64u);
    // DLC=8: 64 + 64 = 128
    EXPECT_EQ(BusLoadAnalyzer::frameBits(true, 8, false), 128u);
}

TEST(BusLoadAnalyzer, FrameBitsFdStandard) {
    // FD standard, DLC=0: 60 bits
    EXPECT_EQ(BusLoadAnalyzer::frameBits(false, 0, true), 60u);
    // FD standard, DLC=64: 60 + 512 = 572
    EXPECT_EQ(BusLoadAnalyzer::frameBits(false, 64, true), 572u);
}

TEST(BusLoadAnalyzer, FrameBitsFdExtended) {
    // FD extended, DLC=0: 80 bits
    EXPECT_EQ(BusLoadAnalyzer::frameBits(true, 0, true), 80u);
    // FD extended, DLC=64: 80 + 512 = 592
    EXPECT_EQ(BusLoadAnalyzer::frameBits(true, 64, true), 592u);
}

TEST(BusLoadAnalyzer, FrameBitsClassicDlcClampsAt8) {
    // DLC > 8 is illegal in classic CAN — should clamp
    EXPECT_EQ(BusLoadAnalyzer::frameBits(false, 9, false),
              BusLoadAnalyzer::frameBits(false, 8, false));
    EXPECT_EQ(BusLoadAnalyzer::frameBits(true, 12, false),
              BusLoadAnalyzer::frameBits(true, 8, false));
}

TEST(BusLoadAnalyzer, FrameBitsFdDlcClampsAt64) {
    EXPECT_EQ(BusLoadAnalyzer::frameBits(false, 65, true),
              BusLoadAnalyzer::frameBits(false, 64, true));
}

// ── Construction defaults ─────────────────────────────────────────────────────

TEST(BusLoadAnalyzer, DefaultStateIsZero) {
    BusLoadAnalyzer bla;
    EXPECT_DOUBLE_EQ(bla.currentLoad(), 0.0);
    EXPECT_DOUBLE_EQ(bla.peakLoad(),    0.0);
    EXPECT_DOUBLE_EQ(bla.framesPerSec(), 0.0);
    EXPECT_EQ(bla.uniqueIdCount(), 0u);
}

// ── Single-frame load ─────────────────────────────────────────────────────────

TEST(BusLoadAnalyzer, SingleFrameLoad) {
    // 500 kbps, 1s window
    BusLoadAnalyzer bla(500000);
    bla.setWindowSec(1.0);

    // Standard DLC=0 → 44 bits at t=0
    bla.processFrame(makeFrame(0x100, 0, false, false, 0));

    double load = bla.currentLoad();
    double expected = 44.0 / 500000.0;
    EXPECT_NEAR(load, expected, 1e-9);
}

TEST(BusLoadAnalyzer, PeakLoadTracked) {
    BusLoadAnalyzer bla(500000);
    bla.setWindowSec(1.0);

    bla.processFrame(makeFrame(0x100, 8, false, false, 0));
    double first = bla.peakLoad();

    // Reset load via reset()
    bla.reset();
    EXPECT_DOUBLE_EQ(bla.peakLoad(), 0.0);

    // After reset, inject smaller frame
    bla.processFrame(makeFrame(0x100, 0, false, false, 0));
    EXPECT_LT(bla.peakLoad(), first);
}

// ── Sliding window pruning ────────────────────────────────────────────────────

TEST(BusLoadAnalyzer, OldFramesPruned) {
    BusLoadAnalyzer bla(500000);
    bla.setWindowSec(1.0); // 1 s window

    // Frame at t=0
    bla.processFrame(makeFrame(0x100, 8, false, false, 0));
    double beforePrune = bla.currentLoad();
    EXPECT_GT(beforePrune, 0.0);

    // Frame at t=2 000 000 µs (2 s later) — t=0 frame falls outside 1 s window
    bla.processFrame(makeFrame(0x200, 0, false, false, 2'000'000));

    // Only the DLC=0 (44-bit) frame should remain
    double afterPrune = bla.currentLoad();
    double expected = 44.0 / 500000.0;
    EXPECT_NEAR(afterPrune, expected, 1e-9);
}

TEST(BusLoadAnalyzer, WindowFillsAndDrains) {
    BusLoadAnalyzer bla(1'000'000); // 1 Mbit/s
    bla.setWindowSec(1.0);

    // 10 frames spaced 100 ms each (0.1 s apart), DLC=8 (108 bits each)
    for (int i = 0; i < 10; ++i)
        bla.processFrame(makeFrame(0x100, 8, false, false,
                                   static_cast<uint64_t>(i) * 100'000));

    // All 10 should be in window (latest = 900 ms, oldest = 0 ms, window=1s)
    EXPECT_NEAR(bla.framesPerSec(), 10.0, 0.5);

    // Move to t = 1 500 000 µs; frames 0..4 (0..400 ms) should be pruned
    bla.processFrame(makeFrame(0x100, 0, false, false, 1'500'000));
    // Remaining: frames 5..9 + new = 6 frames in window, plus new DLC=0
    // framesPerSec ≈ 6 / 1.0  (window still 1 s wide)
    EXPECT_LE(bla.framesPerSec(), 10.0);
}

// ── framesPerSec ──────────────────────────────────────────────────────────────

TEST(BusLoadAnalyzer, FramesPerSec) {
    BusLoadAnalyzer bla(500000);
    bla.setWindowSec(1.0);

    for (int i = 0; i < 5; ++i)
        bla.processFrame(makeFrame(0x100, 1, false, false,
                                   static_cast<uint64_t>(i) * 100'000));
    EXPECT_NEAR(bla.framesPerSec(), 5.0, 0.1);
}

// ── uniqueIdCount ─────────────────────────────────────────────────────────────

TEST(BusLoadAnalyzer, UniqueIdCountAccumulates) {
    BusLoadAnalyzer bla;
    bla.processFrame(makeFrame(0x100, 1, false, false, 0));
    bla.processFrame(makeFrame(0x100, 1, false, false, 1000));
    EXPECT_EQ(bla.uniqueIdCount(), 1u);

    bla.processFrame(makeFrame(0x200, 1, false, false, 2000));
    EXPECT_EQ(bla.uniqueIdCount(), 2u);
}

TEST(BusLoadAnalyzer, UniqueIdCountNotAffectedByPrune) {
    // m_seenIds is lifetime-cumulative — pruning window doesn't remove IDs
    BusLoadAnalyzer bla;
    bla.setWindowSec(0.001); // 1 ms window — prunes quickly
    bla.processFrame(makeFrame(0x100, 1, false, false, 0));
    bla.processFrame(makeFrame(0x200, 1, false, false, 100'000));
    // First frame pruned, but seenIds still has 2 entries
    EXPECT_EQ(bla.uniqueIdCount(), 2u);
}

// ── topLoaders ────────────────────────────────────────────────────────────────

TEST(BusLoadAnalyzer, TopLoadersEmpty) {
    BusLoadAnalyzer bla;
    EXPECT_TRUE(bla.topLoaders(5).empty());
}

TEST(BusLoadAnalyzer, TopLoadersSortedByLoad) {
    BusLoadAnalyzer bla(500000);
    bla.setWindowSec(1.0);

    // 0x100: DLC=8 (108 bits)
    bla.processFrame(makeFrame(0x100, 8, false, false, 0));
    // 0x200: DLC=0 (44 bits)
    bla.processFrame(makeFrame(0x200, 0, false, false, 1000));

    auto top = bla.topLoaders(10);
    ASSERT_EQ(top.size(), 2u);
    EXPECT_EQ(top[0].id, 0x100u); // higher load first
    EXPECT_GT(top[0].loadFraction, top[1].loadFraction);
}

TEST(BusLoadAnalyzer, TopLoadersLimitN) {
    BusLoadAnalyzer bla(500000);
    bla.setWindowSec(1.0);

    for (uint32_t id = 0x100; id <= 0x110; ++id)
        bla.processFrame(makeFrame(id, 4, false, false, 0));

    auto top = bla.topLoaders(3);
    EXPECT_EQ(top.size(), 3u);
}

TEST(BusLoadAnalyzer, TopLoadersFrameCount) {
    BusLoadAnalyzer bla(500000);
    bla.setWindowSec(1.0);

    for (int i = 0; i < 5; ++i)
        bla.processFrame(makeFrame(0x100, 4, false, false,
                                   static_cast<uint64_t>(i) * 10'000));

    auto top = bla.topLoaders(1);
    ASSERT_EQ(top.size(), 1u);
    EXPECT_EQ(top[0].id, 0x100u);
    EXPECT_EQ(top[0].framesInWindow, 5u);
}

// ── reset ─────────────────────────────────────────────────────────────────────

TEST(BusLoadAnalyzer, ResetClearsAll) {
    BusLoadAnalyzer bla;
    bla.processFrame(makeFrame(0x100, 8, false, false, 0));
    bla.processFrame(makeFrame(0x200, 4, false, false, 1000));

    bla.reset();

    EXPECT_DOUBLE_EQ(bla.currentLoad(), 0.0);
    EXPECT_DOUBLE_EQ(bla.peakLoad(),    0.0);
    EXPECT_DOUBLE_EQ(bla.framesPerSec(), 0.0);
    EXPECT_TRUE(bla.topLoaders(10).empty());
}

// ── setBaudRate guard ─────────────────────────────────────────────────────────

TEST(BusLoadAnalyzer, SetBaudRateZeroDefaultsTo500k) {
    BusLoadAnalyzer bla(250000);
    bla.setBaudRate(0); // should default to 500 000

    // processFrame and check load uses 500000 denom
    bla.processFrame(makeFrame(0x100, 0, false, false, 0));
    double load = bla.currentLoad();
    EXPECT_NEAR(load, 44.0 / 500000.0, 1e-9);
}

// ── Load fraction sanity ──────────────────────────────────────────────────────

TEST(BusLoadAnalyzer, LoadFractionNonNegative) {
    BusLoadAnalyzer bla(500000);
    bla.setWindowSec(1.0);

    // Send 10 frames well within capacity; load must be in (0, 1)
    for (int i = 0; i < 10; ++i)
        bla.processFrame(makeFrame(0x100, 8, false, false,
                                   static_cast<uint64_t>(i) * 1000));

    double load = bla.currentLoad();
    EXPECT_GT(load, 0.0);
    EXPECT_LT(load, 1.0);
}
