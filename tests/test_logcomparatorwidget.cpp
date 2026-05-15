#include <gtest/gtest.h>
#include "core/LogComparatorWidget.h"
#include "core/CanFrame.h"
#include <QApplication>

// ── Test fixture ─────────────────────────────────────────────────────────────

class LogComparatorTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        if (!QApplication::instance()) {
            static int   argc   = 1;
            static char *argv[] = { (char*)"test", nullptr };
            new QApplication(argc, argv);
        }
    }

    static QString candump(std::initializer_list<std::tuple<double,uint32_t,const char*>> rows) {
        QString out;
        for (const auto &[ts, id, data] : rows)
            out += QString("(%1) vcan0 %2#%3\n")
                .arg(ts, 0, 'f', 6)
                .arg(id, 3, 16, QChar('0'))
                .arg(data);
        return out;
    }
};

// ── parseCandumpContent ──────────────────────────────────────────────────────

TEST_F(LogComparatorTest, ParseEmpty) {
    auto frames = LogComparatorWidget::parseCandumpContent("");
    EXPECT_TRUE(frames.isEmpty());
}

TEST_F(LogComparatorTest, ParseCommentLinesIgnored) {
    QString content = "# this is a comment\n// another comment\n";
    auto frames = LogComparatorWidget::parseCandumpContent(content);
    EXPECT_TRUE(frames.isEmpty());
}

TEST_F(LogComparatorTest, ParseSingleFrame) {
    auto frames = LogComparatorWidget::parseCandumpContent(
        "(1.000000) vcan0 123#AABBCCDD\n");
    ASSERT_EQ(frames.size(), 1);
    EXPECT_EQ(frames[0].id,  0x123u);
    EXPECT_EQ(frames[0].dlc, 4);
    EXPECT_EQ(frames[0].data[0], 0xAA);
    EXPECT_EQ(frames[0].data[1], 0xBB);
    EXPECT_EQ(frames[0].data[2], 0xCC);
    EXPECT_EQ(frames[0].data[3], 0xDD);
}

TEST_F(LogComparatorTest, ParseTimestampMicroseconds) {
    auto frames = LogComparatorWidget::parseCandumpContent(
        "(1715123456.789123) vcan0 100#FF\n");
    ASSERT_EQ(frames.size(), 1);
    uint64_t expectedUs = static_cast<uint64_t>(1715123456.789123 * 1'000'000.0);
    EXPECT_NEAR(static_cast<double>(frames[0].timestamp),
                static_cast<double>(expectedUs), 2.0);
}

TEST_F(LogComparatorTest, ParseExtendedId) {
    auto frames = LogComparatorWidget::parseCandumpContent(
        "(0.000001) vcan0 1FFFFFFF#01\n");
    ASSERT_EQ(frames.size(), 1);
    EXPECT_EQ(frames[0].id, 0x1FFFFFFFu);
}

TEST_F(LogComparatorTest, ParseEmptyPayload) {
    auto frames = LogComparatorWidget::parseCandumpContent(
        "(1.0) vcan0 200#\n");
    ASSERT_EQ(frames.size(), 1);
    EXPECT_EQ(frames[0].id,  0x200u);
    EXPECT_EQ(frames[0].dlc, 0);
}

TEST_F(LogComparatorTest, ParseMultipleFrames) {
    QString content =
        "(1.0) vcan0 100#AA\n"
        "(2.0) vcan0 200#BB\n"
        "(3.0) vcan0 300#CC\n";
    auto frames = LogComparatorWidget::parseCandumpContent(content);
    ASSERT_EQ(frames.size(), 3);
    EXPECT_EQ(frames[0].id, 0x100u);
    EXPECT_EQ(frames[1].id, 0x200u);
    EXPECT_EQ(frames[2].id, 0x300u);
}

TEST_F(LogComparatorTest, ParseMalformedLineSkipped) {
    QString content =
        "not a valid candump line\n"
        "(1.0) vcan0 100#AA\n";
    auto frames = LogComparatorWidget::parseCandumpContent(content);
    ASSERT_EQ(frames.size(), 1);
    EXPECT_EQ(frames[0].id, 0x100u);
}

TEST_F(LogComparatorTest, ParseMixedCaseHex) {
    auto frames = LogComparatorWidget::parseCandumpContent(
        "(1.0) vcan0 abc#DeAdBeEf\n");
    ASSERT_EQ(frames.size(), 1);
    EXPECT_EQ(frames[0].id,      0xABCu);
    EXPECT_EQ(frames[0].data[0], 0xDE);
    EXPECT_EQ(frames[0].data[1], 0xAD);
    EXPECT_EQ(frames[0].data[2], 0xBE);
    EXPECT_EQ(frames[0].data[3], 0xEF);
}

TEST_F(LogComparatorTest, ParseEightBytePayload) {
    auto frames = LogComparatorWidget::parseCandumpContent(
        "(1.0) vcan0 123#0102030405060708\n");
    ASSERT_EQ(frames.size(), 1);
    EXPECT_EQ(frames[0].dlc, 8);
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(frames[0].data[i], static_cast<uint8_t>(i + 1));
}

// ── Widget construct / no crash ──────────────────────────────────────────────

TEST_F(LogComparatorTest, WidgetConstructsWithoutCrash) {
    EXPECT_NO_THROW({
        LogComparatorWidget w;
        w.show();
        w.hide();
    });
}

// ── parseCandumpContent macro tests ─────────────────────────────────────────

TEST_F(LogComparatorTest, ParseCandump_TimestampOrder) {
    QString content =
        "(3.0) vcan0 300#CC\n"
        "(1.0) vcan0 100#AA\n"
        "(2.0) vcan0 200#BB\n";
    auto frames = LogComparatorWidget::parseCandumpContent(content);
    ASSERT_EQ(frames.size(), 3);
    // Frames returned in file order, not sorted
    EXPECT_EQ(frames[0].id, 0x300u);
    EXPECT_EQ(frames[1].id, 0x100u);
    EXPECT_EQ(frames[2].id, 0x200u);
}
