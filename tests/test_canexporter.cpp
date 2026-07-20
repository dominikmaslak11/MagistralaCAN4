#include <gtest/gtest.h>
#include "core/CanExporter.h"
#include "core/CanPlayer.h"
#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>
#include <QDataStream>

// ── Helpers ──────────────────────────────────────────────────

static CanFrame makeFrame(uint32_t id, uint8_t dlc,
                          std::initializer_list<uint8_t> bytes = {},
                          uint64_t ts = 0, bool ext = false,
                          bool fd = false) {
    CanFrame f{};
    f.id        = id;
    f.dlc       = dlc;
    f.timestamp = ts;
    f.extended  = ext;
    f.fd        = fd;
    int i = 0;
    for (uint8_t b : bytes) { if (i < 64) f.data[i++] = b; }
    return f;
}

static QVector<CanFrame> sampleFrames() {
    return {
        makeFrame(0x100, 3, {0xAA, 0xBB, 0xCC}, 1000000),
        makeFrame(0x200, 2, {0x11, 0x22},        2000000),
        makeFrame(0x18DB33F1, 8, {0x02, 0x01, 0x0C, 0, 0, 0, 0, 0}, 3000000, true),
    };
}

// ── CSV export ───────────────────────────────────────────────

TEST(CanExporterTest, ExportCsv_CreatesFile) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString path = dir.filePath("out.csv");
    EXPECT_TRUE(CanExporter::exportCsv(path, sampleFrames()));
    EXPECT_TRUE(QFile::exists(path));
}

TEST(CanExporterTest, ExportCsv_HasHeader) {
    QTemporaryDir dir;
    QString path = dir.filePath("out.csv");
    CanExporter::exportCsv(path, sampleFrames());
    QFile f(path); f.open(QIODevice::ReadOnly | QIODevice::Text);
    QString firstLine = QTextStream(&f).readLine();
    EXPECT_TRUE(firstLine.startsWith("Index"));
    EXPECT_TRUE(firstLine.contains("Timestamp_us"));
    EXPECT_TRUE(firstLine.contains("ID_hex"));
    EXPECT_TRUE(firstLine.contains("DLC"));
    EXPECT_TRUE(firstLine.contains("Data_hex"));
}

TEST(CanExporterTest, ExportCsv_CorrectRowCount) {
    QTemporaryDir dir;
    QString path = dir.filePath("out.csv");
    auto frames = sampleFrames();
    CanExporter::exportCsv(path, frames);
    QFile f(path); f.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream s(&f);
    int lines = 0;
    while (!s.atEnd()) { s.readLine(); lines++; }
    EXPECT_EQ(lines, frames.size() + 1);  // header + data rows
}

TEST(CanExporterTest, ExportCsv_ExtendedFlagInOutput) {
    QTemporaryDir dir;
    QString path = dir.filePath("out.csv");
    CanExporter::exportCsv(path, sampleFrames());
    QFile f(path); f.open(QIODevice::ReadOnly | QIODevice::Text);
    QString content = QTextStream(&f).readAll();
    // Extended frame (0x18DB33F1) should have extended=1
    EXPECT_TRUE(content.contains(",1,"));
}

TEST(CanExporterTest, ExportCsv_EmptyFrames_OnlyHeader) {
    QTemporaryDir dir;
    QString path = dir.filePath("empty.csv");
    EXPECT_TRUE(CanExporter::exportCsv(path, {}));
    QFile f(path); f.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream s(&f);
    int lines = 0;
    while (!s.atEnd()) { s.readLine(); lines++; }
    EXPECT_EQ(lines, 1);  // only header
}

TEST(CanExporterTest, ExportCsv_InvalidPath_ReturnsFalse) {
    EXPECT_FALSE(CanExporter::exportCsv("/nonexistent/path/out.csv", sampleFrames()));
}

// ── Candump export ────────────────────────────────────────────

TEST(CanExporterTest, ExportCandump_CreatesFile) {
    QTemporaryDir dir;
    QString path = dir.filePath("out.log");
    EXPECT_TRUE(CanExporter::exportCandump(path, sampleFrames()));
    EXPECT_TRUE(QFile::exists(path));
}

TEST(CanExporterTest, ExportCandump_LinePrefixFormat) {
    QTemporaryDir dir;
    QString path = dir.filePath("out.log");
    CanExporter::exportCandump(path, sampleFrames(), "vcan0");
    QFile f(path); f.open(QIODevice::ReadOnly | QIODevice::Text);
    QString firstLine = QTextStream(&f).readLine();
    // Expected: (timestamp) vcan0 ID#dlcDATA
    EXPECT_TRUE(firstLine.startsWith("("));
    EXPECT_TRUE(firstLine.contains("vcan0"));
    EXPECT_TRUE(firstLine.contains("#"));
}

TEST(CanExporterTest, ExportCandump_CorrectLineCount) {
    QTemporaryDir dir;
    QString path = dir.filePath("out.log");
    auto frames = sampleFrames();
    CanExporter::exportCandump(path, frames);
    QFile f(path); f.open(QIODevice::ReadOnly | QIODevice::Text);
    QTextStream s(&f);
    int lines = 0;
    while (!s.atEnd()) { QString l = s.readLine(); if (!l.trimmed().isEmpty()) lines++; }
    EXPECT_EQ(lines, frames.size());
}

TEST(CanExporterTest, ExportCandump_FdFrameUsesDblHash) {
    QTemporaryDir dir;
    QString path = dir.filePath("fd.log");
    CanFrame fd = makeFrame(0x100, 8, {0}, 0, false, true);
    CanExporter::exportCandump(path, {fd});
    QFile f(path); f.open(QIODevice::ReadOnly | QIODevice::Text);
    QString line = QTextStream(&f).readAll();
    EXPECT_TRUE(line.contains("##"));
}

TEST(CanExporterTest, ExportCandump_InvalidPath_ReturnsFalse) {
    EXPECT_FALSE(CanExporter::exportCandump("/bad/path.log", sampleFrames()));
}

// ── PCAP export ───────────────────────────────────────────────

TEST(CanExporterTest, ExportPcap_CreatesFile) {
    QTemporaryDir dir;
    QString path = dir.filePath("out.pcap");
    EXPECT_TRUE(CanExporter::exportPcap(path, sampleFrames()));
    EXPECT_TRUE(QFile::exists(path));
}

TEST(CanExporterTest, ExportPcap_ValidMagic) {
    QTemporaryDir dir;
    QString path = dir.filePath("out.pcap");
    CanExporter::exportPcap(path, sampleFrames());
    QFile f(path); f.open(QIODevice::ReadOnly);
    // PCAP global header magic: 0xa1b2c3d4 (little-endian) or 0xd4c3b2a1
    uint32_t magic = 0;
    f.read(reinterpret_cast<char*>(&magic), 4);
    EXPECT_TRUE(magic == 0xa1b2c3d4u || magic == 0xd4c3b2a1u);
}

TEST(CanExporterTest, ExportPcap_EmptyFrames) {
    QTemporaryDir dir;
    QString path = dir.filePath("empty.pcap");
    EXPECT_TRUE(CanExporter::exportPcap(path, {}));
    EXPECT_TRUE(QFile::exists(path));
}

// ── MCAN binary export + roundtrip ───────────────────────────

TEST(CanExporterTest, ExportMcan_CreatesFile) {
    QTemporaryDir dir;
    QString path = dir.filePath("out.mcan");
    EXPECT_TRUE(CanExporter::exportMcan(path, sampleFrames()));
    EXPECT_TRUE(QFile::exists(path));
}

TEST(CanExporterTest, ExportMcan_ValidMagic) {
    QTemporaryDir dir;
    QString path = dir.filePath("out.mcan");
    CanExporter::exportMcan(path, sampleFrames());
    QFile f(path); f.open(QIODevice::ReadOnly);
    QDataStream s(&f);
    s.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0, version = 0;
    s >> magic >> version;
    EXPECT_EQ(magic,   0x4E41434Du);  // "MCAN"
    EXPECT_EQ(version, 1u);
}

TEST(CanExporterTest, ExportMcan_RoundtripViaCanPlayer) {
    QTemporaryDir dir;
    QString path = dir.filePath("rt.mcan");
    auto frames = sampleFrames();
    ASSERT_TRUE(CanExporter::exportMcan(path, frames));

    CanPlayer player;
    int loaded = player.loadFile(path);
    EXPECT_EQ(loaded, frames.size());
    EXPECT_EQ(player.frameCount(), frames.size());
}

TEST(CanExporterTest, ExportMcan_FrameDataPreserved) {
    QTemporaryDir dir;
    QString path = dir.filePath("data.mcan");
    CanFrame orig = makeFrame(0x123, 3, {0xDE, 0xAD, 0xBE}, 9999);
    ASSERT_TRUE(CanExporter::exportMcan(path, {orig}));

    CanPlayer player;
    player.loadFile(path);
    ASSERT_EQ(player.frameCount(), 1);
    const CanFrame &loaded = player.frameAt(0);
    EXPECT_EQ(loaded.id,        0x123u);
    EXPECT_EQ(loaded.dlc,       3);
    EXPECT_EQ(loaded.data[0],   0xDE);
    EXPECT_EQ(loaded.data[1],   0xAD);
    EXPECT_EQ(loaded.data[2],   0xBE);
    EXPECT_EQ(loaded.timestamp, 9999u);
}

TEST(CanExporterTest, ExportMcan_ExtendedFlagPreserved) {
    QTemporaryDir dir;
    QString path = dir.filePath("ext.mcan");
    CanFrame f = makeFrame(0x18DB33F1, 2, {0xAA, 0xBB}, 0, true);
    ASSERT_TRUE(CanExporter::exportMcan(path, {f}));

    CanPlayer player;
    player.loadFile(path);
    ASSERT_EQ(player.frameCount(), 1);
    EXPECT_TRUE(player.frameAt(0).extended);
}

TEST(CanExporterTest, ExportMcan_InvalidPath_ReturnsFalse) {
    EXPECT_FALSE(CanExporter::exportMcan("/bad/path.mcan", sampleFrames()));
}
