#include <gtest/gtest.h>
#include "core/Mdf4Writer.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

// ── QApplication environment ──────────────────────────────────

static int    s_argc   = 1;
static char  *s_argbuf = (char*)"test_mdf4writer";
static char **s_argv   = &s_argbuf;

class Mdf4WriterEnv : public ::testing::Environment {
    QApplication *m_app = nullptr;
public:
    void SetUp()    override {
        if (!QApplication::instance())
            m_app = new QApplication(s_argc, s_argv);
    }
    void TearDown() override { delete m_app; m_app = nullptr; }
};

[[maybe_unused]] static const bool kEnvReg = []() -> bool {
    ::testing::AddGlobalTestEnvironment(new Mdf4WriterEnv);
    return true;
}();

static bool spinUntil(std::function<bool()> pred, int ms = 3000) {
    QDeadlineTimer dl(ms);
    while (!pred() && !dl.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return pred();
}

// ── Helpers ───────────────────────────────────────────────────

static CanFrame makeFrame(uint32_t id, uint8_t dlc,
                          std::initializer_list<uint8_t> bytes = {},
                          uint64_t ts = 0) {
    CanFrame f{};
    f.id = id; f.dlc = dlc; f.timestamp = ts;
    int i = 0;
    for (uint8_t b : bytes) { if (i < 64) f.data[i++] = b; }
    return f;
}

static void writeAndWait(Mdf4Writer &w,
                         const QString &path,
                         const QVector<CanFrame> &frames) {
    w.start(path);
    for (const auto &f : frames) w.recordFrame(f);
    w.stop();
}

// ══════════════════════════════════════════════════════════════
// State tests
// ══════════════════════════════════════════════════════════════

TEST(Mdf4WriterTest, InitialState_NotRecording) {
    Mdf4Writer w;
    EXPECT_FALSE(w.isRecording());
}

TEST(Mdf4WriterTest, Start_InvalidPath_ReturnsFalse) {
    Mdf4Writer w;
    EXPECT_FALSE(w.start("/nonexistent/dir/out.mf4"));
    EXPECT_FALSE(w.isRecording());
}

TEST(Mdf4WriterTest, IsRecording_TrueAfterStart) {
    QTemporaryDir dir;
    Mdf4Writer w;
    w.start(dir.filePath("rec.mf4"));
    EXPECT_TRUE(w.isRecording());
    w.stop();
}

TEST(Mdf4WriterTest, IsRecording_FalseAfterStop) {
    QTemporaryDir dir;
    Mdf4Writer w;
    w.start(dir.filePath("rec.mf4"));
    w.stop();
    EXPECT_FALSE(w.isRecording());
}

TEST(Mdf4WriterTest, DoubleStart_StopsFirstSession) {
    QTemporaryDir dir;
    Mdf4Writer w;
    bool ok1 = w.start(dir.filePath("first.mf4"));
    bool ok2 = w.start(dir.filePath("second.mf4"));
    EXPECT_TRUE(ok1);
    EXPECT_TRUE(ok2);
    EXPECT_TRUE(w.isRecording());
    w.stop();
}

// ══════════════════════════════════════════════════════════════
// Signal tests
// ══════════════════════════════════════════════════════════════

TEST(Mdf4WriterTest, RecordingStopped_EmitsSignal) {
    QTemporaryDir dir;
    Mdf4Writer w;
    QSignalSpy spy(&w, &Mdf4Writer::recordingStopped);
    w.start(dir.filePath("sig.mf4"));
    w.stop();
    ASSERT_EQ(spy.count(), 1);
}

TEST(Mdf4WriterTest, RecordingStopped_EmitsCorrectFrameCount) {
    QTemporaryDir dir;
    Mdf4Writer w;
    QSignalSpy spy(&w, &Mdf4Writer::recordingStopped);

    const int N = 7;
    w.start(dir.filePath("cnt.mf4"));
    for (int i = 0; i < N; ++i)
        w.recordFrame(makeFrame(0x100, 2, {0xAA, 0xBB}, 1000ULL * i));
    w.stop();

    ASSERT_EQ(spy.count(), 1);
    uint64_t count = spy[0][1].value<uint64_t>();
    EXPECT_EQ(count, static_cast<uint64_t>(N));
}

TEST(Mdf4WriterTest, RecordingStopped_ReportsFilePath) {
    QTemporaryDir dir;
    QString path = dir.filePath("path_check.mf4");
    Mdf4Writer w;
    QSignalSpy spy(&w, &Mdf4Writer::recordingStopped);
    w.start(path);
    w.stop();
    ASSERT_EQ(spy.count(), 1);
    QString reported = spy[0][0].toString();
    EXPECT_EQ(reported, path);
}

TEST(Mdf4WriterTest, EmptyRecording_StopsCleanly) {
    QTemporaryDir dir;
    Mdf4Writer w;
    QSignalSpy spy(&w, &Mdf4Writer::recordingStopped);
    w.start(dir.filePath("empty.mf4"));
    w.stop();
    EXPECT_EQ(spy.count(), 1);
    EXPECT_FALSE(w.isRecording());
}

// ══════════════════════════════════════════════════════════════
// File format tests
// ══════════════════════════════════════════════════════════════

TEST(Mdf4WriterTest, CreatesFile) {
    QTemporaryDir dir;
    QString path = dir.filePath("out.mf4");
    Mdf4Writer w;
    writeAndWait(w, path, {makeFrame(0x100, 2, {0x11, 0x22}, 1000)});
    EXPECT_TRUE(QFile::exists(path));
}

TEST(Mdf4WriterTest, FileHasIdblockMagic) {
    QTemporaryDir dir;
    QString path = dir.filePath("magic.mf4");
    Mdf4Writer w;
    writeAndWait(w, path, {makeFrame(0x100, 1, {0xAA}, 1000)});

    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    QByteArray header = f.read(8);
    // First 4 bytes of IDBLOCK should be "IDBL"
    EXPECT_EQ(header[0], 'I');
    EXPECT_EQ(header[1], 'D');
    EXPECT_EQ(header[2], 'B');
    EXPECT_EQ(header[3], 'L');
}

TEST(Mdf4WriterTest, FileHasCnblockAfterIdblock) {
    QTemporaryDir dir;
    QString path = dir.filePath("cn.mf4");
    Mdf4Writer w;
    // One CAN ID → one CNBLOCK expected in the file
    writeAndWait(w, path, {makeFrame(0x200, 2, {0x11, 0x22}, 1000)});

    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    QByteArray data = f.readAll();
    EXPECT_TRUE(data.contains("CNBLOCK"));
}

TEST(Mdf4WriterTest, FileHasDtblockForData) {
    QTemporaryDir dir;
    QString path = dir.filePath("dt.mf4");
    Mdf4Writer w;
    writeAndWait(w, path, {makeFrame(0x300, 3, {0xAA, 0xBB, 0xCC}, 2000)});

    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    QByteArray data = f.readAll();
    EXPECT_TRUE(data.contains("DTBL"));
}

TEST(Mdf4WriterTest, MultipleIds_MultipleCnblocks) {
    QTemporaryDir dir;
    QString path = dir.filePath("multi.mf4");
    Mdf4Writer w;
    QVector<CanFrame> frames = {
        makeFrame(0x100, 1, {0x01}, 1000),
        makeFrame(0x200, 1, {0x02}, 2000),
        makeFrame(0x300, 1, {0x03}, 3000),
    };
    writeAndWait(w, path, frames);

    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    QByteArray data = f.readAll();

    // Three distinct CAN IDs → three CNBLOCK entries
    int count = 0;
    int pos = 0;
    while ((pos = data.indexOf("CNBLOCK", pos)) != -1) { ++count; ++pos; }
    EXPECT_EQ(count, 3);
}

TEST(Mdf4WriterTest, ChannelNameContainsCanId) {
    QTemporaryDir dir;
    QString path = dir.filePath("name.mf4");
    Mdf4Writer w;
    writeAndWait(w, path, {makeFrame(0x1AB, 1, {0xFF}, 1000)});

    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    QByteArray data = f.readAll();
    // Channel name should contain "1ab" (hex)
    EXPECT_TRUE(data.contains("1ab") || data.contains("1AB"));
}

TEST(Mdf4WriterTest, EmptyFile_StillHasIdblock) {
    QTemporaryDir dir;
    QString path = dir.filePath("empty_id.mf4");
    Mdf4Writer w;
    writeAndWait(w, path, {});  // no frames

    EXPECT_TRUE(QFile::exists(path));
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    QByteArray header = f.read(4);
    EXPECT_EQ(header, QByteArray("IDBL"));
}

TEST(Mdf4WriterTest, LargeFrameCount_AllRecorded) {
    QTemporaryDir dir;
    QString path = dir.filePath("large.mf4");
    Mdf4Writer w;
    QSignalSpy spy(&w, &Mdf4Writer::recordingStopped);

    const int N = 300;  // more than MAX_CHAN_SAMPLES=256 → flush triggered
    w.start(path);
    for (int i = 0; i < N; ++i)
        w.recordFrame(makeFrame(0x100, 1, {static_cast<uint8_t>(i % 256)},
                                static_cast<uint64_t>(i) * 1000));
    w.stop();

    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy[0][1].value<uint64_t>(), static_cast<uint64_t>(N));
}
