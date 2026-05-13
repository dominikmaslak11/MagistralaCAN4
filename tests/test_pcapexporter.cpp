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
