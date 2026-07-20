/**
 * @file test_canbuserroranalyzer.cpp
 * @brief Unit tests for CanBusErrorAnalyzer – SocketCAN error frame decoding.
 */
#include <gtest/gtest.h>
#include "core/CanBusErrorAnalyzer.h"

using EC = CanBusErrorAnalyzer::ErrorClass;

static CanFrame errFrame(uint32_t errId, uint64_t ts = 0) {
    CanFrame f{};
    f.error     = true;
    f.id        = errId;
    f.timestamp = ts;
    return f;
}

// ── classifyId ────────────────────────────────────────────────────────────────

TEST(CanBusErrorAnalyzer, ClassifyTxTimeout) {
    EXPECT_EQ(CanBusErrorAnalyzer::classifyId(CanBusErrorAnalyzer::ERR_TX_TIMEOUT),
              EC::TxTimeout);
}

TEST(CanBusErrorAnalyzer, ClassifyLostArbitration) {
    EXPECT_EQ(CanBusErrorAnalyzer::classifyId(CanBusErrorAnalyzer::ERR_LOSTARB),
              EC::LostArbitration);
}

TEST(CanBusErrorAnalyzer, ClassifyController) {
    EXPECT_EQ(CanBusErrorAnalyzer::classifyId(CanBusErrorAnalyzer::ERR_CRTL),
              EC::Controller);
}

TEST(CanBusErrorAnalyzer, ClassifyProtocol) {
    EXPECT_EQ(CanBusErrorAnalyzer::classifyId(CanBusErrorAnalyzer::ERR_PROT),
              EC::Protocol);
}

TEST(CanBusErrorAnalyzer, ClassifyTransceiver) {
    EXPECT_EQ(CanBusErrorAnalyzer::classifyId(CanBusErrorAnalyzer::ERR_TRX),
              EC::Transceiver);
}

TEST(CanBusErrorAnalyzer, ClassifyAck) {
    EXPECT_EQ(CanBusErrorAnalyzer::classifyId(CanBusErrorAnalyzer::ERR_ACK),
              EC::Ack);
}

TEST(CanBusErrorAnalyzer, ClassifyBusOff) {
    EXPECT_EQ(CanBusErrorAnalyzer::classifyId(CanBusErrorAnalyzer::ERR_BUSOFF),
              EC::BusOff);
}

TEST(CanBusErrorAnalyzer, ClassifyBusError) {
    EXPECT_EQ(CanBusErrorAnalyzer::classifyId(CanBusErrorAnalyzer::ERR_BUSERROR),
              EC::BusError);
}

TEST(CanBusErrorAnalyzer, ClassifyRestarted) {
    EXPECT_EQ(CanBusErrorAnalyzer::classifyId(CanBusErrorAnalyzer::ERR_RESTARTED),
              EC::Restarted);
}

TEST(CanBusErrorAnalyzer, ClassifyUnknown) {
    EXPECT_EQ(CanBusErrorAnalyzer::classifyId(0), EC::Unknown);
}

TEST(CanBusErrorAnalyzer, ClassifyMultiFlags_HighestPriorityWins) {
    // TX_TIMEOUT (bit 0) has priority over LOSTARB (bit 1)
    uint32_t both = CanBusErrorAnalyzer::ERR_TX_TIMEOUT | CanBusErrorAnalyzer::ERR_LOSTARB;
    EXPECT_EQ(CanBusErrorAnalyzer::classifyId(both), EC::TxTimeout);
}

// ── processFrame – only error frames counted ──────────────────────────────────

TEST(CanBusErrorAnalyzer, InitialState) {
    CanBusErrorAnalyzer a;
    EXPECT_EQ(a.totalErrors(), 0);
    EXPECT_TRUE(a.events().empty());
    EXPECT_FALSE(a.isBusOff());
}

TEST(CanBusErrorAnalyzer, NormalFrameIgnored) {
    CanBusErrorAnalyzer a;
    CanFrame f{};
    f.error = false; f.id = 0x100; f.dlc = 8;
    a.processFrame(f);
    EXPECT_EQ(a.totalErrors(), 0);
}

TEST(CanBusErrorAnalyzer, SingleErrorFrame_CountsOne) {
    CanBusErrorAnalyzer a;
    a.processFrame(errFrame(CanBusErrorAnalyzer::ERR_ACK, 1000));
    EXPECT_EQ(a.totalErrors(), 1);
    EXPECT_EQ(a.errorCount(EC::Ack), 1);
    EXPECT_EQ(a.events().size(), 1u);
}

TEST(CanBusErrorAnalyzer, MultipleErrors_CountsCorrectly) {
    CanBusErrorAnalyzer a;
    a.processFrame(errFrame(CanBusErrorAnalyzer::ERR_PROT,  1000));
    a.processFrame(errFrame(CanBusErrorAnalyzer::ERR_PROT,  2000));
    a.processFrame(errFrame(CanBusErrorAnalyzer::ERR_BUSOFF, 3000));
    EXPECT_EQ(a.totalErrors(), 3);
    EXPECT_EQ(a.errorCount(EC::Protocol), 2);
    EXPECT_EQ(a.errorCount(EC::BusOff), 1);
}

TEST(CanBusErrorAnalyzer, IsBusOff_AfterBusOffFrame) {
    CanBusErrorAnalyzer a;
    EXPECT_FALSE(a.isBusOff());
    a.processFrame(errFrame(CanBusErrorAnalyzer::ERR_BUSOFF, 5000));
    EXPECT_TRUE(a.isBusOff());
}

TEST(CanBusErrorAnalyzer, IsBusOff_NotSetByOtherErrors) {
    CanBusErrorAnalyzer a;
    a.processFrame(errFrame(CanBusErrorAnalyzer::ERR_PROT, 1000));
    a.processFrame(errFrame(CanBusErrorAnalyzer::ERR_ACK,  2000));
    EXPECT_FALSE(a.isBusOff());
}

TEST(CanBusErrorAnalyzer, UnknownErrorId_StillCounted) {
    CanBusErrorAnalyzer a;
    a.processFrame(errFrame(0, 100));
    EXPECT_EQ(a.totalErrors(), 1);
    EXPECT_EQ(a.errorCount(EC::Unknown), 1);
}

// ── reset ─────────────────────────────────────────────────────────────────────

TEST(CanBusErrorAnalyzer, ResetClearsAll) {
    CanBusErrorAnalyzer a;
    a.processFrame(errFrame(CanBusErrorAnalyzer::ERR_BUSOFF, 1000));
    a.processFrame(errFrame(CanBusErrorAnalyzer::ERR_PROT,   2000));
    a.reset();
    EXPECT_EQ(a.totalErrors(), 0);
    EXPECT_EQ(a.errorCount(EC::BusOff), 0);
    EXPECT_TRUE(a.events().empty());
    EXPECT_FALSE(a.isBusOff());
}

// ── hasBurst ──────────────────────────────────────────────────────────────────

TEST(CanBusErrorAnalyzer, NoBurst_TooFewErrors) {
    CanBusErrorAnalyzer a;
    CanBusErrorAnalyzer::BurstConfig cfg{10, 1'000'000};
    for (int i = 0; i < 5; ++i)
        a.processFrame(errFrame(CanBusErrorAnalyzer::ERR_PROT, static_cast<uint64_t>(i) * 100));
    EXPECT_FALSE(a.hasBurst(cfg));
}

TEST(CanBusErrorAnalyzer, Burst_ThresholdReachedInWindow) {
    CanBusErrorAnalyzer a;
    CanBusErrorAnalyzer::BurstConfig cfg{5, 1'000'000};
    for (int i = 0; i < 5; ++i)
        a.processFrame(errFrame(CanBusErrorAnalyzer::ERR_PROT, static_cast<uint64_t>(i) * 100'000));
    EXPECT_TRUE(a.hasBurst(cfg));
}

TEST(CanBusErrorAnalyzer, Burst_ErrorsSpreadTooWide_NoBurst) {
    CanBusErrorAnalyzer a;
    // 10 errors each 500ms apart → span = 4.5s, window = 1s
    CanBusErrorAnalyzer::BurstConfig cfg{5, 1'000'000};
    for (int i = 0; i < 10; ++i)
        a.processFrame(errFrame(CanBusErrorAnalyzer::ERR_PROT, static_cast<uint64_t>(i) * 500'000));
    // Most recent 5 span 4*500ms = 2s > 1s window → no burst from the back
    EXPECT_FALSE(a.hasBurst(cfg));
}

// ── recentEvents ──────────────────────────────────────────────────────────────

TEST(CanBusErrorAnalyzer, RecentEvents_FiltersByWindow) {
    CanBusErrorAnalyzer a;
    a.processFrame(errFrame(CanBusErrorAnalyzer::ERR_PROT, 1'000'000));   // 1s
    a.processFrame(errFrame(CanBusErrorAnalyzer::ERR_PROT, 5'000'000));   // 5s (latest)
    // Window 2s → cutoff = 3s; only the 5s event is recent
    auto recent = a.recentEvents(2'000'000);
    ASSERT_EQ(recent.size(), 1u);
    EXPECT_EQ(recent[0].timestampUs, 5'000'000u);
}

TEST(CanBusErrorAnalyzer, RecentEvents_ZeroWindow_ReturnsEmpty) {
    CanBusErrorAnalyzer a;
    a.processFrame(errFrame(CanBusErrorAnalyzer::ERR_PROT, 1000));
    EXPECT_TRUE(a.recentEvents(0).empty());
}

// ── errorRate ─────────────────────────────────────────────────────────────────

TEST(CanBusErrorAnalyzer, ErrorRate_ZeroWhenEmpty) {
    CanBusErrorAnalyzer a;
    EXPECT_DOUBLE_EQ(a.errorRate(1'000'000), 0.0);
}

TEST(CanBusErrorAnalyzer, ErrorRate_ZeroWindow) {
    CanBusErrorAnalyzer a;
    a.processFrame(errFrame(CanBusErrorAnalyzer::ERR_PROT, 1000));
    a.processFrame(errFrame(CanBusErrorAnalyzer::ERR_PROT, 2000));
    EXPECT_DOUBLE_EQ(a.errorRate(0), 0.0);
}

TEST(CanBusErrorAnalyzer, ErrorRate_Computed) {
    CanBusErrorAnalyzer a;
    // 11 events over 1 second (1'000'000 µs) → rate ≈ 11 / 1.0 = 11 errors/s
    for (int i = 0; i <= 10; ++i)
        a.processFrame(errFrame(CanBusErrorAnalyzer::ERR_PROT, static_cast<uint64_t>(i) * 100'000));
    double rate = a.errorRate(2'000'000);
    EXPECT_NEAR(rate, 10.0, 1.0);  // 11 events spanning 1s → 11/1s ≈ 11, allow ±1
}
