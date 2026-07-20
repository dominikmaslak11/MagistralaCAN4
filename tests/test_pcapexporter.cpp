#include <gtest/gtest.h>
#include <QTemporaryFile>
#include <QFile>
#include "core/PcapExporter.h"
#include "core/CanFrame.h"

static CanFrame makeFrame(uint32_t id, uint8_t dlc, bool ext = false) {
    CanFrame f{};
    f.id = id; f.dlc = dlc; f.extended = ext;
    for (int i = 0; i < dlc && i < 8; ++i) f.data[i] = static_cast<uint8_t>(i * 0x11);
    return f;
}

static uint32_t readU32LE(const QByteArray &ba, int offset) {
    return static_cast<uint8_t>(ba[offset])
         | (static_cast<uint8_t>(ba[offset+1]) << 8)
         | (static_cast<uint8_t>(ba[offset+2]) << 16)
         | (static_cast<uint8_t>(ba[offset+3]) << 24);
}

TEST(PcapExporter, EmptyFrameListCreatesValidHeader) {
    QTemporaryFile tmp;
    tmp.open();
    QString path = tmp.fileName();
    tmp.close();

    ASSERT_TRUE(PcapExporter::exportFrames(path, {}));

    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    QByteArray data = f.readAll();
    // Global header is 24 bytes
    ASSERT_EQ(data.size(), 24);
    EXPECT_EQ(readU32LE(data, 0), 0xa1b2c3d4u); // magic
    EXPECT_EQ(readU32LE(data, 20), 227u);        // LINKTYPE_CAN_SOCKETCAN
}

TEST(PcapExporter, SingleFrameCorrectSize) {
    QTemporaryFile tmp;
    tmp.open(); QString path = tmp.fileName(); tmp.close();

    ASSERT_TRUE(PcapExporter::exportFrames(path, {makeFrame(0x100, 8)}));

    QFile f(path); f.open(QIODevice::ReadOnly);
    QByteArray data = f.readAll();
    // 24 (global header) + 16 (packet header) + 16 (socketcan payload) = 56
    EXPECT_EQ(data.size(), 56);
}

TEST(PcapExporter, MultipleFrames) {
    QTemporaryFile tmp;
    tmp.open(); QString path = tmp.fileName(); tmp.close();

    std::vector<CanFrame> frames = {
        makeFrame(0x100, 8), makeFrame(0x200, 4), makeFrame(0x300, 1)
    };
    ASSERT_TRUE(PcapExporter::exportFrames(path, frames));

    QFile f(path); f.open(QIODevice::ReadOnly);
    QByteArray data = f.readAll();
    // 24 + 3 * (16 + 16) = 24 + 96 = 120
    EXPECT_EQ(data.size(), 120);
}

TEST(PcapExporter, CanIdWrittenCorrectly) {
    QTemporaryFile tmp;
    tmp.open(); QString path = tmp.fileName(); tmp.close();

    ASSERT_TRUE(PcapExporter::exportFrames(path, {makeFrame(0x1AB, 8, false)}));

    QFile f(path); f.open(QIODevice::ReadOnly);
    QByteArray data = f.readAll();
    // First packet payload starts at offset 24 (global) + 16 (pkt header) = 40
    uint32_t canId = readU32LE(data, 40);
    EXPECT_EQ(canId & 0x1FFFFFFFu, 0x1ABu); // lower 29 bits = CAN ID
    EXPECT_EQ(canId & 0x80000000u, 0u);      // EFF flag not set (standard frame)
}

TEST(PcapExporter, ExtendedFrameFlagSet) {
    QTemporaryFile tmp;
    tmp.open(); QString path = tmp.fileName(); tmp.close();

    ASSERT_TRUE(PcapExporter::exportFrames(path, {makeFrame(0x18FF1234, 8, true)}));

    QFile f(path); f.open(QIODevice::ReadOnly);
    QByteArray data = f.readAll();
    uint32_t canId = readU32LE(data, 40);
    EXPECT_NE(canId & 0x80000000u, 0u); // EFF flag set
}

TEST(PcapExporter, TimestampsWrittenCorrectly) {
    QTemporaryFile tmp;
    tmp.open(); QString path = tmp.fileName(); tmp.close();

    std::vector<uint64_t> ts = {1'500'000'000ULL * 1000000ULL + 123456ULL}; // some epoch µs
    ASSERT_TRUE(PcapExporter::exportFrames(path, {makeFrame(0x1, 1)}, ts));

    QFile f(path); f.open(QIODevice::ReadOnly);
    QByteArray data = f.readAll();
    uint32_t tsSec  = readU32LE(data, 24);
    uint32_t tsUsec = readU32LE(data, 28);
    EXPECT_EQ(tsSec,  static_cast<uint32_t>(ts[0] / 1'000'000ULL));
    EXPECT_EQ(tsUsec, static_cast<uint32_t>(ts[0] % 1'000'000ULL));
}

TEST(PcapExporter, PayloadDataCorrect) {
    QTemporaryFile tmp;
    tmp.open(); QString path = tmp.fileName(); tmp.close();

    CanFrame f = makeFrame(0x100, 4);
    f.data[0]=0xDE; f.data[1]=0xAD; f.data[2]=0xBE; f.data[3]=0xEF;
    ASSERT_TRUE(PcapExporter::exportFrames(path, {f}));

    QFile file(path); file.open(QIODevice::ReadOnly);
    QByteArray data = file.readAll();
    // payload data starts at 40 (global+pkt hdr) + 8 (can_id + len + pad) = 48
    EXPECT_EQ(static_cast<uint8_t>(data[48]), 0xDE);
    EXPECT_EQ(static_cast<uint8_t>(data[49]), 0xAD);
    EXPECT_EQ(static_cast<uint8_t>(data[50]), 0xBE);
    EXPECT_EQ(static_cast<uint8_t>(data[51]), 0xEF);
}

TEST(PcapExporter, BadPathReturnsFalse) {
    EXPECT_FALSE(PcapExporter::exportFrames("/nonexistent/path/test.pcap", {}));
}

TEST(PcapExporter, PcapVersionFields) {
    QTemporaryFile tmp;
    tmp.open(); QString path = tmp.fileName(); tmp.close();

    ASSERT_TRUE(PcapExporter::exportFrames(path, {}));

    QFile f(path); f.open(QIODevice::ReadOnly);
    QByteArray data = f.readAll();
    // major at offset 4 (uint16 LE), minor at offset 6
    uint16_t major = static_cast<uint8_t>(data[4]) | (static_cast<uint8_t>(data[5]) << 8);
    uint16_t minor = static_cast<uint8_t>(data[6]) | (static_cast<uint8_t>(data[7]) << 8);
    EXPECT_EQ(major, 2u);
    EXPECT_EQ(minor, 4u);
}

TEST(PcapExporter, SnaplenField) {
    QTemporaryFile tmp;
    tmp.open(); QString path = tmp.fileName(); tmp.close();

    ASSERT_TRUE(PcapExporter::exportFrames(path, {}));

    QFile f(path); f.open(QIODevice::ReadOnly);
    QByteArray data = f.readAll();
    // snaplen at offset 16
    EXPECT_EQ(readU32LE(data, 16), 65535u);
}

TEST(PcapExporter, FdFlagSet) {
    QTemporaryFile tmp;
    tmp.open(); QString path = tmp.fileName(); tmp.close();

    CanFrame frame = makeFrame(0x100, 8);
    frame.fd = true;
    ASSERT_TRUE(PcapExporter::exportFrames(path, {frame}));

    QFile f(path); f.open(QIODevice::ReadOnly);
    QByteArray data = f.readAll();
    uint32_t canId = readU32LE(data, 40);
    EXPECT_NE(canId & 0x40000000u, 0u); // FD flag set
}

TEST(PcapExporter, DlcCappedAt8ForStandardFrame) {
    QTemporaryFile tmp;
    tmp.open(); QString path = tmp.fileName(); tmp.close();

    // DLC > 8 (shouldn't happen in classic CAN but let's guard)
    CanFrame frame = makeFrame(0x1, 8);
    frame.dlc = 12; // oversized
    ASSERT_TRUE(PcapExporter::exportFrames(path, {frame}));

    QFile f(path); f.open(QIODevice::ReadOnly);
    QByteArray data = f.readAll();
    // len field is at payload[4] = file offset 40+4 = 44
    uint8_t len = static_cast<uint8_t>(data[44]);
    EXPECT_EQ(len, 8u); // capped at 8
}

TEST(PcapExporter, ZeroDlcFrame) {
    QTemporaryFile tmp;
    tmp.open(); QString path = tmp.fileName(); tmp.close();

    ASSERT_TRUE(PcapExporter::exportFrames(path, {makeFrame(0x1, 0)}));

    QFile f(path); f.open(QIODevice::ReadOnly);
    QByteArray data = f.readAll();
    EXPECT_EQ(data.size(), 56); // still full 16-byte socketcan record
    EXPECT_EQ(static_cast<uint8_t>(data[44]), 0u); // len = 0
}

TEST(PcapExporter, SyntheticTimestamps) {
    QTemporaryFile tmp;
    tmp.open(); QString path = tmp.fileName(); tmp.close();

    // No timestamps provided → frames get synthetic 1ms spacing
    std::vector<CanFrame> frames = {makeFrame(0x1, 1), makeFrame(0x2, 1), makeFrame(0x3, 1)};
    ASSERT_TRUE(PcapExporter::exportFrames(path, frames));

    QFile f(path); f.open(QIODevice::ReadOnly);
    QByteArray data = f.readAll();

    // Frame 0: ts = 0 µs → sec=0, usec=0
    EXPECT_EQ(readU32LE(data, 24), 0u);
    EXPECT_EQ(readU32LE(data, 28), 0u);

    // Frame 1: ts = 1000 µs → sec=0, usec=1000
    EXPECT_EQ(readU32LE(data, 24 + 32), 0u);
    EXPECT_EQ(readU32LE(data, 28 + 32), 1000u);

    // Frame 2: ts = 2000 µs → sec=0, usec=2000
    EXPECT_EQ(readU32LE(data, 24 + 64), 0u);
    EXPECT_EQ(readU32LE(data, 28 + 64), 2000u);
}

TEST(PcapExporter, WriteHeader_CreatesValidHeader) {
    QTemporaryFile tmp;
    tmp.open(); QString path = tmp.fileName(); tmp.close();

    ASSERT_TRUE(PcapExporter::writeHeader(path));

    QFile f(path); f.open(QIODevice::ReadOnly);
    QByteArray data = f.readAll();
    ASSERT_EQ(data.size(), 24);
    EXPECT_EQ(readU32LE(data, 0),  0xa1b2c3d4u); // magic
    EXPECT_EQ(readU32LE(data, 20), 227u);         // linktype
}

TEST(PcapExporter, WriteHeader_BadPathReturnsFalse) {
    EXPECT_FALSE(PcapExporter::writeHeader("/nonexistent/dir/test.pcap"));
}
