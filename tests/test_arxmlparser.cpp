#include <gtest/gtest.h>
#include "core/ArxmlParser.h"
#include <QTemporaryFile>
#include <QTextStream>

// ── Helpers ───────────────────────────────────────────────────────────────────

static QString writeArxml(const QString &content) {
    QTemporaryFile tmp;
    tmp.setAutoRemove(false);
    if (!tmp.open()) return {};
    QTextStream ts(&tmp);
    ts << content;
    tmp.close();
    return tmp.fileName();
}

static const QString kMinimalArxml = R"(<?xml version="1.0" encoding="UTF-8"?>
<AUTOSAR>
  <AR-PACKAGES>
    <AR-PACKAGE>
      <ELEMENTS>
        <I-SIGNAL>
          <SHORT-NAME>EngineSpeed</SHORT-NAME>
          <BIT-LENGTH>16</BIT-LENGTH>
          <BYTE-ORDER>MOST-SIGNIFICANT-BYTE-LAST</BYTE-ORDER>
        </I-SIGNAL>
        <I-SIGNAL>
          <SHORT-NAME>Throttle</SHORT-NAME>
          <BIT-LENGTH>8</BIT-LENGTH>
          <BYTE-ORDER>MOST-SIGNIFICANT-BYTE-LAST</BYTE-ORDER>
        </I-SIGNAL>
        <I-SIGNAL-I-PDU>
          <SHORT-NAME>EngineData_PDU</SHORT-NAME>
          <LENGTH>8</LENGTH>
          <I-SIGNAL-TO-I-PDU-MAPPINGS>
            <I-SIGNAL-TO-I-PDU-MAPPING>
              <I-SIGNAL-REF DEST="I-SIGNAL">/pkg/EngineSpeed</I-SIGNAL-REF>
              <START-POSITION>0</START-POSITION>
              <PACKING-BYTE-ORDER>MOST-SIGNIFICANT-BYTE-LAST</PACKING-BYTE-ORDER>
            </I-SIGNAL-TO-I-PDU-MAPPING>
            <I-SIGNAL-TO-I-PDU-MAPPING>
              <I-SIGNAL-REF DEST="I-SIGNAL">/pkg/Throttle</I-SIGNAL-REF>
              <START-POSITION>16</START-POSITION>
              <PACKING-BYTE-ORDER>MOST-SIGNIFICANT-BYTE-LAST</PACKING-BYTE-ORDER>
            </I-SIGNAL-TO-I-PDU-MAPPING>
          </I-SIGNAL-TO-I-PDU-MAPPINGS>
        </I-SIGNAL-I-PDU>
        <FRAME>
          <SHORT-NAME>EngineData</SHORT-NAME>
          <FRAME-LENGTH>8</FRAME-LENGTH>
          <PDU-TO-FRAME-MAPPINGS>
            <PDU-TO-FRAME-MAPPING>
              <PDU-REF DEST="I-SIGNAL-I-PDU">/pkg/EngineData_PDU</PDU-REF>
              <START-POSITION>0</START-POSITION>
            </PDU-TO-FRAME-MAPPING>
          </PDU-TO-FRAME-MAPPINGS>
        </FRAME>
        <CAN-FRAME-TRIGGERING>
          <SHORT-NAME>EngineData_Tx</SHORT-NAME>
          <IDENTIFIER>0x0C0</IDENTIFIER>
          <FRAME-REF DEST="FRAME">/pkg/EngineData</FRAME-REF>
        </CAN-FRAME-TRIGGERING>
      </ELEMENTS>
    </AR-PACKAGE>
  </AR-PACKAGES>
</AUTOSAR>)";

// ── Tests ──────────────────────────────────────────────────────────────────────

TEST(ArxmlParser, LoadMissingFile) {
    ArxmlParser p;
    auto msgs = p.load("/nonexistent/file.arxml");
    EXPECT_TRUE(msgs.isEmpty());
    EXPECT_FALSE(p.lastError().isEmpty());
}

TEST(ArxmlParser, ParseOneMessage) {
    QString path = writeArxml(kMinimalArxml);
    ASSERT_FALSE(path.isEmpty());

    ArxmlParser p;
    auto msgs = p.load(path);
    QFile::remove(path);

    ASSERT_EQ(msgs.size(), 1);
    EXPECT_EQ(msgs[0].id, 0x0C0u);
    EXPECT_EQ(msgs[0].name, "EngineData");
    EXPECT_EQ(msgs[0].dlc, 8);
}

TEST(ArxmlParser, ParseTwoSignals) {
    QString path = writeArxml(kMinimalArxml);
    ASSERT_FALSE(path.isEmpty());

    ArxmlParser p;
    auto msgs = p.load(path);
    QFile::remove(path);

    ASSERT_EQ(msgs.size(), 1);
    ASSERT_EQ(msgs[0].sigList.size(), 2);

    QStringList names;
    for (auto &s : msgs[0].sigList) names << s.name;
    EXPECT_TRUE(names.contains("EngineSpeed"));
    EXPECT_TRUE(names.contains("Throttle"));
}

TEST(ArxmlParser, SignalBitLength) {
    QString path = writeArxml(kMinimalArxml);
    ASSERT_FALSE(path.isEmpty());

    ArxmlParser p;
    auto msgs = p.load(path);
    QFile::remove(path);

    ASSERT_EQ(msgs.size(), 1);
    for (auto &s : msgs[0].sigList) {
        if (s.name == "EngineSpeed") EXPECT_EQ(s.length, 16);
        if (s.name == "Throttle")    EXPECT_EQ(s.length, 8);
    }
}

TEST(ArxmlParser, SignalStartPosition) {
    QString path = writeArxml(kMinimalArxml);
    ASSERT_FALSE(path.isEmpty());

    ArxmlParser p;
    auto msgs = p.load(path);
    QFile::remove(path);

    ASSERT_EQ(msgs.size(), 1);
    for (auto &s : msgs[0].sigList) {
        if (s.name == "EngineSpeed") EXPECT_EQ(s.startBit, 0);
        if (s.name == "Throttle")    EXPECT_EQ(s.startBit, 16);
    }
}

TEST(ArxmlParser, TwoFramesTriggers) {
    QString arxml = R"(<?xml version="1.0"?>
<AUTOSAR>
  <AR-PACKAGES><AR-PACKAGE><ELEMENTS>
    <I-SIGNAL><SHORT-NAME>S1</SHORT-NAME><BIT-LENGTH>8</BIT-LENGTH></I-SIGNAL>
    <I-SIGNAL><SHORT-NAME>S2</SHORT-NAME><BIT-LENGTH>8</BIT-LENGTH></I-SIGNAL>
    <I-SIGNAL-I-PDU>
      <SHORT-NAME>PDU1</SHORT-NAME><LENGTH>4</LENGTH>
      <I-SIGNAL-TO-I-PDU-MAPPINGS>
        <I-SIGNAL-TO-I-PDU-MAPPING>
          <I-SIGNAL-REF>/x/S1</I-SIGNAL-REF>
          <START-POSITION>0</START-POSITION>
        </I-SIGNAL-TO-I-PDU-MAPPING>
      </I-SIGNAL-TO-I-PDU-MAPPINGS>
    </I-SIGNAL-I-PDU>
    <I-SIGNAL-I-PDU>
      <SHORT-NAME>PDU2</SHORT-NAME><LENGTH>4</LENGTH>
      <I-SIGNAL-TO-I-PDU-MAPPINGS>
        <I-SIGNAL-TO-I-PDU-MAPPING>
          <I-SIGNAL-REF>/x/S2</I-SIGNAL-REF>
          <START-POSITION>0</START-POSITION>
        </I-SIGNAL-TO-I-PDU-MAPPING>
      </I-SIGNAL-TO-I-PDU-MAPPINGS>
    </I-SIGNAL-I-PDU>
    <FRAME><SHORT-NAME>F1</SHORT-NAME><FRAME-LENGTH>4</FRAME-LENGTH>
      <PDU-TO-FRAME-MAPPINGS><PDU-TO-FRAME-MAPPING>
        <PDU-REF>/x/PDU1</PDU-REF><START-POSITION>0</START-POSITION>
      </PDU-TO-FRAME-MAPPING></PDU-TO-FRAME-MAPPINGS>
    </FRAME>
    <FRAME><SHORT-NAME>F2</SHORT-NAME><FRAME-LENGTH>4</FRAME-LENGTH>
      <PDU-TO-FRAME-MAPPINGS><PDU-TO-FRAME-MAPPING>
        <PDU-REF>/x/PDU2</PDU-REF><START-POSITION>0</START-POSITION>
      </PDU-TO-FRAME-MAPPING></PDU-TO-FRAME-MAPPINGS>
    </FRAME>
    <CAN-FRAME-TRIGGERING>
      <SHORT-NAME>T1</SHORT-NAME>
      <IDENTIFIER>0x100</IDENTIFIER>
      <FRAME-REF>/x/F1</FRAME-REF>
    </CAN-FRAME-TRIGGERING>
    <CAN-FRAME-TRIGGERING>
      <SHORT-NAME>T2</SHORT-NAME>
      <IDENTIFIER>0x200</IDENTIFIER>
      <FRAME-REF>/x/F2</FRAME-REF>
    </CAN-FRAME-TRIGGERING>
  </ELEMENTS></AR-PACKAGE></AR-PACKAGES>
</AUTOSAR>)";

    QString path = writeArxml(arxml);
    ASSERT_FALSE(path.isEmpty());

    ArxmlParser p;
    auto msgs = p.load(path);
    QFile::remove(path);

    ASSERT_EQ(msgs.size(), 2);
    bool has100 = false, has200 = false;
    for (auto &m : msgs) {
        if (m.id == 0x100u) has100 = true;
        if (m.id == 0x200u) has200 = true;
    }
    EXPECT_TRUE(has100);
    EXPECT_TRUE(has200);
}

TEST(ArxmlParser, EmptyArxml) {
    QString arxml = "<?xml version=\"1.0\"?><AUTOSAR/>";
    QString path = writeArxml(arxml);
    ASSERT_FALSE(path.isEmpty());

    ArxmlParser p;
    auto msgs = p.load(path);
    QFile::remove(path);

    EXPECT_TRUE(msgs.isEmpty());
    EXPECT_TRUE(p.lastError().isEmpty());
}

TEST(ArxmlParser, MalformedXml) {
    QString path = writeArxml("<unclosed");
    ASSERT_FALSE(path.isEmpty());

    ArxmlParser p;
    auto msgs = p.load(path);
    QFile::remove(path);
    // Parser may or may not return empty, but should not crash
    (void)msgs;
}
