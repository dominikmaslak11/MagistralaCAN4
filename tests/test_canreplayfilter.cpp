#include <gtest/gtest.h>
#include "core/CanReplayFilter.h"

static CanFrame makeFrame(uint32_t id, std::initializer_list<uint8_t> data, bool ext = false) {
    CanFrame f;
    f.id       = id;
    f.extended = ext;
    f.dlc      = static_cast<uint8_t>(data.size());
    int i = 0;
    for (uint8_t b : data) f.data[i++] = b;
    return f;
}

// ── Passthrough (no rules) ────────────────────────────────────────────────────

TEST(CanReplayFilter, NoRulesPassthrough) {
    CanReplayFilter flt;
    CanFrame f = makeFrame(0x100, {0x01, 0x02});
    auto result = flt.apply(f);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->id, 0x100u);
    EXPECT_EQ(result->data[0], 0x01u);
}

// ── Drop ─────────────────────────────────────────────────────────────────────

TEST(CanReplayFilter, DropById) {
    CanReplayFilter flt;
    ReplayRule r;
    r.srcId = 0x200;
    r.drop  = true;
    flt.setRules({r});

    EXPECT_FALSE(flt.apply(makeFrame(0x200, {0x00})).has_value());
    EXPECT_TRUE( flt.apply(makeFrame(0x100, {0x00})).has_value()); // non-matching passes
}

TEST(CanReplayFilter, DropDoesNotAffectOtherIds) {
    CanReplayFilter flt;
    ReplayRule r;
    r.srcId = 0x300;
    r.drop  = true;
    flt.setRules({r});

    auto result = flt.apply(makeFrame(0x301, {0xAB}));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data[0], 0xABu);
}

// ── ID Remap ──────────────────────────────────────────────────────────────────

TEST(CanReplayFilter, RemapId) {
    CanReplayFilter flt;
    ReplayRule r;
    r.srcId    = 0x100;
    r.remapId  = true;
    r.dstId    = 0x200;
    flt.setRules({r});

    auto result = flt.apply(makeFrame(0x100, {0x00}));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->id, 0x200u);
}

TEST(CanReplayFilter, NoRemapWhenDisabled) {
    CanReplayFilter flt;
    ReplayRule r;
    r.srcId   = 0x100;
    r.remapId = false;
    r.dstId   = 0x200;
    flt.setRules({r});

    auto result = flt.apply(makeFrame(0x100, {0x00}));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->id, 0x100u); // unchanged
}

// ── Byte transform: scale ─────────────────────────────────────────────────────

TEST(CanReplayFilter, ScaleByte) {
    CanReplayFilter flt;
    ReplayRule r;
    r.srcId = 0x100;
    r.byteXforms[0].active = true;
    r.byteXforms[0].scale  = 2.0;
    r.byteXforms[0].offset = 0.0;
    flt.setRules({r});

    auto result = flt.apply(makeFrame(0x100, {0x10})); // 0x10=16 * 2 = 32 = 0x20
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data[0], 0x20u);
}

TEST(CanReplayFilter, OffsetByte) {
    CanReplayFilter flt;
    ReplayRule r;
    r.srcId = 0x100;
    r.byteXforms[0].active = true;
    r.byteXforms[0].scale  = 1.0;
    r.byteXforms[0].offset = 10.0;
    flt.setRules({r});

    auto result = flt.apply(makeFrame(0x100, {0x05})); // 5 + 10 = 15
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data[0], 15u);
}

TEST(CanReplayFilter, ClampOverflow) {
    CanReplayFilter flt;
    ReplayRule r;
    r.srcId = 0x100;
    r.byteXforms[0].active = true;
    r.byteXforms[0].scale  = 2.0;
    r.byteXforms[0].offset = 0.0;
    flt.setRules({r});

    auto result = flt.apply(makeFrame(0x100, {0xFF})); // 255 * 2 = 510 → clamped to 255
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data[0], 0xFFu);
}

TEST(CanReplayFilter, ClampUnderflow) {
    CanReplayFilter flt;
    ReplayRule r;
    r.srcId = 0x100;
    r.byteXforms[0].active = true;
    r.byteXforms[0].scale  = 1.0;
    r.byteXforms[0].offset = -100.0;
    flt.setRules({r});

    auto result = flt.apply(makeFrame(0x100, {0x05})); // 5 - 100 = -95 → clamped to 0
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data[0], 0u);
}

TEST(CanReplayFilter, InactiveBytesUnchanged) {
    CanReplayFilter flt;
    ReplayRule r;
    r.srcId = 0x100;
    r.byteXforms[0].active = true;
    r.byteXforms[0].scale  = 2.0;
    // byteXforms[1] left inactive
    flt.setRules({r});

    auto result = flt.apply(makeFrame(0x100, {0x10, 0xAB}));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data[0], 0x20u); // transformed
    EXPECT_EQ(result->data[1], 0xABu); // unchanged
}

// ── Batch ─────────────────────────────────────────────────────────────────────

TEST(CanReplayFilter, BatchDropsSelected) {
    CanReplayFilter flt;
    ReplayRule r;
    r.srcId = 0x200;
    r.drop  = true;
    flt.setRules({r});

    std::vector<CanFrame> batch = {
        makeFrame(0x100, {0x01}),
        makeFrame(0x200, {0x02}), // dropped
        makeFrame(0x300, {0x03}),
    };
    auto out = flt.applyBatch(batch);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].id, 0x100u);
    EXPECT_EQ(out[1].id, 0x300u);
}

// ── First-match wins ──────────────────────────────────────────────────────────

TEST(CanReplayFilter, FirstRuleWins) {
    CanReplayFilter flt;
    ReplayRule r1, r2;
    r1.srcId = 0x100; r1.remapId = true; r1.dstId = 0x111;
    r2.srcId = 0x100; r2.remapId = true; r2.dstId = 0x222;
    flt.setRules({r1, r2});

    auto result = flt.apply(makeFrame(0x100, {0x00}));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->id, 0x111u); // first rule wins
}

// ── Clear rules ───────────────────────────────────────────────────────────────

TEST(CanReplayFilter, ClearRules) {
    CanReplayFilter flt;
    ReplayRule r; r.srcId = 0x100; r.drop = true;
    flt.setRules({r});
    flt.clearRules();

    auto result = flt.apply(makeFrame(0x100, {0x00}));
    ASSERT_TRUE(result.has_value()); // no rules → passthrough
}

// ── Extended frame flag matching ──────────────────────────────────────────────

TEST(CanReplayFilter, UseExtMatchesOnlyExtendedFrames) {
    CanReplayFilter flt;
    ReplayRule r;
    r.srcId  = 0x100;
    r.useExt = true;
    r.drop   = true;
    flt.setRules({r});

    // Standard frame: same ID, not extended → no match → passes through
    EXPECT_TRUE(flt.apply(makeFrame(0x100, {0x01}, false)).has_value());
    // Extended frame: same ID, extended → match → dropped
    EXPECT_FALSE(flt.apply(makeFrame(0x100, {0x01}, true)).has_value());
}

TEST(CanReplayFilter, UseExtFalseMatchesBothExtendedAndStandard) {
    CanReplayFilter flt;
    ReplayRule r;
    r.srcId  = 0x100;
    r.useExt = false; // does not discriminate by extended flag
    r.drop   = true;
    flt.setRules({r});

    EXPECT_FALSE(flt.apply(makeFrame(0x100, {0x01}, false)).has_value());
    EXPECT_FALSE(flt.apply(makeFrame(0x100, {0x01}, true)).has_value());
}

// ── Scale + offset combined ───────────────────────────────────────────────────

TEST(CanReplayFilter, ScaleAndOffsetCombined) {
    CanReplayFilter flt;
    ReplayRule r;
    r.srcId = 0x100;
    r.byteXforms[0].active = true;
    r.byteXforms[0].scale  = 2.0;
    r.byteXforms[0].offset = 5.0;
    flt.setRules({r});

    // 10 * 2 + 5 = 25
    auto result = flt.apply(makeFrame(0x100, {10}));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data[0], 25u);
}

TEST(CanReplayFilter, LastByteTransformed) {
    CanReplayFilter flt;
    ReplayRule r;
    r.srcId = 0x100;
    r.byteXforms[7].active = true;
    r.byteXforms[7].scale  = 3.0;
    r.byteXforms[7].offset = 0.0;
    flt.setRules({r});

    auto result = flt.apply(makeFrame(0x100, {0,0,0,0,0,0,0,10})); // byte[7]=10 * 3 = 30
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->data[7], 30u);
    EXPECT_EQ(result->data[0], 0u); // first byte unchanged
}

// ── DLC preserved ─────────────────────────────────────────────────────────────

TEST(CanReplayFilter, DlcPreservedAfterTransform) {
    CanReplayFilter flt;
    ReplayRule r;
    r.srcId = 0x100;
    r.byteXforms[0].active = true;
    r.byteXforms[0].scale  = 2.0;
    flt.setRules({r});

    auto result = flt.apply(makeFrame(0x100, {1, 2, 3}));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->dlc, 3u);
}

// ── rules() accessor ─────────────────────────────────────────────────────────

TEST(CanReplayFilter, RulesAccessorReturnsCurrentRules) {
    CanReplayFilter flt;
    EXPECT_TRUE(flt.rules().empty());

    ReplayRule r1; r1.srcId = 0x100;
    ReplayRule r2; r2.srcId = 0x200;
    flt.setRules({r1, r2});
    EXPECT_EQ(flt.rules().size(), 2u);

    flt.clearRules();
    EXPECT_TRUE(flt.rules().empty());
}
