#include <gtest/gtest.h>
#include "core/LinParser.h"

// ── PID computation ───────────────────────────────────────────────────────────

TEST(LinParser, PidCompute_ID0) {
    // Frame ID 0x00: all bits zero → P0=0, P1=~0=1 → PID=0x80
    EXPECT_EQ(LinFrame::computePid(0x00), 0x80u);
}

TEST(LinParser, PidCompute_ID1) {
    // Frame ID 0x01: P0=ID0^ID1^ID2^ID4=1, P1=~(ID1^ID3^ID4^ID5)=~0=1 → PID=0xC1
    EXPECT_EQ(LinFrame::computePid(0x01), 0xC1u);
}

TEST(LinParser, PidValid) {
    for (uint8_t id = 0; id < 64; ++id) {
        uint8_t pid = LinFrame::computePid(id);
        EXPECT_TRUE(LinFrame::pidValid(pid)) << "id=" << (int)id << " pid=" << (int)pid;
    }
}

TEST(LinParser, PidInvalid) {
    // Flip a parity bit → should fail
    uint8_t pid = LinFrame::computePid(0x10);
    EXPECT_FALSE(LinFrame::pidValid(pid ^ 0x40)); // flip P0
}

// ── Checksum ──────────────────────────────────────────────────────────────────

TEST(LinParser, ClassicChecksumAllZero) {
    // Data all zeros, classic → ~0 = 0xFF
    uint8_t data[4] = {0, 0, 0, 0};
    uint8_t cs = LinFrame::computeChecksum(0x00, data, 4, LinFrame::ChecksumType::Classic);
    EXPECT_EQ(cs, 0xFFu);
}

TEST(LinParser, EnhancedChecksumIncludesPid) {
    uint8_t pid = LinFrame::computePid(0x10);
    uint8_t data[2] = {0x00, 0x00};
    uint8_t classic  = LinFrame::computeChecksum(0x00, data, 2, LinFrame::ChecksumType::Classic);
    uint8_t enhanced = LinFrame::computeChecksum(pid,  data, 2, LinFrame::ChecksumType::Enhanced);
    // Enhanced differs unless pid==0
    EXPECT_NE(classic, enhanced);
}

TEST(LinParser, ChecksumInverse) {
    // sum(data) + checksum mod 256 with carry = 0xFF → no carry needed → sum = 0xFF
    uint8_t data[1] = {0x42};
    uint8_t cs = LinFrame::computeChecksum(0x00, data, 1, LinFrame::ChecksumType::Classic);
    // sum = 0x42, carry add: 0x42 + 0 = 0x42, ~0x42 = 0xBD
    EXPECT_EQ(cs, 0xBDu);
}

// ── Parse preframed bytes ─────────────────────────────────────────────────────

TEST(LinParser, ParseWithSync) {
    // Build: sync=0x55 | PID | data[0]=0x01 data[1]=0x02 | checksum
    uint8_t id = 0x20;
    uint8_t pid = LinFrame::computePid(id);
    uint8_t data[2] = {0x01, 0x02};
    uint8_t cs = LinFrame::computeChecksum(pid, data, 2, LinFrame::ChecksumType::Enhanced);
    std::vector<uint8_t> bytes = {0x55, pid, 0x01, 0x02, cs};

    LinFrame f = LinParser::parse(bytes, LinFrame::ChecksumType::Enhanced);
    EXPECT_EQ(f.frameId, id);
    EXPECT_EQ(f.pid, pid);
    EXPECT_TRUE(f.checksumValid);
    EXPECT_EQ(f.data[0], 0x01u);
    EXPECT_EQ(f.data[1], 0x02u);
}

TEST(LinParser, ParseWithoutSync) {
    uint8_t id = 0x05;
    uint8_t pid = LinFrame::computePid(id);
    uint8_t data[2] = {0xAB, 0xCD};
    uint8_t cs = LinFrame::computeChecksum(pid, data, 2, LinFrame::ChecksumType::Enhanced);
    std::vector<uint8_t> bytes = {pid, 0xAB, 0xCD, cs};

    LinFrame f = LinParser::parse(bytes, LinFrame::ChecksumType::Enhanced);
    EXPECT_EQ(f.frameId, id);
    EXPECT_TRUE(f.checksumValid);
}

TEST(LinParser, WrongChecksumDetected) {
    uint8_t id = 0x10;
    uint8_t pid = LinFrame::computePid(id);
    uint8_t data[2] = {0x01, 0x02};
    uint8_t cs = 0xAA; // wrong
    std::vector<uint8_t> bytes = {0x55, pid, 0x01, 0x02, cs};

    LinFrame f = LinParser::parse(bytes, LinFrame::ChecksumType::Enhanced);
    EXPECT_FALSE(f.checksumValid);
}

TEST(LinParser, ClassicFallback) {
    // Build with classic checksum, parse expecting enhanced → should fall back
    uint8_t id = 0x3A;
    uint8_t pid = LinFrame::computePid(id);
    uint8_t data[2] = {0x55, 0x33};
    uint8_t cs_classic = LinFrame::computeChecksum(pid, data, 2, LinFrame::ChecksumType::Classic);
    std::vector<uint8_t> bytes = {0x55, pid, 0x55, 0x33, cs_classic};

    LinFrame f = LinParser::parse(bytes, LinFrame::ChecksumType::Enhanced);
    EXPECT_TRUE(f.checksumValid);
    EXPECT_EQ(f.csType, LinFrame::ChecksumType::Classic);
}

// ── guessDataLength ───────────────────────────────────────────────────────────

TEST(LinParser, GuessDataLength) {
    EXPECT_EQ(LinParser::guessDataLength(0x00), 2);
    EXPECT_EQ(LinParser::guessDataLength(0x1F), 2);
    EXPECT_EQ(LinParser::guessDataLength(0x20), 4);
    EXPECT_EQ(LinParser::guessDataLength(0x2F), 4);
    EXPECT_EQ(LinParser::guessDataLength(0x30), 8);
    EXPECT_EQ(LinParser::guessDataLength(0x3C), 8); // diagnostic
    EXPECT_EQ(LinParser::guessDataLength(0x3F), 8);
}

// ── frameName ─────────────────────────────────────────────────────────────────

TEST(LinParser, FrameNames) {
    EXPECT_EQ(LinParser::frameName(0x3C), "MasterReq (Diagnostics)");
    EXPECT_EQ(LinParser::frameName(0x3D), "SlaveResp (Diagnostics)");
    EXPECT_NE(LinParser::frameName(0x10), "");
}

// ── Feed / reassembly ─────────────────────────────────────────────────────────

TEST(LinParser, FeedSingleFrame) {
    LinParser parser;
    uint8_t id = 0x15;
    uint8_t pid = LinFrame::computePid(id);
    uint8_t data[2] = {0x11, 0x22};
    uint8_t cs = LinFrame::computeChecksum(pid, data, 2, LinFrame::ChecksumType::Enhanced);
    std::vector<uint8_t> bytes = {0x55, pid, 0x11, 0x22, cs};

    auto frames = parser.feed(bytes, 2, LinFrame::ChecksumType::Enhanced);
    ASSERT_EQ(frames.size(), 1u);
    EXPECT_EQ(frames[0].frameId, id);
    EXPECT_TRUE(frames[0].checksumValid);
}

TEST(LinParser, FeedTwoFrames) {
    LinParser parser;

    auto makeBytes = [](uint8_t frameId, uint8_t b0) {
        uint8_t pid = LinFrame::computePid(frameId);
        uint8_t data[2] = {b0, 0x00};
        uint8_t cs = LinFrame::computeChecksum(pid, data, 2, LinFrame::ChecksumType::Enhanced);
        return std::vector<uint8_t>{0x55, pid, b0, 0x00, cs};
    };

    auto b1 = makeBytes(0x10, 0xAA);
    auto b2 = makeBytes(0x20, 0xBB);
    b1.insert(b1.end(), b2.begin(), b2.end());

    auto frames = parser.feed(b1, 2, LinFrame::ChecksumType::Enhanced);
    ASSERT_EQ(frames.size(), 2u);
    EXPECT_EQ(frames[0].frameId, 0x10u);
    EXPECT_EQ(frames[1].frameId, 0x20u);
}

TEST(LinParser, TooShortReturnsEmpty) {
    LinFrame f = LinParser::parse(std::vector<uint8_t>{0x55, 0x80}, LinFrame::ChecksumType::Enhanced);
    EXPECT_FALSE(f.checksumValid);
}
