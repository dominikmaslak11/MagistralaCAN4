#include <gtest/gtest.h>
#include "core/CanTpLayer.h"

// Helper: build a CanFrame quickly
static CanFrame makeTP(uint32_t id, std::initializer_list<uint8_t> bytes) {
    CanFrame f;
    f.id  = id;
    f.dlc = static_cast<uint8_t>(bytes.size());
    f.timestamp = 1000000;
    int i = 0;
    for (uint8_t b : bytes) f.data[i++] = b;
    return f;
}

// ── Single Frame ──────────────────────────────────────────────

TEST(CanTp, SingleFrameBasic) {
    CanTpMessage received;
    CanTpLayer tp([&](const CanTpMessage &m){ received = m; });

    // SF: PCI=0x03 (len=3), data=0xAA 0xBB 0xCC
    tp.processFrame(makeTP(0x7E8, {0x03, 0xAA, 0xBB, 0xCC}));

    ASSERT_TRUE(received.complete);
    ASSERT_EQ(received.payload.size(), 3u);
    EXPECT_EQ(received.payload[0], 0xAAu);
    EXPECT_EQ(received.payload[1], 0xBBu);
    EXPECT_EQ(received.payload[2], 0xCCu);
    EXPECT_EQ(received.canId, 0x7E8u);
}

TEST(CanTp, SingleFrameMaxLength) {
    CanTpMessage received;
    CanTpLayer tp([&](const CanTpMessage &m){ received = m; });

    // SF max: PCI=0x07 (len=7)
    tp.processFrame(makeTP(0x100, {0x07, 1, 2, 3, 4, 5, 6, 7}));

    ASSERT_TRUE(received.complete);
    EXPECT_EQ(received.payload.size(), 7u);
}

TEST(CanTp, SingleFrameZeroLengthIgnored) {
    int count = 0;
    CanTpLayer tp([&](const CanTpMessage &){ ++count; });

    // PCI=0x00 — invalid length, must be ignored
    tp.processFrame(makeTP(0x7E8, {0x00, 0xAA}));
    EXPECT_EQ(count, 0);
}

// ── Multi-frame (FF + CF) ─────────────────────────────────────

TEST(CanTp, MultiFrameReassembly) {
    CanTpMessage received;
    CanTpLayer tp([&](const CanTpMessage &m){ received = m; });

    // FF: PCI=0x10, len=0x0E (14 bytes total), first 6 bytes of data
    tp.processFrame(makeTP(0x7E8, {0x10, 0x0E, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06}));
    EXPECT_FALSE(received.complete);

    // CF SN=1: 7 bytes
    tp.processFrame(makeTP(0x7E8, {0x21, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D}));
    EXPECT_FALSE(received.complete);

    // CF SN=2: 1 remaining byte (0x0E)
    tp.processFrame(makeTP(0x7E8, {0x22, 0x0E}));
    ASSERT_TRUE(received.complete);
    ASSERT_EQ(received.payload.size(), 14u);
    for (int i = 0; i < 14; ++i)
        EXPECT_EQ(received.payload[i], static_cast<uint8_t>(i + 1));
}

TEST(CanTp, MultiFrameSnWrap) {
    // SN wraps 1..15: test session with 16 CFs (SN goes 1→15→1)
    CanTpMessage received;
    CanTpLayer tp([&](const CanTpMessage &m){ received = m; });

    // FF: total = 6 + 15*7 + 3 = 6 + 105 + 3 = 114 bytes  → 0x0072
    const int total = 114;
    tp.processFrame(makeTP(0x200, {0x10, static_cast<uint8_t>(total),
                                   1, 2, 3, 4, 5, 6}));

    int sn = 1;
    int sent = 6;
    uint8_t val = 7;
    while (sent < total) {
        int chunk = std::min(7, total - sent);
        std::initializer_list<uint8_t> bytes;
        CanFrame cf;
        cf.id  = 0x200;
        cf.dlc = static_cast<uint8_t>(1 + chunk);
        cf.data[0] = static_cast<uint8_t>(0x20 | (sn & 0x0F));
        for (int i = 0; i < chunk; ++i) cf.data[1 + i] = val++;
        cf.timestamp = 1000000;
        tp.processFrame(cf);
        sent += chunk;
        sn = (sn % 15) + 1;
    }

    ASSERT_TRUE(received.complete);
    EXPECT_EQ(received.payload.size(), static_cast<size_t>(total));
}

// ── Error handling ────────────────────────────────────────────

TEST(CanTp, SequenceNumberError) {
    CanTpMessage received;
    CanTpLayer tp([&](const CanTpMessage &m){ received = m; });

    tp.processFrame(makeTP(0x7E8, {0x10, 0x0A, 1, 2, 3, 4, 5, 6})); // FF, 10 bytes
    // Send SN=2 instead of expected SN=1
    tp.processFrame(makeTP(0x7E8, {0x22, 7, 8, 9, 0, 0, 0, 0}));

    EXPECT_FALSE(received.complete);
    EXPECT_FALSE(received.error.empty());
    // Session should be cleared — next FF should work fresh
    EXPECT_EQ(tp.activeSessions(), 0u);
}

TEST(CanTp, CFWithoutFF) {
    // CF without preceding FF must be silently ignored
    int count = 0;
    CanTpLayer tp([&](const CanTpMessage &){ ++count; });

    tp.processFrame(makeTP(0x7E8, {0x21, 0x01, 0x02}));
    EXPECT_EQ(count, 0);
    EXPECT_EQ(tp.activeSessions(), 0u);
}

TEST(CanTp, SessionTimeout) {
    CanTpMessage received;
    CanTpLayer tp([&](const CanTpMessage &m){ received = m; });

    tp.processFrame(makeTP(0x7E8, {0x10, 0x0A, 1, 2, 3, 4, 5, 6}));
    EXPECT_EQ(tp.activeSessions(), 1u);

    // Purge with timeout=500µs, now=1'600'000µs (600µs after last frame at 1'000'000)
    tp.purgeTimedOut(1'600'000, 500'000);

    EXPECT_EQ(tp.activeSessions(), 0u);
    EXPECT_FALSE(received.error.empty());
}

// ── Send ─────────────────────────────────────────────────────

TEST(CanTp, SendSingleFrame) {
    std::vector<CanFrame> out;
    CanTpLayer tp;

    tp.sendMessage(0x7DF, {0x22, 0xF1, 0x90}, [&](const CanFrame &f){ out.push_back(f); });

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].data[0], 0x03u); // SF, len=3
    EXPECT_EQ(out[0].data[1], 0x22u);
    EXPECT_EQ(out[0].data[2], 0xF1u);
    EXPECT_EQ(out[0].data[3], 0x90u);
}

TEST(CanTp, SendMultiFrame) {
    // Payload 20 bytes: FF(6) + CF1(7) + CF2(7)
    std::vector<uint8_t> payload(20);
    for (int i = 0; i < 20; ++i) payload[i] = static_cast<uint8_t>(i + 1);

    std::vector<CanFrame> out;
    CanTpLayer tp;
    tp.sendMessage(0x7DF, payload, [&](const CanFrame &f){ out.push_back(f); });

    // FF + 2 CFs = 3 frames
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ((out[0].data[0] >> 4), 0x1u); // FF
    EXPECT_EQ((out[1].data[0] >> 4), 0x2u); // CF SN=1
    EXPECT_EQ(out[1].data[0] & 0x0F, 0x1u);
    EXPECT_EQ((out[2].data[0] >> 4), 0x2u); // CF SN=2
    EXPECT_EQ(out[2].data[0] & 0x0F, 0x2u);
}

TEST(CanTp, SendReceiveRoundTrip) {
    // Send from one layer, receive on another — full round-trip
    std::vector<uint8_t> original(50);
    for (int i = 0; i < 50; ++i) original[i] = static_cast<uint8_t>(i * 2);

    CanTpMessage received;
    CanTpLayer receiver([&](const CanTpMessage &m){ received = m; });
    CanTpLayer sender;

    sender.sendMessage(0x7DF, original,
                       [&](const CanFrame &f){ receiver.processFrame(f); });

    ASSERT_TRUE(received.complete);
    ASSERT_EQ(received.payload.size(), 50u);
    EXPECT_EQ(received.payload, original);
}

TEST(CanTp, FrameTypeDetection) {
    EXPECT_EQ(CanTpLayer::frameType(0x03), CanTpFrameType::SingleFrame);
    EXPECT_EQ(CanTpLayer::frameType(0x10), CanTpFrameType::FirstFrame);
    EXPECT_EQ(CanTpLayer::frameType(0x21), CanTpFrameType::ConsecutiveFrame);
    EXPECT_EQ(CanTpLayer::frameType(0x30), CanTpFrameType::FlowControl);
}

TEST(CanTp, ActiveSessionsZeroInitially) {
    CanTpLayer tp;
    EXPECT_EQ(tp.activeSessions(), 0u);
}

TEST(CanTp, WrongSNAbortsSessionWithError) {
    CanTpMessage received;
    CanTpLayer tp([&](const CanTpMessage &m){ received = m; });

    // FF: total=10, 6 bytes in FF
    tp.processFrame(makeTP(0x7E8, {0x10, 0x0A, 1, 2, 3, 4, 5, 6}));
    EXPECT_EQ(tp.activeSessions(), 1u);

    // CF with wrong SN (expected 1, sending 2)
    tp.processFrame(makeTP(0x7E8, {0x22, 7, 8, 9, 10}));

    EXPECT_EQ(tp.activeSessions(), 0u); // session aborted
    EXPECT_FALSE(received.complete);
    EXPECT_FALSE(received.error.empty());
}

TEST(CanTp, SFOverwritesPendingSession) {
    int count = 0;
    CanTpMessage last;
    CanTpLayer tp([&](const CanTpMessage &m){ last = m; ++count; });

    // Start a multi-frame session
    tp.processFrame(makeTP(0x7E8, {0x10, 0x0A, 1, 2, 3, 4, 5, 6}));
    EXPECT_EQ(tp.activeSessions(), 1u);

    // SF for same ID cancels pending session and delivers SF
    tp.processFrame(makeTP(0x7E8, {0x02, 0xAA, 0xBB}));

    // SF delivers one complete message; pending session is gone
    EXPECT_EQ(tp.activeSessions(), 0u);
    EXPECT_TRUE(last.complete);
    EXPECT_EQ(count, 1); // only the SF message, not the aborted session
}

TEST(CanTp, TwoSessionsConcurrent) {
    CanTpMessage msg1, msg2;
    CanTpLayer tp([&](const CanTpMessage &m){
        if (m.canId == 0x100) msg1 = m;
        if (m.canId == 0x200) msg2 = m;
    });

    // Start FF for ID 0x100 (total=10)
    tp.processFrame(makeTP(0x100, {0x10, 0x0A, 1, 2, 3, 4, 5, 6}));
    // Start FF for ID 0x200 (total=10)
    tp.processFrame(makeTP(0x200, {0x10, 0x0A, 11, 12, 13, 14, 15, 16}));
    EXPECT_EQ(tp.activeSessions(), 2u);

    // Finish 0x100 with CF SN=1 (remaining 4 bytes)
    tp.processFrame(makeTP(0x100, {0x21, 7, 8, 9, 10}));
    EXPECT_TRUE(msg1.complete);
    EXPECT_EQ(msg1.payload.size(), 10u);
    EXPECT_EQ(tp.activeSessions(), 1u); // 0x200 still pending

    // Finish 0x200 with CF SN=1
    tp.processFrame(makeTP(0x200, {0x21, 17, 18, 19, 20}));
    EXPECT_TRUE(msg2.complete);
    EXPECT_EQ(msg2.payload.size(), 10u);
    EXPECT_EQ(tp.activeSessions(), 0u);
}

TEST(CanTp, PurgeKeepsActiveSessions) {
    CanTpLayer tp;
    // FF with timestamp=1_000_000
    tp.processFrame(makeTP(0x7E8, {0x10, 0x0A, 1, 2, 3, 4, 5, 6}));
    EXPECT_EQ(tp.activeSessions(), 1u);

    // Purge with now=1_200_000 and timeout=500_000 → 200µs elapsed < 500µs timeout
    tp.purgeTimedOut(1'200'000, 500'000);
    EXPECT_EQ(tp.activeSessions(), 1u); // not expired
}

TEST(CanTp, SendEmptyPayloadDoesNothing) {
    int count = 0;
    CanTpLayer tp;
    tp.sendMessage(0x7DF, {}, [&](const CanFrame &){ ++count; });
    EXPECT_EQ(count, 0);
}

TEST(CanTp, SendMaxSingleFramePayload) {
    // 7-byte payload must be sent as a single frame, not multi-frame
    std::vector<uint8_t> payload = {1, 2, 3, 4, 5, 6, 7};
    std::vector<CanFrame> out;
    CanTpLayer tp;
    tp.sendMessage(0x7DF, payload, [&](const CanFrame &f){ out.push_back(f); });
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ((out[0].data[0] >> 4), 0x0u); // SF type
    EXPECT_EQ(out[0].data[0] & 0x0Fu, 7u);  // length=7
}
