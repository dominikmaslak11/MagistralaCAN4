#include <gtest/gtest.h>
#include "core/CanFrameDiff.h"
#include "core/CanFrame.h"
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

static QVector<CanFrame> frames(std::initializer_list<CanFrame> list) {
    QVector<CanFrame> v;
    for (const auto &f : list) v.append(f);
    return v;
}

// ── Both empty → no differences ───────────────────────────────────────────

TEST(CanFrameDiff, BothEmpty_NoDifferences) {
    CanFrameDiff diff;
    diff.setRecordingA({});
    diff.setRecordingB({});
    EXPECT_TRUE(diff.compute().isEmpty());
}

// ── Same single frame → no differences ───────────────────────────────────

TEST(CanFrameDiff, SameFrame_NoDifferences) {
    CanFrameDiff diff;
    auto f = makeFrame(0x100, {0x01, 0x02});
    diff.setRecordingA(frames({f}));
    diff.setRecordingB(frames({f}));
    EXPECT_TRUE(diff.compute().isEmpty());
}

// ── ID in B but not A → Added ─────────────────────────────────────────────

TEST(CanFrameDiff, IdOnlyInB_IsAdded) {
    CanFrameDiff diff;
    diff.setRecordingA({});
    diff.setRecordingB(frames({makeFrame(0x200, {0xAA})}));

    auto result = diff.compute();
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].change, CanDiffEntry::Added);
    EXPECT_EQ(result[0].id, 0x200u);
    EXPECT_EQ(result[0].countA, 0u);
    EXPECT_EQ(result[0].countB, 1u);
}

// ── ID in A but not B → Removed ──────────────────────────────────────────

TEST(CanFrameDiff, IdOnlyInA_IsRemoved) {
    CanFrameDiff diff;
    diff.setRecordingA(frames({makeFrame(0x300, {0xBB})}));
    diff.setRecordingB({});

    auto result = diff.compute();
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].change, CanDiffEntry::Removed);
    EXPECT_EQ(result[0].id, 0x300u);
    EXPECT_EQ(result[0].countA, 1u);
    EXPECT_EQ(result[0].countB, 0u);
}

// ── Byte value range changed → ByteChanged ───────────────────────────────

TEST(CanFrameDiff, ByteValueChanged_IsDetected) {
    CanFrameDiff diff;
    diff.setRecordingA(frames({makeFrame(0x100, {0x00, 0x00})}));
    diff.setRecordingB(frames({makeFrame(0x100, {0xFF, 0x00})}));

    auto result = diff.compute();
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].change, CanDiffEntry::ByteChanged);
    EXPECT_EQ(result[0].id, 0x100u);
    // Byte 0 changed, byte 1 did not
    EXPECT_TRUE(result[0].changedBytesMask & 0x01);
    EXPECT_FALSE(result[0].changedBytesMask & 0x02);
}

// ── Multiple bytes changed → correct mask ───────────────────────────────

TEST(CanFrameDiff, MultipleBytesChanged_CorrectMask) {
    CanFrameDiff diff;
    diff.setRecordingA(frames({makeFrame(0x100, {0x00, 0x00, 0x00, 0x00})}));
    diff.setRecordingB(frames({makeFrame(0x100, {0xFF, 0x00, 0xAA, 0x00})}));

    auto result = diff.compute();
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].change, CanDiffEntry::ByteChanged);
    uint8_t mask = result[0].changedBytesMask;
    EXPECT_TRUE(mask & (1u << 0));   // byte 0 changed
    EXPECT_FALSE(mask & (1u << 1));  // byte 1 unchanged
    EXPECT_TRUE(mask & (1u << 2));   // byte 2 changed
    EXPECT_FALSE(mask & (1u << 3));  // byte 3 unchanged
}

// ── Identical byte ranges → no change entry ──────────────────────────────

TEST(CanFrameDiff, SameByteRanges_NoDifference) {
    CanFrameDiff diff;
    // Two frames each in A and B; same min/max per byte
    diff.setRecordingA(frames({
        makeFrame(0x100, {0x10, 0x20}),
        makeFrame(0x100, {0x30, 0x40}),
    }));
    diff.setRecordingB(frames({
        makeFrame(0x100, {0x10, 0x20}),
        makeFrame(0x100, {0x30, 0x40}),
    }));
    EXPECT_TRUE(diff.compute().isEmpty());
}

// ── Result sorted by ID ───────────────────────────────────────────────────

TEST(CanFrameDiff, OutputSortedById) {
    CanFrameDiff diff;
    diff.setRecordingA(frames({makeFrame(0x300, {0x01})}));
    diff.setRecordingB(frames({
        makeFrame(0x100, {0x01}),
        makeFrame(0x200, {0x01}),
        makeFrame(0x300, {0xFF}),
    }));

    auto result = diff.compute();
    ASSERT_EQ(result.size(), 3);  // 0x100 Added, 0x200 Added, 0x300 Changed
    EXPECT_LT(result[0].id, result[1].id);
    EXPECT_LT(result[1].id, result[2].id);
}

// ── Mixed: added + removed + changed ────────────────────────────────────

TEST(CanFrameDiff, Mixed_AddedRemovedChanged) {
    CanFrameDiff diff;
    diff.setRecordingA(frames({
        makeFrame(0x100, {0x01}),          // will be removed
        makeFrame(0x200, {0x00}),          // will be changed
    }));
    diff.setRecordingB(frames({
        makeFrame(0x200, {0xFF}),          // changed
        makeFrame(0x300, {0x55}),          // added
    }));

    auto result = diff.compute();
    ASSERT_EQ(result.size(), 3);

    // Sorted: 0x100, 0x200, 0x300
    EXPECT_EQ(result[0].id, 0x100u);
    EXPECT_EQ(result[0].change, CanDiffEntry::Removed);

    EXPECT_EQ(result[1].id, 0x200u);
    EXPECT_EQ(result[1].change, CanDiffEntry::ByteChanged);

    EXPECT_EQ(result[2].id, 0x300u);
    EXPECT_EQ(result[2].change, CanDiffEntry::Added);
}

// ── frame counts captured correctly ─────────────────────────────────────

TEST(CanFrameDiff, FrameCountsStoredInSummary) {
    CanFrameDiff diff;
    diff.setRecordingA(frames({
        makeFrame(0x100, {0x01}),
        makeFrame(0x100, {0x02}),
        makeFrame(0x100, {0x03}),
    }));
    diff.setRecordingB(frames({
        makeFrame(0x100, {0x01}),
        makeFrame(0x100, {0xFF}),  // this extends the max range
    }));

    auto result = diff.compute();
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].countA, 3u);
    EXPECT_EQ(result[0].countB, 2u);
}

// ── summariesA / summariesB accessible ───────────────────────────────────

TEST(CanFrameDiff, SummariesAccessors) {
    CanFrameDiff diff;
    diff.setRecordingA(frames({makeFrame(0x111, {0x0A, 0x0B})}));
    diff.setRecordingB(frames({makeFrame(0x222, {0xCC})}));

    const auto &sA = diff.summariesA();
    const auto &sB = diff.summariesB();

    EXPECT_TRUE(sA.contains(0x111));
    EXPECT_FALSE(sA.contains(0x222));

    EXPECT_TRUE(sB.contains(0x222));
    EXPECT_FALSE(sB.contains(0x111));

    EXPECT_EQ(sA[0x111].maxDlc, 2u);
    EXPECT_EQ(sA[0x111].byteMin[0], 0x0Au);
    EXPECT_EQ(sA[0x111].byteMax[0], 0x0Au);
}

// ── byteMin / byteMax built across multiple frames ────────────────────────

TEST(CanFrameDiff, ByteMinMaxAcrossFrames) {
    CanFrameDiff diff;
    diff.setRecordingA(frames({
        makeFrame(0x100, {0x10}),
        makeFrame(0x100, {0x50}),
        makeFrame(0x100, {0x30}),
    }));
    diff.setRecordingB(frames({makeFrame(0x100, {0x10})}));

    const auto &s = diff.summariesA()[0x100];
    EXPECT_EQ(s.byteMin[0], 0x10u);
    EXPECT_EQ(s.byteMax[0], 0x50u);
}

// ── Range widened in B → ByteChanged ─────────────────────────────────────

TEST(CanFrameDiff, RangeWidenedInB_IsChanged) {
    CanFrameDiff diff;
    // A: byte0 range [0x20..0x20]
    diff.setRecordingA(frames({makeFrame(0x100, {0x20})}));
    // B: byte0 range [0x10..0x30] — different min and max
    diff.setRecordingB(frames({
        makeFrame(0x100, {0x10}),
        makeFrame(0x100, {0x30}),
    }));

    auto result = diff.compute();
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].change, CanDiffEntry::ByteChanged);
    EXPECT_TRUE(result[0].changedBytesMask & 0x01);
}

// ── Extended flag preserved in summary ───────────────────────────────────

TEST(CanFrameDiff, ExtendedFlagPreserved) {
    CanFrameDiff diff;
    diff.setRecordingA(frames({makeFrame(0x18DB00F1, {0x01}, true)}));
    diff.setRecordingB({});

    auto result = diff.compute();
    ASSERT_EQ(result.size(), 1);
    EXPECT_TRUE(result[0].extended);
}

// ── description() text ───────────────────────────────────────────────────

TEST(CanFrameDiff, Description_Added) {
    CanDiffEntry e;
    e.id     = 0x100;
    e.change = CanDiffEntry::Added;
    e.countB = 5;
    QString d = e.description();
    EXPECT_TRUE(d.contains("ADDED",   Qt::CaseInsensitive));
    EXPECT_TRUE(d.contains("0x100",   Qt::CaseInsensitive) ||
                d.contains("100",     Qt::CaseInsensitive));
}

TEST(CanFrameDiff, Description_Removed) {
    CanDiffEntry e;
    e.id     = 0x200;
    e.change = CanDiffEntry::Removed;
    e.countA = 3;
    QString d = e.description();
    EXPECT_TRUE(d.contains("REMOVED", Qt::CaseInsensitive));
}

TEST(CanFrameDiff, Description_ByteChanged) {
    CanDiffEntry e;
    e.id               = 0x300;
    e.change           = CanDiffEntry::ByteChanged;
    e.changedBytesMask = 0b00000101;  // bytes 0 and 2
    e.countA           = 10;
    e.countB           = 8;
    QString d = e.description();
    EXPECT_TRUE(d.contains("CHANGED", Qt::CaseInsensitive));
    EXPECT_TRUE(d.contains("B0"));
    EXPECT_TRUE(d.contains("B2"));
    EXPECT_FALSE(d.contains("B1"));
}

// ── report() output ──────────────────────────────────────────────────────

TEST(CanFrameDiff, Report_NoDifferences) {
    CanFrameDiff diff;
    diff.setRecordingA({});
    diff.setRecordingB({});
    EXPECT_TRUE(diff.report().contains("No differences"));
}

TEST(CanFrameDiff, Report_ContainsSummaryLine) {
    CanFrameDiff diff;
    diff.setRecordingA(frames({makeFrame(0x100, {0x01})}));
    diff.setRecordingB(frames({makeFrame(0x200, {0x02})}));

    QString r = diff.report();
    EXPECT_TRUE(r.contains("added",   Qt::CaseInsensitive));
    EXPECT_TRUE(r.contains("removed", Qt::CaseInsensitive));
}

// ── Compute is const (can call twice, same result) ────────────────────────

TEST(CanFrameDiff, ComputeIsIdempotent) {
    CanFrameDiff diff;
    diff.setRecordingA(frames({makeFrame(0x100, {0x00})}));
    diff.setRecordingB(frames({makeFrame(0x100, {0xFF})}));

    auto r1 = diff.compute();
    auto r2 = diff.compute();
    ASSERT_EQ(r1.size(), r2.size());
    EXPECT_EQ(r1[0].id,     r2[0].id);
    EXPECT_EQ(r1[0].change, r2[0].change);
}

// ── DLC 0 frame (empty payload) handled without crash ────────────────────

TEST(CanFrameDiff, EmptyPayloadFrame_NoCrash) {
    CanFrameDiff diff;
    CanFrame f{};
    f.id  = 0x123;
    f.dlc = 0;
    diff.setRecordingA(frames({f}));
    diff.setRecordingB(frames({f}));
    EXPECT_NO_THROW(diff.compute());
    EXPECT_TRUE(diff.compute().isEmpty());
}

// ── Many IDs, check summary count matches ────────────────────────────────

TEST(CanFrameDiff, ManySummaries_CountCorrect) {
    QVector<CanFrame> framesA, framesB;
    for (int i = 0; i < 20; ++i)
        framesA.append(makeFrame(i * 0x10, {static_cast<uint8_t>(i)}));
    for (int i = 10; i < 30; ++i)
        framesB.append(makeFrame(i * 0x10, {static_cast<uint8_t>(i)}));

    CanFrameDiff diff;
    diff.setRecordingA(framesA);
    diff.setRecordingB(framesB);

    auto result = diff.compute();
    int added = 0, removed = 0;
    for (const auto &e : result) {
        if (e.change == CanDiffEntry::Added)   ++added;
        if (e.change == CanDiffEntry::Removed) ++removed;
    }
    // IDs 0..9 in A only → 10 removed; IDs 20..29 in B only → 10 added
    EXPECT_EQ(removed, 10);
    EXPECT_EQ(added, 10);
}
