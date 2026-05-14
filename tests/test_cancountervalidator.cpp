/**
 * @file test_cancountervalidator.cpp
 * @brief Unit tests for CanCounterValidator – rolling counter validation.
 */
#include <gtest/gtest.h>
#include "core/CanCounterValidator.h"

using Result = CanCounterValidator::Result;

static CanFrame makeFrame(uint32_t id, uint8_t counterByte, int byteIdx = 0, uint8_t dlc = 4) {
    CanFrame f{};
    f.id  = id;
    f.dlc = dlc;
    f.data[byteIdx] = counterByte;
    return f;
}

// ── Initial state ─────────────────────────────────────────────────────────────

TEST(CanCounterValidator, InitiallyNoConfigs) {
    CanCounterValidator v;
    EXPECT_EQ(v.activeConfigCount(), 0);
}

TEST(CanCounterValidator, NoConfigForId_ReturnsOk) {
    CanCounterValidator v;
    // Frame for unconfigured ID → always Ok (ignored)
    EXPECT_EQ(v.update(makeFrame(0x100, 0)), Result::Ok);
}

// ── First frame → Skip ────────────────────────────────────────────────────────

TEST(CanCounterValidator, FirstFrame_ReturnsSkip) {
    CanCounterValidator v;
    CanCounterValidator::Config cfg;
    cfg.canId     = 0x100;
    cfg.byteIndex = 0;
    cfg.modulus   = 16;
    v.addConfig(cfg);

    EXPECT_EQ(v.update(makeFrame(0x100, 5)), Result::Skip);
}

// ── Sequential counter → Ok ───────────────────────────────────────────────────

TEST(CanCounterValidator, SequentialCounter_AllOk) {
    CanCounterValidator v;
    CanCounterValidator::Config cfg;
    cfg.canId     = 0x200;
    cfg.byteIndex = 0;
    cfg.modulus   = 16;
    v.addConfig(cfg);

    v.update(makeFrame(0x200, 0)); // Skip
    for (uint8_t i = 1; i < 8; ++i)
        EXPECT_EQ(v.update(makeFrame(0x200, i)), Result::Ok) << "at counter=" << (int)i;
}

// ── Wrap-around ───────────────────────────────────────────────────────────────

TEST(CanCounterValidator, CounterWraps_OkAfterWrap) {
    CanCounterValidator v;
    CanCounterValidator::Config cfg;
    cfg.canId     = 0x300;
    cfg.byteIndex = 0;
    cfg.modulus   = 4; // wraps at 4: 0,1,2,3,0,1,...
    v.addConfig(cfg);

    v.update(makeFrame(0x300, 0));  // Skip
    EXPECT_EQ(v.update(makeFrame(0x300, 1)), Result::Ok);
    EXPECT_EQ(v.update(makeFrame(0x300, 2)), Result::Ok);
    EXPECT_EQ(v.update(makeFrame(0x300, 3)), Result::Ok);
    EXPECT_EQ(v.update(makeFrame(0x300, 0)), Result::Ok); // wrap
    EXPECT_EQ(v.update(makeFrame(0x300, 1)), Result::Ok);
}

// ── Mismatch ──────────────────────────────────────────────────────────────────

TEST(CanCounterValidator, MissingFrame_ReturnsMismatch) {
    CanCounterValidator v;
    CanCounterValidator::Config cfg;
    cfg.canId     = 0x400;
    cfg.byteIndex = 0;
    cfg.modulus   = 16;
    v.addConfig(cfg);

    v.update(makeFrame(0x400, 0));  // Skip
    EXPECT_EQ(v.update(makeFrame(0x400, 1)), Result::Ok);
    // Skip counter 2, send 3 → mismatch
    EXPECT_EQ(v.update(makeFrame(0x400, 3)), Result::Mismatch);
}

TEST(CanCounterValidator, AfterMismatch_Resyncs) {
    CanCounterValidator v;
    CanCounterValidator::Config cfg;
    cfg.canId     = 0x500;
    cfg.byteIndex = 0;
    cfg.modulus   = 16;
    v.addConfig(cfg);

    v.update(makeFrame(0x500, 0));  // Skip
    v.update(makeFrame(0x500, 5));  // Mismatch (expected 1, got 5) → re-sync to 6
    EXPECT_EQ(v.update(makeFrame(0x500, 6)), Result::Ok); // re-synced
}

// ── Upper nibble mode ─────────────────────────────────────────────────────────

TEST(CanCounterValidator, UpperNibble_Extracted) {
    CanCounterValidator v;
    CanCounterValidator::Config cfg;
    cfg.canId       = 0x600;
    cfg.byteIndex   = 0;
    cfg.upperNibble = true;
    cfg.modulus     = 16;
    v.addConfig(cfg);

    // Upper nibble of 0x30 = 3
    CanFrame f0; f0.id=0x600; f0.dlc=4; f0.data[0]=0x30; // counter=3
    v.update(f0); // Skip

    // Upper nibble of 0x40 = 4
    CanFrame f1; f1.id=0x600; f1.dlc=4; f1.data[0]=0x40;
    EXPECT_EQ(v.update(f1), Result::Ok); // expected 4
}

TEST(CanCounterValidator, UpperNibble_MismatchDetected) {
    CanCounterValidator v;
    CanCounterValidator::Config cfg;
    cfg.canId       = 0x700;
    cfg.byteIndex   = 0;
    cfg.upperNibble = true;
    cfg.modulus     = 16;
    v.addConfig(cfg);

    CanFrame f0; f0.id=0x700; f0.dlc=4; f0.data[0]=0x10; // counter nibble=1
    v.update(f0); // Skip → expects 2

    CanFrame f1; f1.id=0x700; f1.dlc=4; f1.data[0]=0x50; // counter nibble=5 (expected 2)
    EXPECT_EQ(v.update(f1), Result::Mismatch);
}

// ── OutOfRange ────────────────────────────────────────────────────────────────

TEST(CanCounterValidator, OutOfRange_WhenByteIndexGeqDlc) {
    CanCounterValidator v;
    CanCounterValidator::Config cfg;
    cfg.canId     = 0x800;
    cfg.byteIndex = 7; // high byte
    cfg.modulus   = 16;
    v.addConfig(cfg);

    CanFrame f; f.id=0x800; f.dlc=4; // dlc=4, byte 7 out of range
    EXPECT_EQ(v.update(f), Result::OutOfRange);
}

// ── Stats ─────────────────────────────────────────────────────────────────────

TEST(CanCounterValidator, Stats_Accumulated) {
    CanCounterValidator v;
    CanCounterValidator::Config cfg;
    cfg.canId     = 0x900;
    cfg.byteIndex = 0;
    cfg.modulus   = 16;
    v.addConfig(cfg);

    v.update(makeFrame(0x900, 0));  // Skip (counted as ok)
    v.update(makeFrame(0x900, 1));  // Ok
    v.update(makeFrame(0x900, 2));  // Ok
    v.update(makeFrame(0x900, 9));  // Mismatch

    auto s = v.statsFor(0x900);
    EXPECT_EQ(s.totalFrames,   4u);
    EXPECT_EQ(s.mismatchCount, 1u);
    EXPECT_EQ(s.okCount,       3u);
}

TEST(CanCounterValidator, Stats_UnknownId_Empty) {
    CanCounterValidator v;
    auto s = v.statsFor(0xDEAD);
    EXPECT_EQ(s.totalFrames, 0u);
}

// ── reset() ───────────────────────────────────────────────────────────────────

TEST(CanCounterValidator, Reset_ClearsState) {
    CanCounterValidator v;
    CanCounterValidator::Config cfg;
    cfg.canId     = 0xA00;
    cfg.byteIndex = 0;
    cfg.modulus   = 16;
    v.addConfig(cfg);

    v.update(makeFrame(0xA00, 5));  // Skip
    v.update(makeFrame(0xA00, 6));  // Ok

    v.reset();

    // After reset, first frame is Skip again
    EXPECT_EQ(v.update(makeFrame(0xA00, 0)), Result::Skip);
    // Stats cleared
    EXPECT_EQ(v.statsFor(0xA00).mismatchCount, 0u);
}

// ── removeConfig ──────────────────────────────────────────────────────────────

TEST(CanCounterValidator, RemoveConfig_IgnoresFrameAfter) {
    CanCounterValidator v;
    CanCounterValidator::Config cfg;
    cfg.canId     = 0xB00;
    cfg.byteIndex = 0;
    cfg.modulus   = 16;
    v.addConfig(cfg);

    EXPECT_EQ(v.activeConfigCount(), 1);
    v.removeConfig(0xB00);
    EXPECT_EQ(v.activeConfigCount(), 0);

    // Frame for now-removed ID → Ok (ignored)
    EXPECT_EQ(v.update(makeFrame(0xB00, 0)), Result::Ok);
}

// ── Multiple IDs independently tracked ───────────────────────────────────────

TEST(CanCounterValidator, MultipleIds_IndependentState) {
    CanCounterValidator v;
    for (uint32_t id : {0xC00u, 0xD00u}) {
        CanCounterValidator::Config cfg;
        cfg.canId     = id;
        cfg.byteIndex = 0;
        cfg.modulus   = 16;
        v.addConfig(cfg);
    }

    v.update(makeFrame(0xC00, 0)); // C00: Skip
    v.update(makeFrame(0xD00, 7)); // D00: Skip

    EXPECT_EQ(v.update(makeFrame(0xC00, 1)), Result::Ok);     // C00: ok
    EXPECT_EQ(v.update(makeFrame(0xD00, 8)), Result::Ok);     // D00: ok
    EXPECT_EQ(v.update(makeFrame(0xC00, 5)), Result::Mismatch); // C00: mismatch
    EXPECT_EQ(v.update(makeFrame(0xD00, 9)), Result::Ok);       // D00: unaffected
}
