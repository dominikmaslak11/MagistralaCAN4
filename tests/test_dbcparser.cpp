/**
 * @file test_dbcparser.cpp
 * @brief Unit tests for DbcParser — DBC file parsing, signal decoding, serialization.
 *
 * Requires QCoreApplication (Qt6::Core). Tests create temp DBC files inline.
 */

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTemporaryFile>
#include "core/DbcParser.h"

// ── Helper: write DBC content to a temp file, parse it, clean up ──

static bool parseDbcContent(const QString &content, DbcParser &parser) {
    QTemporaryFile tmp(QDir::tempPath() + "/magistrala_test_XXXXXX.dbc");
    tmp.setAutoRemove(true);
    if (!tmp.open()) return false;
    QTextStream ts(&tmp);
    ts << content;
    ts.flush();
    QString path = tmp.fileName();
    tmp.close();
    return parser.load(path);
}

// ── Empty DBC ──

TEST(DbcParser, loadEmptyDbc) {
    DbcParser parser;
    EXPECT_TRUE(parseDbcContent("", parser));
    EXPECT_TRUE(parser.messages().isEmpty());
}

// ── Single message, single signal ──

TEST(DbcParser, parseSingleMessage) {
    DbcParser parser;
    const QString dbc = QStringLiteral(
        "VERSION \"\"\n\n"
        "BO_ 256 EngineData: 8 Vector__XXX\n"
        " SG_ EngineSpeed : 0|16@1+ (1,0) [0|65535] \"rpm\" Vector__XXX\n"
    );
    ASSERT_TRUE(parseDbcContent(dbc, parser));

    auto msgs = parser.messages();
    ASSERT_EQ(msgs.size(), 1);
    EXPECT_EQ(msgs[0].id, 256u);
    EXPECT_EQ(msgs[0].name, QStringLiteral("EngineData"));
    EXPECT_EQ(msgs[0].dlc, 8);

    ASSERT_EQ(msgs[0].sigList.size(), 1);
    const auto &sig = msgs[0].sigList[0];
    EXPECT_EQ(sig.name, QStringLiteral("EngineSpeed"));
    EXPECT_EQ(sig.startBit, 0);
    EXPECT_EQ(sig.length, 16);
    EXPECT_TRUE(sig.isLittleEndian);
    EXPECT_FALSE(sig.isSigned);
    EXPECT_DOUBLE_EQ(sig.scale, 1.0);
    EXPECT_DOUBLE_EQ(sig.offset, 0.0);
}

// ── Multiple messages ──

TEST(DbcParser, parseMultipleMessages) {
    DbcParser parser;
    const QString dbc = QStringLiteral(
        "VERSION \"\"\n\n"
        "BO_ 100 MsgA: 4 Vector__XXX\n"
        " SG_ SigA : 0|8@1+ (0.5,0) [0|255] \"V\" Vector__XXX\n"
        "\n"
        "BO_ 200 MsgB: 8 Vector__XXX\n"
        " SG_ SigB : 8|16@1- (0.1,-10) [-32768|32767] \"deg\" Vector__XXX\n"
    );
    ASSERT_TRUE(parseDbcContent(dbc, parser));

    auto msgs = parser.messages();
    ASSERT_EQ(msgs.size(), 2);
    EXPECT_EQ(msgs[0].id, 100u);
    EXPECT_EQ(msgs[1].id, 200u);

    // Signed signal
    EXPECT_TRUE(msgs[1].sigList[0].isSigned);
}

// ── messageForId ──

TEST(DbcParser, messageForIdLookup) {
    DbcParser parser;
    const QString dbc = QStringLiteral(
        "VERSION \"\"\n\n"
        "BO_ 0x123 MsgHex: 8 Vector__XXX\n"
        " SG_ SigX : 0|32@1+ (1,0) [0|4294967295] \"\" Vector__XXX\n"
    );
    ASSERT_TRUE(parseDbcContent(dbc, parser));

    auto msg = parser.messageForId(0x123);
    EXPECT_EQ(msg.id, 0x123u);
    EXPECT_EQ(msg.name, QStringLiteral("MsgHex"));
}

// ── decodeSignals (little-endian unsigned) ──

TEST(DbcParser, decodeSignalsUnsigned) {
    DbcParser parser;
    const QString dbc = QStringLiteral(
        "VERSION \"\"\n\n"
        "BO_ 256 TestMsg: 8 Vector__XXX\n"
        " SG_ Sig16 : 0|16@1+ (1,0) [0|65535] \"rpm\" Vector__XXX\n"
    );
    ASSERT_TRUE(parseDbcContent(dbc, parser));

    // Raw bytes: 0x34 0x12 → little-endian 16-bit = 0x1234 = 4660
    uint8_t data[8] = {0x34, 0x12, 0, 0, 0, 0, 0, 0};
    auto values = parser.decodeSignals(256, data, 8);
    ASSERT_TRUE(values.contains("Sig16"));
    EXPECT_DOUBLE_EQ(values["Sig16"], 4660.0);
}

// ── decodeSignals with scale/offset ──

TEST(DbcParser, decodeSignalsWithScaleOffset) {
    DbcParser parser;
    const QString dbc = QStringLiteral(
        "VERSION \"\"\n\n"
        "BO_ 100 TempMsg: 4 Vector__XXX\n"
        " SG_ Temp : 0|8@1+ (0.5,-40) [0|255] \"degC\" Vector__XXX\n"
    );
    ASSERT_TRUE(parseDbcContent(dbc, parser));

    // Raw byte: 100  → scaled: 100 * 0.5 - 40 = 10.0 degC
    uint8_t data[8] = {100, 0, 0, 0, 0, 0, 0, 0};
    auto values = parser.decodeSignals(100, data, 4);
    ASSERT_TRUE(values.contains("Temp"));
    EXPECT_DOUBLE_EQ(values["Temp"], 10.0);
}

// ── setMessages + serialize roundtrip ──

TEST(DbcParser, setMessagesAndSerialize) {
    DbcParser parser;
    QVector<DbcMessage> msgs;
    DbcMessage m;
    m.id = 512;
    m.name = "Roundtrip";
    m.dlc = 6;
    DbcSignal s;
    s.name = "SignalA";
    s.startBit = 0;
    s.length = 8;
    s.isLittleEndian = true;
    s.scale = 1.0;
    m.sigList.append(s);
    msgs.append(m);

    parser.setMessages(msgs);
    EXPECT_EQ(parser.messages().size(), 1);
    EXPECT_EQ(parser.messages()[0].id, 512u);

    QString serialized = DbcParser::serialize(msgs);
    EXPECT_TRUE(serialized.contains("Roundtrip"));
    EXPECT_TRUE(serialized.contains("512"));
}

// ── decodeSignals returns empty for unknown ID ──

TEST(DbcParser, decodeSignalsUnknownIdReturnsEmpty) {
    DbcParser parser;
    const QString dbc = QStringLiteral(
        "VERSION \"\"\n\n"
        "BO_ 256 Msg: 8 Vector__XXX\n"
        " SG_ Sig : 0|8@1+ (1,0) [0|255] \"\" Vector__XXX\n"
    );
    ASSERT_TRUE(parseDbcContent(dbc, parser));

    uint8_t data[8] = {0};
    auto values = parser.decodeSignals(999, data, 8);
    EXPECT_TRUE(values.isEmpty());
}

// ── signalDescriptions ──

TEST(DbcParser, signalDescriptions) {
    DbcParser parser;
    const QString dbc = QStringLiteral(
        "VERSION \"\"\n\n"
        "BO_ 256 Msg: 8 Vector__XXX\n"
        " SG_ RPM : 0|16@1+ (1,0) [0|65535] \"rpm\" Vector__XXX\n"
    );
    ASSERT_TRUE(parseDbcContent(dbc, parser));

    uint8_t data[8] = {0xE8, 0x03, 0, 0, 0, 0, 0, 0};  // 1000 rpm
    QString desc = parser.signalDescriptions(256, data, 8);
    EXPECT_TRUE(desc.contains("RPM"));
    EXPECT_TRUE(desc.contains("1000"));
}
