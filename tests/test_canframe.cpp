/**
 * @file test_canframe.cpp
 * @brief Unit tests for CanFrame struct — byteAt bounds-check, defaults, size.
 *
 * CanFrame is a header-only struct. No Qt application needed.
 */

#include <gtest/gtest.h>
#include "core/CanFrame.h"

// ── byteAt bounds-check ──

TEST(CanFrame, byteAtReturnsZeroForNegativeIndex) {
    CanFrame f;
    EXPECT_EQ(f.byteAt(-1), 0);
    EXPECT_EQ(f.byteAt(-100), 0);
}

TEST(CanFrame, byteAtReturnsZeroForIndexAtOrAbove64) {
    CanFrame f;
    f.data[63] = 0xFF;
    EXPECT_EQ(f.byteAt(64), 0);
    EXPECT_EQ(f.byteAt(2047), 0);   // CAN XL boundary
}

TEST(CanFrame, byteAtReturnsCorrectByte) {
    CanFrame f;
    f.data[0] = 0xAA;
    f.data[10] = 0x55;
    f.data[63] = 0xFF;
    EXPECT_EQ(f.byteAt(0), 0xAA);
    EXPECT_EQ(f.byteAt(10), 0x55);
    EXPECT_EQ(f.byteAt(63), 0xFF);
}

// ── Payload size ──

TEST(CanFrame, dataArrayIs64Bytes) {
    EXPECT_EQ(sizeof(CanFrame::data), 64);
}

// ── Default values ──

TEST(CanFrame, defaultIdIsZero) {
    CanFrame f;
    EXPECT_EQ(f.id, 0u);
}

TEST(CanFrame, defaultFlagsAreFalse) {
    CanFrame f;
    EXPECT_FALSE(f.extended);
    EXPECT_FALSE(f.rtr);
    EXPECT_FALSE(f.error);
    EXPECT_FALSE(f.fd);
    EXPECT_FALSE(f.xl);
}

TEST(CanFrame, defaultDlcIsZero) {
    CanFrame f;
    EXPECT_EQ(f.dlc, 0);
}

TEST(CanFrame, defaultTimestampIsZero) {
    CanFrame f;
    EXPECT_EQ(f.timestamp, 0ull);
}

TEST(CanFrame, defaultDataIsZeroed) {
    CanFrame f;
    for (int i = 0; i < 64; ++i)
        EXPECT_EQ(f.data[i], 0);
}

// ── toString format ──

TEST(CanFrame, toStringContainsIdAndDlc) {
    CanFrame f;
    f.id = 0x123;
    f.dlc = 8;
    QString s = f.toString();
    EXPECT_TRUE(s.contains("123"));
    EXPECT_TRUE(s.contains("8"));
    EXPECT_TRUE(s.contains("CAN"));
}

TEST(CanFrame, toStringShowsFdFlag) {
    CanFrame f;
    f.fd = true;
    EXPECT_TRUE(f.toString().contains("FD"));
}

TEST(CanFrame, toStringShowsXlFlag) {
    CanFrame f;
    f.xl = true;
    EXPECT_TRUE(f.toString().contains("XL"));
}

// ── XL fields ──

TEST(CanFrame, defaultSdtIsZero) {
    CanFrame f;
    EXPECT_EQ(f.sdt, 0);
}

TEST(CanFrame, defaultAfIsZero) {
    CanFrame f;
    EXPECT_EQ(f.af, 0u);
}

// ── Additional flag defaults ──

TEST(CanFrame, defaultBrsIsFalse) {
    CanFrame f;
    EXPECT_FALSE(f.brs);
}

TEST(CanFrame, defaultEsiIsFalse) {
    CanFrame f;
    EXPECT_FALSE(f.esi);
}

TEST(CanFrame, defaultChangedMaskIsZero) {
    CanFrame f;
    EXPECT_EQ(f.changedMask, 0ull);
}

// ── byteAt exact boundary ──

TEST(CanFrame, byteAtExactBoundary63) {
    CanFrame f;
    f.data[63] = 0xBE;
    EXPECT_EQ(f.byteAt(63), 0xBE);
    EXPECT_EQ(f.byteAt(64), 0);  // out of bounds
}

// ── Data write-read roundtrip ──

TEST(CanFrame, allBytesWriteReadback) {
    CanFrame f;
    for (int i = 0; i < 64; ++i)
        f.data[i] = static_cast<uint8_t>(i * 3 + 7);
    for (int i = 0; i < 64; ++i)
        EXPECT_EQ(f.byteAt(i), static_cast<uint8_t>(i * 3 + 7)) << "byte " << i;
}

// ── Flag setting ──

TEST(CanFrame, extendedFlagCanBeSet) {
    CanFrame f;
    f.extended = true;
    EXPECT_TRUE(f.extended);
}

TEST(CanFrame, rtrFlagCanBeSet) {
    CanFrame f;
    f.rtr = true;
    EXPECT_TRUE(f.rtr);
}

TEST(CanFrame, errorFlagCanBeSet) {
    CanFrame f;
    f.error = true;
    EXPECT_TRUE(f.error);
}

// ── ID max value ──

TEST(CanFrame, maxExtendedId) {
    CanFrame f;
    f.id = 0x1FFFFFFFu;  // 29-bit max
    EXPECT_EQ(f.id, 0x1FFFFFFFu);
}

// ── toString does not crash for various flag combinations ──

TEST(CanFrame, toStringNoCrash_AllFlagsSet) {
    CanFrame f;
    f.id = 0x1FF; f.dlc = 64;
    f.extended = true; f.rtr = true; f.error = true;
    f.fd = true; f.brs = true; f.esi = true; f.xl = true;
    EXPECT_NO_THROW({ QString s = f.toString(); (void)s; });
}
