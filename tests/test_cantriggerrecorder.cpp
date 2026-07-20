/**
 * @file test_cantriggerrecorder.cpp
 * @brief Unit tests for CanTriggerRecorder – pre/post trigger frame capture.
 */
#include <gtest/gtest.h>
#include "core/CanTriggerRecorder.h"

static CanFrame f(uint32_t id, uint8_t byte0 = 0) {
    CanFrame fr{};
    fr.id = id; fr.dlc = 1; fr.data[0] = byte0;
    return fr;
}

// ── Default state ──────────────────────────────────────────────────────────────

TEST(CanTriggerRecorder, InitialState_NotTriggered) {
    CanTriggerRecorder r;
    EXPECT_FALSE(r.triggered());
    EXPECT_FALSE(r.captureComplete());
    EXPECT_TRUE(r.capture().empty());
    EXPECT_EQ(r.remainingPost(), 0);
}

// ── Trigger without pre or post ────────────────────────────────────────────────

TEST(CanTriggerRecorder, TriggerFires_NoPreNoPost) {
    CanTriggerRecorder r;
    r.setTrigger([](const CanFrame &fr){ return fr.data[0] == 0xFF; });

    EXPECT_FALSE(r.addFrame(f(0x100, 0x00)));
    EXPECT_FALSE(r.triggered());

    EXPECT_TRUE(r.addFrame(f(0x100, 0xFF)));  // fires
    EXPECT_TRUE(r.triggered());
    EXPECT_TRUE(r.captureComplete());
}

TEST(CanTriggerRecorder, TriggerFires_CaptureContainsTriggerFrame) {
    CanTriggerRecorder r;
    r.setTrigger([](const CanFrame &fr){ return fr.id == 0x200u; });

    r.addFrame(f(0x100));
    r.addFrame(f(0x200));  // trigger
    const auto &cap = r.capture();
    ASSERT_GE(cap.size(), 1u);
    // Last element in capture should be the trigger frame (no pre)
    EXPECT_EQ(cap.back().id, 0x200u);
}

// ── Pre-trigger buffer ─────────────────────────────────────────────────────────

TEST(CanTriggerRecorder, PreBuffer_ContainsNFramesBeforeTrigger) {
    CanTriggerRecorder r;
    r.setPreCount(3);
    r.setTrigger([](const CanFrame &fr){ return fr.id == 0xFFFu; });

    for (uint32_t i = 0; i < 5; ++i) r.addFrame(f(i));  // 0,1,2,3,4
    r.addFrame(f(0xFFFu));  // trigger

    const auto &cap = r.capture();
    // Capture should have last 3 pre-frames (2,3,4) + trigger = 4 frames
    ASSERT_EQ(cap.size(), 4u);
    EXPECT_EQ(cap[0].id, 2u);
    EXPECT_EQ(cap[1].id, 3u);
    EXPECT_EQ(cap[2].id, 4u);
    EXPECT_EQ(cap[3].id, 0xFFFu);
}

TEST(CanTriggerRecorder, PreBuffer_FewerFramesThanPreCount) {
    CanTriggerRecorder r;
    r.setPreCount(10);
    r.setTrigger([](const CanFrame &fr){ return fr.id == 0x999u; });

    r.addFrame(f(1)); r.addFrame(f(2));
    r.addFrame(f(0x999u));  // trigger

    const auto &cap = r.capture();
    // Only 2 pre-frames available before trigger
    ASSERT_EQ(cap.size(), 3u);
    EXPECT_EQ(cap[0].id, 1u);
    EXPECT_EQ(cap[1].id, 2u);
    EXPECT_EQ(cap[2].id, 0x999u);
}

TEST(CanTriggerRecorder, PreBuffer_ZeroPreCount_NoPreFrames) {
    CanTriggerRecorder r;
    r.setPreCount(0);
    r.setTrigger([](const CanFrame &fr){ return fr.data[0] == 0xAB; });

    r.addFrame(f(0x100, 0x00));
    r.addFrame(f(0x100, 0xAB));  // trigger

    const auto &cap = r.capture();
    ASSERT_EQ(cap.size(), 1u);  // only the trigger frame
    EXPECT_EQ(cap[0].data[0], 0xABu);
}

// ── Post-trigger capture ───────────────────────────────────────────────────────

TEST(CanTriggerRecorder, PostCapture_CollectsNFrames) {
    CanTriggerRecorder r;
    r.setPostCount(3);
    r.setTrigger([](const CanFrame &fr){ return fr.id == 0x500u; });

    r.addFrame(f(0x500u));  // trigger
    EXPECT_FALSE(r.captureComplete());
    EXPECT_EQ(r.remainingPost(), 3);

    r.addFrame(f(0xA00u));
    r.addFrame(f(0xB00u));
    EXPECT_FALSE(r.captureComplete());
    r.addFrame(f(0xC00u));
    EXPECT_TRUE(r.captureComplete());
    EXPECT_EQ(r.remainingPost(), 0);

    const auto &cap = r.capture();
    ASSERT_EQ(cap.size(), 4u);  // trigger + 3 post
    EXPECT_EQ(cap[1].id, 0xA00u);
    EXPECT_EQ(cap[3].id, 0xC00u);
}

TEST(CanTriggerRecorder, PostCapture_FramesAfterCompleteIgnored) {
    CanTriggerRecorder r;
    r.setPostCount(1);
    r.setTrigger([](const CanFrame &fr){ return fr.id == 0x100u; });

    r.addFrame(f(0x100u));  // trigger
    r.addFrame(f(0x200u));  // post (1/1)
    ASSERT_TRUE(r.captureComplete());
    r.addFrame(f(0x300u));  // should be ignored
    r.addFrame(f(0x400u));  // should be ignored

    EXPECT_EQ(r.capture().size(), 2u);  // trigger + 1 post only
}

// ── Pre + Post combined ────────────────────────────────────────────────────────

TEST(CanTriggerRecorder, PreAndPost_CombinedCapture) {
    CanTriggerRecorder r;
    r.setPreCount(2);
    r.setPostCount(2);
    r.setTrigger([](const CanFrame &fr){ return fr.id == 0x777u; });

    r.addFrame(f(1));
    r.addFrame(f(2));
    r.addFrame(f(0x777u));  // trigger
    r.addFrame(f(3));
    r.addFrame(f(4));

    const auto &cap = r.capture();
    ASSERT_EQ(cap.size(), 5u);
    EXPECT_EQ(cap[0].id, 1u);
    EXPECT_EQ(cap[1].id, 2u);
    EXPECT_EQ(cap[2].id, 0x777u);
    EXPECT_EQ(cap[3].id, 3u);
    EXPECT_EQ(cap[4].id, 4u);
}

// ── No trigger function ────────────────────────────────────────────────────────

TEST(CanTriggerRecorder, NoTriggerSet_NeverFires) {
    CanTriggerRecorder r;
    for (int i = 0; i < 20; ++i)
        EXPECT_FALSE(r.addFrame(f(static_cast<uint32_t>(i))));
    EXPECT_FALSE(r.triggered());
}

// ── reset ──────────────────────────────────────────────────────────────────────

TEST(CanTriggerRecorder, Reset_ArmsAgain) {
    CanTriggerRecorder r;
    r.setPreCount(2);
    r.setPostCount(1);
    r.setTrigger([](const CanFrame &fr){ return fr.id == 0xDEADu; });

    r.addFrame(f(0xDEADu));
    r.addFrame(f(0x001u));
    ASSERT_TRUE(r.captureComplete());

    r.reset();
    EXPECT_FALSE(r.triggered());
    EXPECT_FALSE(r.captureComplete());
    EXPECT_TRUE(r.capture().empty());

    // Can fire again
    EXPECT_TRUE(r.addFrame(f(0xDEADu)));
    EXPECT_TRUE(r.triggered());
}

TEST(CanTriggerRecorder, Reset_ClearsPreBuffer) {
    CanTriggerRecorder r;
    r.setPreCount(5);
    r.setTrigger([](const CanFrame &fr){ return fr.id == 0xFFFu; });

    r.addFrame(f(1)); r.addFrame(f(2)); r.addFrame(f(3));
    r.reset();
    r.addFrame(f(0xFFFu));  // trigger right after reset

    // Pre buffer was cleared on reset, so no pre frames
    EXPECT_EQ(r.capture().size(), 1u);  // only trigger
}

// ── Trigger fires only once until reset ───────────────────────────────────────

TEST(CanTriggerRecorder, SecondMatchAfterTrigger_DoesNotRetrigger) {
    CanTriggerRecorder r;
    r.setPostCount(0);
    r.setTrigger([](const CanFrame &fr){ return fr.id == 0x111u; });

    bool first  = r.addFrame(f(0x111u));
    bool second = r.addFrame(f(0x111u));  // same pattern, but already triggered
    EXPECT_TRUE(first);
    EXPECT_FALSE(second);
}

// ── Negative pre/post treated as zero ─────────────────────────────────────────

TEST(CanTriggerRecorder, NegativePreCount_TreatedAsZero) {
    CanTriggerRecorder r;
    r.setPreCount(-5);
    r.setTrigger([](const CanFrame &fr){ return fr.id == 0x1u; });
    r.addFrame(f(0x2u));
    r.addFrame(f(0x1u));  // trigger
    EXPECT_EQ(r.capture().size(), 1u);  // 0 pre + trigger
}
