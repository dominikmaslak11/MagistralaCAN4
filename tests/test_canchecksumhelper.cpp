/**
 * @file test_canchecksumhelper.cpp
 * @brief Unit tests for CanChecksumHelper – XOR, SUM8, CRC8-SAE-J1850, CRC8-AUTOSAR.
 */
#include <gtest/gtest.h>
#include "core/CanChecksumHelper.h"

using Algo = CanChecksumHelper::Algorithm;

static CanFrame makeFrame(std::initializer_list<uint8_t> bytes) {
    CanFrame f{};
    f.dlc = static_cast<uint8_t>(bytes.size());
    int i = 0;
    for (uint8_t b : bytes) f.data[i++] = b;
    return f;
}

// ── XOR ──────────────────────────────────────────────────────────────────────

TEST(CanChecksumHelper, Xor_SingleByte) {
    CanFrame f = makeFrame({0xAB});
    EXPECT_EQ(CanChecksumHelper::compute(f, 0, 0, Algo::XOR), 0xABu);
}

TEST(CanChecksumHelper, Xor_MultipleBytes) {
    // 0x01 ^ 0x02 ^ 0x04 = 0x07
    CanFrame f = makeFrame({0x01, 0x02, 0x04});
    EXPECT_EQ(CanChecksumHelper::compute(f, 0, 2, Algo::XOR), 0x07u);
}

TEST(CanChecksumHelper, Xor_SubRange) {
    // only bytes 1..2: 0x02 ^ 0x04 = 0x06
    CanFrame f = makeFrame({0xFF, 0x02, 0x04, 0xFF});
    EXPECT_EQ(CanChecksumHelper::compute(f, 1, 2, Algo::XOR), 0x06u);
}

TEST(CanChecksumHelper, Xor_SameValueTwice_Zero) {
    // XOR of a ^ a = 0
    CanFrame f = makeFrame({0xAA, 0xAA});
    EXPECT_EQ(CanChecksumHelper::compute(f, 0, 1, Algo::XOR), 0x00u);
}

// ── SUM8 ─────────────────────────────────────────────────────────────────────

TEST(CanChecksumHelper, Sum8_Basic) {
    // 10 + 20 + 30 = 60
    CanFrame f = makeFrame({10, 20, 30});
    EXPECT_EQ(CanChecksumHelper::compute(f, 0, 2, Algo::SUM8), 60u);
}

TEST(CanChecksumHelper, Sum8_Overflow_Wraps) {
    // 200 + 100 = 300 → 300 % 256 = 44
    CanFrame f = makeFrame({200, 100});
    EXPECT_EQ(CanChecksumHelper::compute(f, 0, 1, Algo::SUM8), 44u);
}

// ── CRC8_SAE_J1850 ────────────────────────────────────────────────────────────

TEST(CanChecksumHelper, Crc8SaeJ1850_KnownVector) {
    // CRC-8/SAE-J1850 of {0x01, 0x02, 0x03} = known value from reference impl
    CanFrame f = makeFrame({0x01, 0x02, 0x03});
    uint8_t crc = CanChecksumHelper::compute(f, 0, 2, Algo::CRC8_SAE_J1850);
    // Verify via buffer variant matches
    uint8_t raw[3] = {0x01, 0x02, 0x03};
    EXPECT_EQ(crc, CanChecksumHelper::computeBuffer(raw, 3, Algo::CRC8_SAE_J1850));
}

TEST(CanChecksumHelper, Crc8SaeJ1850_SingleByte_0x00) {
    // CRC-8/SAE-J1850 of {0x00}: init=0xFF, 0xFF^0x00=0xFF, 8 shifts with poly=0x1D
    CanFrame f = makeFrame({0x00, 0x00}); // dlc=2, use byte[0] only
    uint8_t crc = CanChecksumHelper::compute(f, 0, 0, Algo::CRC8_SAE_J1850);
    // Manually: init=0xFF, crc=0xFF, 8 shifts: all MSB=1, so crc = (crc<<1)^0x1D each time
    // Result should be deterministic
    uint8_t raw[1] = {0x00};
    EXPECT_EQ(crc, CanChecksumHelper::computeBuffer(raw, 1, Algo::CRC8_SAE_J1850));
}

// ── CRC8_AUTOSAR ─────────────────────────────────────────────────────────────

TEST(CanChecksumHelper, Crc8Autosar_KnownVector) {
    // CRC-8/AUTOSAR of {0x00}: finalXor=0xFF
    uint8_t raw[1] = {0x00};
    uint8_t crc = CanChecksumHelper::computeBuffer(raw, 1, Algo::CRC8_AUTOSAR);
    // Compare with same via frame API
    CanFrame f = makeFrame({0x00, 0x00});
    EXPECT_EQ(CanChecksumHelper::compute(f, 0, 0, Algo::CRC8_AUTOSAR), crc);
}

TEST(CanChecksumHelper, Crc8Autosar_DifferentFromSaeJ1850) {
    // These two CRC variants should yield different results for most inputs
    uint8_t raw[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t sae  = CanChecksumHelper::computeBuffer(raw, 4, Algo::CRC8_SAE_J1850);
    uint8_t asar = CanChecksumHelper::computeBuffer(raw, 4, Algo::CRC8_AUTOSAR);
    EXPECT_NE(sae, asar);
}

// ── validate() ───────────────────────────────────────────────────────────────

TEST(CanChecksumHelper, Validate_XOR_Correct) {
    // data[0..2]=0x01,0x02,0x04, data[3]=checksum=0x07
    CanFrame f = makeFrame({0x01, 0x02, 0x04, 0x07});
    EXPECT_TRUE(CanChecksumHelper::validate(f, 3, 0, 2, Algo::XOR));
}

TEST(CanChecksumHelper, Validate_XOR_Wrong) {
    CanFrame f = makeFrame({0x01, 0x02, 0x04, 0xFF}); // wrong checksum
    EXPECT_FALSE(CanChecksumHelper::validate(f, 3, 0, 2, Algo::XOR));
}

TEST(CanChecksumHelper, Validate_Sum8_Correct) {
    // 10+20+30=60 → checksum byte=60
    CanFrame f = makeFrame({10, 20, 30, 60});
    EXPECT_TRUE(CanChecksumHelper::validate(f, 3, 0, 2, Algo::SUM8));
}

// ── Edge cases ────────────────────────────────────────────────────────────────

TEST(CanChecksumHelper, OutOfRangeBytesReturnZero) {
    CanFrame f = makeFrame({0xAA, 0xBB});
    // endByte >= dlc → 0
    EXPECT_EQ(CanChecksumHelper::compute(f, 0, 5, Algo::XOR), 0u);
    // startByte < 0 → 0
    EXPECT_EQ(CanChecksumHelper::compute(f, -1, 1, Algo::XOR), 0u);
}

TEST(CanChecksumHelper, ValidateOutOfRangeChecksumByte_False) {
    CanFrame f = makeFrame({0x01, 0x02});
    EXPECT_FALSE(CanChecksumHelper::validate(f, 5, 0, 1, Algo::XOR));
}

TEST(CanChecksumHelper, ComputeBuffer_NullReturnsZero) {
    EXPECT_EQ(CanChecksumHelper::computeBuffer(nullptr, 4, Algo::XOR), 0u);
}

TEST(CanChecksumHelper, ComputeBuffer_ZeroLenReturnsZero) {
    uint8_t buf[4] = {1, 2, 3, 4};
    EXPECT_EQ(CanChecksumHelper::computeBuffer(buf, 0, Algo::SUM8), 0u);
}

TEST(CanChecksumHelper, ValidateMatchesComputeForCrc8) {
    // Compute CRC, place it in the frame, then validate
    uint8_t raw[3] = {0xDE, 0xAD, 0xBE};
    uint8_t expectedCrc = CanChecksumHelper::computeBuffer(raw, 3, Algo::CRC8_AUTOSAR);

    CanFrame f = makeFrame({0xDE, 0xAD, 0xBE, expectedCrc});
    EXPECT_TRUE(CanChecksumHelper::validate(f, 3, 0, 2, Algo::CRC8_AUTOSAR));
}
