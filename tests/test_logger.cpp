#include <gtest/gtest.h>
#include "core/Logger.h"
#include <QFile>
#include <QTextStream>
#include <QTemporaryDir>

// ── Fixture that redirects log to a temp file ────────────────

class LoggerTest : public ::testing::Test {
protected:
    QTemporaryDir m_dir;
    QString       m_path;

    void SetUp() override {
        m_path = m_dir.filePath("test.log");
        Logger::setLogPath(m_path);
    }
    void TearDown() override {
        Logger::setLogPath("");  // restore default
    }

    QStringList readLines() {
        QFile f(m_path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
        QStringList lines;
        QTextStream s(&f);
        while (!s.atEnd()) {
            QString l = s.readLine().trimmed();
            if (!l.isEmpty()) lines << l;
        }
        return lines;
    }
};

// ══════════════════════════════════════════════════════════════
// Tests
// ══════════════════════════════════════════════════════════════

TEST_F(LoggerTest, LogPath_DefaultIsHome) {
    Logger::setLogPath("");
    EXPECT_TRUE(Logger::logPath().endsWith("magistrala_can4.log"));
    Logger::setLogPath(m_path);  // restore for cleanup
}

TEST_F(LoggerTest, SetLogPath_ChangesPath) {
    EXPECT_EQ(Logger::logPath(), m_path);
}

TEST_F(LoggerTest, Log_CreatesFile) {
    Logger::log("Hello");
    EXPECT_TRUE(QFile::exists(m_path));
}

TEST_F(LoggerTest, Log_OneMessage_OneLineInFile) {
    Logger::log("Single message");
    auto lines = readLines();
    ASSERT_EQ(lines.size(), 1);
}

TEST_F(LoggerTest, Log_MultipleMessages_MultipleLines) {
    Logger::log("Line one");
    Logger::log("Line two");
    Logger::log("Line three");
    EXPECT_EQ(readLines().size(), 3);
}

TEST_F(LoggerTest, Log_MessageInLine) {
    Logger::log("my_unique_test_token");
    auto lines = readLines();
    ASSERT_EQ(lines.size(), 1);
    EXPECT_TRUE(lines[0].contains("my_unique_test_token"));
}

TEST_F(LoggerTest, Log_LineContainsTimestamp) {
    Logger::log("ts_test");
    auto lines = readLines();
    ASSERT_EQ(lines.size(), 1);
    // Timestamp format: [YYYY-MM-DD hh:mm:ss.zzz]
    EXPECT_TRUE(lines[0].startsWith("["));
    EXPECT_TRUE(lines[0].contains("]"));
}

TEST_F(LoggerTest, Log_AppendMode_PreservesExistingEntries) {
    Logger::log("first");
    Logger::log("second");
    EXPECT_EQ(readLines().size(), 2);
    Logger::log("third");
    EXPECT_EQ(readLines().size(), 3);
}

TEST_F(LoggerTest, Log_EmptyString_WritesLine) {
    Logger::log("");
    EXPECT_EQ(readLines().size(), 1);
}

TEST_F(LoggerTest, SetLogPath_EmptyRestoresDefault) {
    Logger::setLogPath("/tmp/custom.log");
    Logger::setLogPath("");
    EXPECT_TRUE(Logger::logPath().endsWith("magistrala_can4.log"));
    Logger::setLogPath(m_path);  // restore for cleanup
}

TEST_F(LoggerTest, Log_SpecialCharacters_ContainedInLine) {
    Logger::log("test\t[bracket]{brace}");
    auto lines = readLines();
    ASSERT_EQ(lines.size(), 1);
    EXPECT_TRUE(lines[0].contains("[bracket]"));
    EXPECT_TRUE(lines[0].contains("{brace}"));
}

TEST_F(LoggerTest, Log_UnicodeContent) {
    Logger::log(QString::fromUtf8("zażółć gęślą jaźń"));
    auto lines = readLines();
    ASSERT_EQ(lines.size(), 1);
    EXPECT_TRUE(lines[0].contains(QString::fromUtf8("zażółć")));
}

TEST_F(LoggerTest, Log_LongMessage_WrittenInSingleLine) {
    QString msg(1000, 'X');
    Logger::log(msg);
    auto lines = readLines();
    ASSERT_EQ(lines.size(), 1);
    EXPECT_TRUE(lines[0].contains(msg));
}

TEST_F(LoggerTest, Log_PathNotChangedByLog) {
    QString before = Logger::logPath();
    Logger::log("sanity check");
    EXPECT_EQ(Logger::logPath(), before);
}

TEST_F(LoggerTest, Log_SecondLineContainsSecondMessage) {
    Logger::log("first_token");
    Logger::log("second_token");
    auto lines = readLines();
    ASSERT_EQ(lines.size(), 2);
    EXPECT_TRUE(lines[1].contains("second_token"));
    EXPECT_FALSE(lines[0].contains("second_token"));
}

TEST_F(LoggerTest, Log_OrderPreserved) {
    for (int i = 0; i < 5; ++i)
        Logger::log(QString("msg_%1").arg(i));
    auto lines = readLines();
    ASSERT_EQ(lines.size(), 5);
    for (int i = 0; i < 5; ++i)
        EXPECT_TRUE(lines[i].contains(QString("msg_%1").arg(i)));
}
