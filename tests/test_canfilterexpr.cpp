#include <gtest/gtest.h>
#include "core/CanFilterExpr.h"

static CanFrame makeFrame(uint32_t id, uint8_t dlc,
                           std::initializer_list<uint8_t> data = {},
                           bool ext = false, bool fd = false, bool err = false) {
    CanFrame f{};
    f.id = id; f.dlc = dlc; f.extended = ext; f.fd = fd; f.error = err;
    int i = 0;
    for (uint8_t b : data) f.data[i++] = b;
    return f;
}

// ── Basic equality ────────────────────────────────────────────────────────────

TEST(CanFilterExpr, IdEquality) {
    auto expr = CanFilterExpr::parse("can.id == 0x100");
    EXPECT_TRUE(expr.isValid());
    EXPECT_TRUE(expr.matches(makeFrame(0x100, 1)));
    EXPECT_FALSE(expr.matches(makeFrame(0x101, 1)));
}

TEST(CanFilterExpr, IdDecimal) {
    auto expr = CanFilterExpr::parse("can.id == 256"); // 0x100
    EXPECT_TRUE(expr.matches(makeFrame(0x100, 1)));
}

TEST(CanFilterExpr, DlcEquality) {
    auto expr = CanFilterExpr::parse("can.dlc == 8");
    EXPECT_TRUE(expr.matches(makeFrame(0x100, 8)));
    EXPECT_FALSE(expr.matches(makeFrame(0x100, 4)));
}

TEST(CanFilterExpr, DataByteEquality) {
    auto expr = CanFilterExpr::parse("can.data[0] == 0xFF");
    EXPECT_TRUE(expr.matches(makeFrame(0x100, 1, {0xFF})));
    EXPECT_FALSE(expr.matches(makeFrame(0x100, 1, {0xFE})));
}

TEST(CanFilterExpr, DataByteIndex1) {
    auto expr = CanFilterExpr::parse("can.data[1] == 0x42");
    EXPECT_TRUE(expr.matches(makeFrame(0x100, 2, {0x00, 0x42})));
    EXPECT_FALSE(expr.matches(makeFrame(0x100, 2, {0x42, 0x00})));
}

// ── Comparison operators ──────────────────────────────────────────────────────

TEST(CanFilterExpr, GreaterThan) {
    auto expr = CanFilterExpr::parse("can.id > 0x100");
    EXPECT_TRUE(expr.matches(makeFrame(0x200, 1)));
    EXPECT_FALSE(expr.matches(makeFrame(0x100, 1)));
    EXPECT_FALSE(expr.matches(makeFrame(0x0FF, 1)));
}

TEST(CanFilterExpr, LessOrEqual) {
    auto expr = CanFilterExpr::parse("can.dlc <= 4");
    EXPECT_TRUE(expr.matches(makeFrame(0x100, 4)));
    EXPECT_TRUE(expr.matches(makeFrame(0x100, 0)));
    EXPECT_FALSE(expr.matches(makeFrame(0x100, 5)));
}

TEST(CanFilterExpr, NotEqual) {
    auto expr = CanFilterExpr::parse("can.id != 0x7DF");
    EXPECT_TRUE(expr.matches(makeFrame(0x100, 1)));
    EXPECT_FALSE(expr.matches(makeFrame(0x7DF, 1)));
}

// ── Logical operators ─────────────────────────────────────────────────────────

TEST(CanFilterExpr, AndBothTrue) {
    auto expr = CanFilterExpr::parse("can.id == 0x100 && can.dlc == 8");
    EXPECT_TRUE(expr.matches(makeFrame(0x100, 8)));
    EXPECT_FALSE(expr.matches(makeFrame(0x100, 4)));
    EXPECT_FALSE(expr.matches(makeFrame(0x200, 8)));
}

TEST(CanFilterExpr, OrEitherTrue) {
    auto expr = CanFilterExpr::parse("can.id == 0x100 || can.id == 0x200");
    EXPECT_TRUE(expr.matches(makeFrame(0x100, 1)));
    EXPECT_TRUE(expr.matches(makeFrame(0x200, 1)));
    EXPECT_FALSE(expr.matches(makeFrame(0x300, 1)));
}

TEST(CanFilterExpr, NotNegates) {
    auto expr = CanFilterExpr::parse("!can.fd");
    EXPECT_TRUE(expr.matches(makeFrame(0x100, 1, {}, false, false)));
    EXPECT_FALSE(expr.matches(makeFrame(0x100, 1, {}, false, true)));
}

// ── Boolean fields ────────────────────────────────────────────────────────────

TEST(CanFilterExpr, FdField) {
    auto expr = CanFilterExpr::parse("can.fd == true");
    EXPECT_TRUE(expr.matches(makeFrame(0x100, 8, {}, false, true)));
    EXPECT_FALSE(expr.matches(makeFrame(0x100, 8, {}, false, false)));
}

TEST(CanFilterExpr, ExtField) {
    auto expr = CanFilterExpr::parse("can.ext == 1");
    EXPECT_TRUE(expr.matches(makeFrame(0x1FFFFFFF, 1, {}, true)));
    EXPECT_FALSE(expr.matches(makeFrame(0x100, 1, {}, false)));
}

TEST(CanFilterExpr, ErrField) {
    auto expr = CanFilterExpr::parse("can.err == true");
    EXPECT_TRUE(expr.matches(makeFrame(0x100, 0, {}, false, false, true)));
    EXPECT_FALSE(expr.matches(makeFrame(0x100, 0, {}, false, false, false)));
}

// ── Grouping ──────────────────────────────────────────────────────────────────

TEST(CanFilterExpr, Parentheses) {
    auto expr = CanFilterExpr::parse("(can.id >= 0x7DF && can.id <= 0x7EF) || can.dlc == 0");
    EXPECT_TRUE(expr.matches(makeFrame(0x7DF, 1)));
    EXPECT_TRUE(expr.matches(makeFrame(0x7E8, 1)));
    EXPECT_TRUE(expr.matches(makeFrame(0x100, 0))); // dlc == 0
    EXPECT_FALSE(expr.matches(makeFrame(0x100, 1))); // outside range, dlc != 0
}

// ── Empty expression ──────────────────────────────────────────────────────────

TEST(CanFilterExpr, EmptyMatchesAll) {
    auto expr = CanFilterExpr::parse("");
    EXPECT_TRUE(expr.isValid());
    EXPECT_TRUE(expr.matches(makeFrame(0x100, 8)));
    EXPECT_TRUE(expr.matches(makeFrame(0x000, 0)));
}

// ── Error handling ────────────────────────────────────────────────────────────

TEST(CanFilterExpr, InvalidExprNotValid) {
    auto expr = CanFilterExpr::parse("can.xyz == 5"); // unknown field
    EXPECT_FALSE(expr.isValid());
    EXPECT_FALSE(expr.errorString().isEmpty());
    EXPECT_FALSE(expr.matches(makeFrame(0x100, 1)));
}

TEST(CanFilterExpr, MissingOperand) {
    auto expr = CanFilterExpr::parse("can.id ==");
    EXPECT_FALSE(expr.isValid());
}

// ── Data byte out of range returns 0 ─────────────────────────────────────────

TEST(CanFilterExpr, DataOutOfRangeReturnsZero) {
    auto expr = CanFilterExpr::parse("can.data[5] == 0"); // dlc=2, byte[5] doesn't exist
    EXPECT_TRUE(expr.matches(makeFrame(0x100, 2, {0x11, 0x22}))); // returns 0 == 0 → true
}
