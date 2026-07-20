/**
 * @file test_canpayloadsearch.cpp
 * @brief Unit tests for CanPayloadSearch – byte pattern search in CAN recordings.
 */
#include <gtest/gtest.h>
#include "core/CanPayloadSearch.h"

static CanFrame makeFrame(uint32_t id, std::initializer_list<uint8_t> bytes) {
    CanFrame f{};
    f.id  = id;
    f.dlc = static_cast<uint8_t>(bytes.size());
    int i = 0;
    for (uint8_t b : bytes) f.data[i++] = b;
    return f;
}

// ── matches() ────────────────────────────────────────────────────────────────

TEST(CanPayloadSearch, EmptyPattern_NoMatch) {
    CanPayloadSearch::Query q;
    // empty pattern → never matches
    EXPECT_FALSE(CanPayloadSearch::matches(makeFrame(0x1, {0xAA}), q));
}

TEST(CanPayloadSearch, SingleByteAtStart) {
    CanPayloadSearch::Query q;
    q.pattern = {0xAA};
    EXPECT_TRUE(CanPayloadSearch::matches(makeFrame(0x1, {0xAA, 0xBB}), q));
}

TEST(CanPayloadSearch, SingleByteAtEnd) {
    CanPayloadSearch::Query q;
    q.pattern = {0xBB};
    EXPECT_TRUE(CanPayloadSearch::matches(makeFrame(0x1, {0xAA, 0xBB}), q));
}

TEST(CanPayloadSearch, PatternNotPresent) {
    CanPayloadSearch::Query q;
    q.pattern = {0xCC};
    EXPECT_FALSE(CanPayloadSearch::matches(makeFrame(0x1, {0xAA, 0xBB}), q));
}

TEST(CanPayloadSearch, MultiBytePatternFound) {
    CanPayloadSearch::Query q;
    q.pattern = {0x01, 0x02, 0x03};
    EXPECT_TRUE(CanPayloadSearch::matches(makeFrame(0x1, {0xFF, 0x01, 0x02, 0x03, 0xFF}), q));
}

TEST(CanPayloadSearch, MultiBytePattern_Partial_NotFound) {
    CanPayloadSearch::Query q;
    q.pattern = {0x01, 0x02, 0x03};
    // Only first two bytes match
    EXPECT_FALSE(CanPayloadSearch::matches(makeFrame(0x1, {0x01, 0x02, 0xFF}), q));
}

// ── Masked match ──────────────────────────────────────────────────────────────

TEST(CanPayloadSearch, MaskedMatch_DontCareLowNibble) {
    CanPayloadSearch::Query q;
    q.pattern = {0xA0};
    q.mask    = {0xF0}; // only high nibble matters
    EXPECT_TRUE(CanPayloadSearch::matches(makeFrame(0x1, {0xAB}), q)); // 0xAB & 0xF0 == 0xA0
    EXPECT_FALSE(CanPayloadSearch::matches(makeFrame(0x1, {0xBB}), q)); // 0xBB & 0xF0 == 0xB0
}

TEST(CanPayloadSearch, MaskedMatch_AllDontCare) {
    CanPayloadSearch::Query q;
    q.pattern = {0x00};
    q.mask    = {0x00}; // any byte matches
    EXPECT_TRUE(CanPayloadSearch::matches(makeFrame(0x1, {0xFF}), q));
    EXPECT_TRUE(CanPayloadSearch::matches(makeFrame(0x1, {0x00}), q));
}

// ── ID filter ─────────────────────────────────────────────────────────────────

TEST(CanPayloadSearch, IdFilter_MatchesCorrectId) {
    CanPayloadSearch::Query q;
    q.pattern    = {0x42};
    q.filterById = true;
    q.canId      = 0x200;
    EXPECT_TRUE(CanPayloadSearch::matches(makeFrame(0x200, {0x42}), q));
    EXPECT_FALSE(CanPayloadSearch::matches(makeFrame(0x100, {0x42}), q));
}

// ── search() ─────────────────────────────────────────────────────────────────

TEST(CanPayloadSearch, Search_EmptyFrameList) {
    CanPayloadSearch::Query q;
    q.pattern = {0xFF};
    EXPECT_TRUE(CanPayloadSearch::search({}, q).empty());
}

TEST(CanPayloadSearch, Search_FindsCorrectFrameIndex) {
    std::vector<CanFrame> frames = {
        makeFrame(0x100, {0x01, 0x02}),
        makeFrame(0x200, {0xDE, 0xAD}),
        makeFrame(0x300, {0xAA, 0xBB}),
    };
    CanPayloadSearch::Query q;
    q.pattern = {0xDE, 0xAD};
    auto results = CanPayloadSearch::search(frames, q);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].frameIndex, 1u);
    EXPECT_EQ(results[0].canId, 0x200u);
    EXPECT_EQ(results[0].byteOffset, 0);
}

TEST(CanPayloadSearch, Search_ReportsCorrectByteOffset) {
    std::vector<CanFrame> frames = {makeFrame(0x100, {0x00, 0x00, 0xAB, 0xCD})};
    CanPayloadSearch::Query q;
    q.pattern = {0xAB, 0xCD};
    auto results = CanPayloadSearch::search(frames, q);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].byteOffset, 2);
}

TEST(CanPayloadSearch, Search_MaxResults) {
    std::vector<CanFrame> frames;
    for (int i = 0; i < 10; ++i)
        frames.push_back(makeFrame(0x100, {0xFF}));

    CanPayloadSearch::Query q;
    q.pattern    = {0xFF};
    q.maxResults = 3;
    auto results = CanPayloadSearch::search(frames, q);
    EXPECT_EQ(results.size(), 3u);
}

TEST(CanPayloadSearch, Search_IdFilter) {
    std::vector<CanFrame> frames = {
        makeFrame(0x100, {0xFF}),
        makeFrame(0x200, {0xFF}),
        makeFrame(0x100, {0xFF}),
    };
    CanPayloadSearch::Query q;
    q.pattern    = {0xFF};
    q.filterById = true;
    q.canId      = 0x100;
    auto results = CanPayloadSearch::search(frames, q);
    EXPECT_EQ(results.size(), 2u);
    for (auto &r : results) EXPECT_EQ(r.canId, 0x100u);
}

// ── filterFrames() ────────────────────────────────────────────────────────────

TEST(CanPayloadSearch, FilterFrames_ReturnsMatchingCopies) {
    std::vector<CanFrame> frames = {
        makeFrame(0x100, {0xAA}),
        makeFrame(0x200, {0xBB}),
        makeFrame(0x300, {0xAA}),
    };
    CanPayloadSearch::Query q;
    q.pattern = {0xAA};
    auto out = CanPayloadSearch::filterFrames(frames, q);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].id, 0x100u);
    EXPECT_EQ(out[1].id, 0x300u);
}

TEST(CanPayloadSearch, FilterFrames_EmptyWhenNoMatch) {
    std::vector<CanFrame> frames = {makeFrame(0x1, {0x00, 0x00})};
    CanPayloadSearch::Query q;
    q.pattern = {0xFF};
    EXPECT_TRUE(CanPayloadSearch::filterFrames(frames, q).empty());
}

// ── PatternLargerThanDlc ──────────────────────────────────────────────────────

TEST(CanPayloadSearch, PatternLargerThanDlc_NoMatch) {
    CanPayloadSearch::Query q;
    q.pattern = {0x01, 0x02, 0x03, 0x04, 0x05};
    // Frame has only 3 bytes
    EXPECT_FALSE(CanPayloadSearch::matches(makeFrame(0x1, {0x01, 0x02, 0x03}), q));
}
