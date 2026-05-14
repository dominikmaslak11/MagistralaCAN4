/**
 * @file test_canbitanalyzer.cpp
 * @brief Unit tests for CanBitAnalyzer and CanBitProfile.
 */
#include <gtest/gtest.h>
#include "core/CanBitAnalyzer.h"
#include <QCoreApplication>

// ── Helpers ──────────────────────────────────────────────────────────────────

static CanFrame makeFrame(uint32_t id, std::initializer_list<uint8_t> bytes,
                          bool extended = false) {
    CanFrame f{};
    f.id       = id;
    f.extended = extended;
    f.dlc      = static_cast<uint8_t>(bytes.size());
    int i = 0;
    for (uint8_t b : bytes) f.data[i++] = b;
    return f;
}

// ── Empty analyzer ────────────────────────────────────────────────────────

TEST(CanBitAnalyzer, Empty_ProfilesEmpty) {
    CanBitAnalyzer a;
    EXPECT_TRUE(a.profiles().isEmpty());
}

TEST(CanBitAnalyzer, Empty_Report) {
    CanBitAnalyzer a;
    EXPECT_TRUE(a.report().contains("No frames"));
}

TEST(CanBitAnalyzer, Empty_ProfileFor_Nullptr) {
    CanBitAnalyzer a;
    EXPECT_EQ(a.profileFor(0x100), nullptr);
}

// ── Single frame — all bits are both alwaysZero and alwaysOne ────────────
// (because with only one frame, bits 0→alwaysZero, bits 1→alwaysOne)

TEST(CanBitAnalyzer, SingleFrame_CorrectZeroOneMasks) {
    CanBitAnalyzer a;
    // byte0 = 0xA5 = 0b10100101
    a.add(makeFrame(0x100, {0xA5}));

    const auto *p = a.profileFor(0x100);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->frameCount, 1u);
    // alwaysZero: bits that are 0 in 0xA5 → ~0xA5 = 0x5A
    EXPECT_EQ(p->alwaysZero[0], static_cast<uint8_t>(~0xA5));
    // alwaysOne: bits that are 1 in 0xA5 → 0xA5
    EXPECT_EQ(p->alwaysOne[0], 0xA5u);
}

// ── Single frame — varying() is 0 (no bit has been both 0 and 1) ─────────

TEST(CanBitAnalyzer, SingleFrame_VaryingIsZero) {
    CanBitAnalyzer a;
    a.add(makeFrame(0x100, {0xFF}));

    const auto *p = a.profileFor(0x100);
    ASSERT_NE(p, nullptr);
    auto var = p->varying();
    EXPECT_EQ(var[0], 0u);
}

// ── Two identical frames — no varying bits ────────────────────────────────

TEST(CanBitAnalyzer, TwoIdenticalFrames_NoVarying) {
    CanBitAnalyzer a;
    a.add(makeFrame(0x100, {0x55}));
    a.add(makeFrame(0x100, {0x55}));

    const auto *p = a.profileFor(0x100);
    ASSERT_NE(p, nullptr);
    auto var = p->varying();
    EXPECT_EQ(var[0], 0u);
    EXPECT_EQ(p->alwaysZero[0], static_cast<uint8_t>(~0x55));
    EXPECT_EQ(p->alwaysOne[0],  0x55u);
}

// ── Two frames with different bytes — varying bits detected ───────────────

TEST(CanBitAnalyzer, TwoFrames_DifferentByte0_VaryingDetected) {
    CanBitAnalyzer a;
    a.add(makeFrame(0x100, {0x00}));  // all bits 0
    a.add(makeFrame(0x100, {0xFF}));  // all bits 1

    const auto *p = a.profileFor(0x100);
    ASSERT_NE(p, nullptr);
    // Every bit saw both 0 and 1
    EXPECT_EQ(p->alwaysZero[0], 0u);
    EXPECT_EQ(p->alwaysOne[0],  0u);
    auto var = p->varying();
    EXPECT_EQ(var[0], 0xFFu);
}

// ── Partial varying — only some bits change ───────────────────────────────

TEST(CanBitAnalyzer, PartialVarying_OnlySomeBits) {
    CanBitAnalyzer a;
    // Bit 0 toggles; bits 1-7 stay at 1
    a.add(makeFrame(0x100, {0xFE}));  // bit0=0, rest=1
    a.add(makeFrame(0x100, {0xFF}));  // bit0=1, rest=1

    const auto *p = a.profileFor(0x100);
    ASSERT_NE(p, nullptr);
    auto var = p->varying();
    EXPECT_EQ(var[0], 0x01u);         // only bit 0 varies
    EXPECT_EQ(p->alwaysZero[0], 0u);  // no bit is always 0
    EXPECT_EQ(p->alwaysOne[0], 0xFEu); // bits 1-7 are always 1
}

// ── Multiple bytes — per-byte tracking ───────────────────────────────────

TEST(CanBitAnalyzer, MultipleBytes_PerByteTracking) {
    CanBitAnalyzer a;
    a.add(makeFrame(0x100, {0x00, 0xFF, 0xAA}));
    a.add(makeFrame(0x100, {0xFF, 0xFF, 0x55}));

    const auto *p = a.profileFor(0x100);
    ASSERT_NE(p, nullptr);

    // byte0: 0x00→0xFF, all vary
    EXPECT_EQ(p->varying()[0], 0xFFu);
    // byte1: 0xFF→0xFF, no vary, alwaysOne=0xFF
    EXPECT_EQ(p->varying()[1], 0u);
    EXPECT_EQ(p->alwaysOne[1], 0xFFu);
    // byte2: 0xAA→0x55, bit pattern alternates
    // 0xAA = 10101010, 0x55 = 01010101 — every bit toggles
    EXPECT_EQ(p->varying()[2], 0xFFu);
}

// ── Multiple CAN IDs — each gets its own profile ─────────────────────────

TEST(CanBitAnalyzer, MultipleIds_SeparateProfiles) {
    CanBitAnalyzer a;
    a.add(makeFrame(0x100, {0x01}));
    a.add(makeFrame(0x200, {0x02}));
    a.add(makeFrame(0x300, {0x03}));

    EXPECT_EQ(a.profiles().size(), 3);
    EXPECT_NE(a.profileFor(0x100), nullptr);
    EXPECT_NE(a.profileFor(0x200), nullptr);
    EXPECT_NE(a.profileFor(0x300), nullptr);
    EXPECT_EQ(a.profileFor(0x400), nullptr);
}

// ── profiles() sorted by ID ───────────────────────────────────────────────

TEST(CanBitAnalyzer, Profiles_SortedById) {
    CanBitAnalyzer a;
    a.add(makeFrame(0x300, {0x01}));
    a.add(makeFrame(0x100, {0x02}));
    a.add(makeFrame(0x200, {0x03}));

    auto profs = a.profiles();
    ASSERT_EQ(profs.size(), 3);
    EXPECT_LT(profs[0].id, profs[1].id);
    EXPECT_LT(profs[1].id, profs[2].id);
}

// ── frameCount accumulates ────────────────────────────────────────────────

TEST(CanBitAnalyzer, FrameCount_Accumulates) {
    CanBitAnalyzer a;
    for (int i = 0; i < 10; ++i)
        a.add(makeFrame(0x100, {static_cast<uint8_t>(i)}));

    const auto *p = a.profileFor(0x100);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->frameCount, 10u);
}

// ── maxDlc tracks largest DLC seen ───────────────────────────────────────

TEST(CanBitAnalyzer, MaxDlc_TracksLargest) {
    CanBitAnalyzer a;
    a.add(makeFrame(0x100, {0x01, 0x02}));          // dlc=2
    a.add(makeFrame(0x100, {0x01, 0x02, 0x03, 0x04})); // dlc=4

    const auto *p = a.profileFor(0x100);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->maxDlc, 4u);
}

// ── addAll() is equivalent to calling add() in a loop ────────────────────

TEST(CanBitAnalyzer, AddAll_EquivalentToLoop) {
    QVector<CanFrame> frames = {
        makeFrame(0x100, {0x00}),
        makeFrame(0x100, {0xFF}),
    };

    CanBitAnalyzer a1, a2;
    a1.addAll(frames);
    for (const auto &f : frames) a2.add(f);

    const auto *p1 = a1.profileFor(0x100);
    const auto *p2 = a2.profileFor(0x100);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(p1->frameCount, p2->frameCount);
    EXPECT_EQ(p1->alwaysZero[0], p2->alwaysZero[0]);
    EXPECT_EQ(p1->alwaysOne[0],  p2->alwaysOne[0]);
}

// ── reset() clears state ──────────────────────────────────────────────────

TEST(CanBitAnalyzer, Reset_ClearsAllState) {
    CanBitAnalyzer a;
    a.add(makeFrame(0x100, {0x01}));
    EXPECT_FALSE(a.profiles().isEmpty());

    a.reset();
    EXPECT_TRUE(a.profiles().isEmpty());
    EXPECT_EQ(a.profileFor(0x100), nullptr);
}

// ── hasVaryingBits ────────────────────────────────────────────────────────

TEST(CanBitAnalyzer, HasVaryingBits_True) {
    CanBitAnalyzer a;
    a.add(makeFrame(0x100, {0x00, 0xAA}));
    a.add(makeFrame(0x100, {0xFF, 0xAA}));

    const auto *p = a.profileFor(0x100);
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(p->hasVaryingBits(0));   // byte 0 varies
    EXPECT_FALSE(p->hasVaryingBits(1));  // byte 1 static
}

TEST(CanBitAnalyzer, HasVaryingBits_OutOfRange_False) {
    CanBitAnalyzer a;
    a.add(makeFrame(0x100, {0x01}));

    const auto *p = a.profileFor(0x100);
    ASSERT_NE(p, nullptr);
    EXPECT_FALSE(p->hasVaryingBits(-1));
    EXPECT_FALSE(p->hasVaryingBits(8));
}

// ── summary() contains ID and frame count ────────────────────────────────

TEST(CanBitAnalyzer, Summary_ContainsIdAndCount) {
    CanBitAnalyzer a;
    a.add(makeFrame(0x1AB, {0x00}));
    a.add(makeFrame(0x1AB, {0xFF}));

    const auto *p = a.profileFor(0x1AB);
    ASSERT_NE(p, nullptr);
    QString s = p->summary();
    EXPECT_TRUE(s.contains("1AB", Qt::CaseInsensitive) ||
                s.contains("0x1AB", Qt::CaseInsensitive));
    EXPECT_TRUE(s.contains("2"));
}

TEST(CanBitAnalyzer, Summary_StaticFrame_SaysStatic) {
    CanBitAnalyzer a;
    a.add(makeFrame(0x100, {0xAA, 0xBB}));
    a.add(makeFrame(0x100, {0xAA, 0xBB}));

    const auto *p = a.profileFor(0x100);
    ASSERT_NE(p, nullptr);
    QString s = p->summary();
    EXPECT_TRUE(s.contains("static", Qt::CaseInsensitive));
}

TEST(CanBitAnalyzer, Summary_VaryingFrame_ContainsByteInfo) {
    CanBitAnalyzer a;
    a.add(makeFrame(0x100, {0x00}));
    a.add(makeFrame(0x100, {0xFF}));

    const auto *p = a.profileFor(0x100);
    ASSERT_NE(p, nullptr);
    QString s = p->summary();
    EXPECT_TRUE(s.contains("B0"));  // byte 0 varies
}

// ── report() non-empty ────────────────────────────────────────────────────

TEST(CanBitAnalyzer, Report_ContainsIdEntries) {
    CanBitAnalyzer a;
    a.add(makeFrame(0x100, {0x01}));
    a.add(makeFrame(0x200, {0x02}));

    QString r = a.report();
    EXPECT_FALSE(r.isEmpty());
    EXPECT_TRUE(r.contains("100", Qt::CaseInsensitive));
    EXPECT_TRUE(r.contains("200", Qt::CaseInsensitive));
}

// ── extended flag preserved ───────────────────────────────────────────────

TEST(CanBitAnalyzer, ExtendedFlag_Preserved) {
    CanBitAnalyzer a;
    a.add(makeFrame(0x18DB00F1, {0x02}, true));

    const auto *p = a.profileFor(0x18DB00F1);
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(p->extended);
}

// ── Zero-DLC frame (empty payload) — no crash ────────────────────────────

TEST(CanBitAnalyzer, ZeroDlcFrame_NoCrash) {
    CanBitAnalyzer a;
    CanFrame f{};
    f.id  = 0x111;
    f.dlc = 0;
    EXPECT_NO_THROW(a.add(f));
    const auto *p = a.profileFor(0x111);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->frameCount, 1u);
    EXPECT_EQ(p->maxDlc, 0u);
}

// ── Many frames with same constant byte — always-one/zero persists ────────

TEST(CanBitAnalyzer, ManyFrames_ConstantByte_StaysAlwaysOne) {
    CanBitAnalyzer a;
    for (int i = 0; i < 50; ++i)
        a.add(makeFrame(0x100, {0xFF}));

    const auto *p = a.profileFor(0x100);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->alwaysOne[0],  0xFFu);
    EXPECT_EQ(p->alwaysZero[0], 0x00u);
    EXPECT_EQ(p->varying()[0],  0x00u);
}
