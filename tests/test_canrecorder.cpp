#include <gtest/gtest.h>
#include "core/CanRecorder.h"
#include "core/CanPlayer.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDataStream>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

// ── QApplication environment ──────────────────────────────────

static int    s_argc   = 1;
static char  *s_argbuf = (char*)"test_canrecorder";
static char **s_argv   = &s_argbuf;

class CanRecorderEnv : public ::testing::Environment {
    QApplication *m_app = nullptr;
public:
    void SetUp()    override {
        if (!QApplication::instance())
            m_app = new QApplication(s_argc, s_argv);
    }
    void TearDown() override { delete m_app; m_app = nullptr; }
};

[[maybe_unused]] static const bool kEnvReg = []() -> bool {
    ::testing::AddGlobalTestEnvironment(new CanRecorderEnv);
    return true;
}();

// Spin event loop until pred() or timeout.
static bool spinUntil(std::function<bool()> pred, int ms = 3000) {
    QDeadlineTimer dl(ms);
    while (!pred() && !dl.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return pred();
}

// ── Helpers ───────────────────────────────────────────────────

static CanFrame makeFrame(uint32_t id, uint8_t dlc,
                          std::initializer_list<uint8_t> bytes = {},
                          uint64_t ts = 0, bool ext = false) {
    CanFrame f{};
    f.id = id; f.dlc = dlc; f.timestamp = ts; f.extended = ext;
    int i = 0;
    for (uint8_t b : bytes) { if (i < 64) f.data[i++] = b; }
    return f;
}

// Record frames into path and stop, returning the file path used
// (may be .mcan for small files; large files → .mcan.zst).
static void recordAndWait(CanRecorder &rec,
                          const QString &path,
                          const QVector<CanFrame> &frames) {
    rec.startRecording(path);
    for (const auto &f : frames) rec.recordFrame(f);
    rec.stopRecording();   // blocks until I/O thread finishes (max 5 s)
}

// ══════════════════════════════════════════════════════════════
// State tests
// ══════════════════════════════════════════════════════════════

TEST(CanRecorderTest, InitialState_NotRecording) {
    CanRecorder rec;
    EXPECT_FALSE(rec.isRecording());
}

TEST(CanRecorderTest, StartRecording_InvalidPath_ReturnsFalse) {
    CanRecorder rec;
    EXPECT_FALSE(rec.startRecording("/nonexistent/dir/out.mcan"));
    EXPECT_FALSE(rec.isRecording());
}

TEST(CanRecorderTest, IsRecording_TrueAfterStart) {
    QTemporaryDir dir;
    CanRecorder rec;
    rec.startRecording(dir.filePath("rec.mcan"));
    EXPECT_TRUE(rec.isRecording());
    rec.stopRecording();
}

TEST(CanRecorderTest, IsRecording_FalseAfterStop) {
    QTemporaryDir dir;
    CanRecorder rec;
    rec.startRecording(dir.filePath("rec.mcan"));
    rec.stopRecording();
    EXPECT_FALSE(rec.isRecording());
}

TEST(CanRecorderTest, DoubleStart_StopsFirstSession) {
    QTemporaryDir dir;
    CanRecorder rec;
    bool ok1 = rec.startRecording(dir.filePath("first.mcan"));
    bool ok2 = rec.startRecording(dir.filePath("second.mcan"));
    EXPECT_TRUE(ok1);
    EXPECT_TRUE(ok2);
    EXPECT_TRUE(rec.isRecording());
    rec.stopRecording();
}

// ══════════════════════════════════════════════════════════════
// Signal tests
// ══════════════════════════════════════════════════════════════

TEST(CanRecorderTest, RecordingStarted_EmitsSignal) {
    QTemporaryDir dir;
    CanRecorder rec;
    QSignalSpy spy(&rec, &CanRecorder::recordingStarted);
    rec.startRecording(dir.filePath("sig.mcan"));
    rec.stopRecording();
    EXPECT_EQ(spy.count(), 1);
}

TEST(CanRecorderTest, RecordingStopped_EmitsCorrectFrameCount) {
    QTemporaryDir dir;
    CanRecorder rec;
    QSignalSpy spy(&rec, &CanRecorder::recordingStopped);

    const int N = 5;
    rec.startRecording(dir.filePath("cnt.mcan"));
    for (int i = 0; i < N; ++i)
        rec.recordFrame(makeFrame(0x100 + i, 2, {0xAA, 0xBB}, 1000ULL * i));
    rec.stopRecording();

    ASSERT_EQ(spy.count(), 1);
    uint64_t reported = spy[0][0].value<uint64_t>();
    EXPECT_EQ(reported, static_cast<uint64_t>(N));
}

TEST(CanRecorderTest, EmptyRecording_StopsCleanly) {
    QTemporaryDir dir;
    CanRecorder rec;
    QSignalSpy spy(&rec, &CanRecorder::recordingStopped);
    rec.startRecording(dir.filePath("empty.mcan"));
    rec.stopRecording();
    EXPECT_EQ(spy.count(), 1);
    EXPECT_FALSE(rec.isRecording());
}

// ══════════════════════════════════════════════════════════════
// File format — small files stay uncompressed (.mcan)
// ══════════════════════════════════════════════════════════════

TEST(CanRecorderTest, SmallFile_McanExists) {
    QTemporaryDir dir;
    QString path = dir.filePath("small.mcan");
    CanRecorder rec;
    rec.startRecording(path);
    rec.recordFrame(makeFrame(0x100, 3, {0xAA, 0xBB, 0xCC}, 1000000));
    rec.stopRecording();
    // Small file → not compressed → .mcan remains
    EXPECT_TRUE(QFile::exists(path));
}

TEST(CanRecorderTest, SmallFile_ValidMagicAndVersion) {
    QTemporaryDir dir;
    QString path = dir.filePath("magic.mcan");
    CanRecorder rec;
    rec.startRecording(path);
    rec.recordFrame(makeFrame(0x200, 2, {0x11, 0x22}, 2000000));
    rec.stopRecording();

    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    QDataStream s(&f);
    s.setByteOrder(QDataStream::LittleEndian);
    quint32 magic = 0, version = 0;
    s >> magic >> version;
    EXPECT_EQ(magic,   0x4E41434Du);  // "MCAN"
    EXPECT_EQ(version, 1u);
}

// ══════════════════════════════════════════════════════════════
// Roundtrip: CanRecorder → CanPlayer
// ══════════════════════════════════════════════════════════════

TEST(CanRecorderTest, Roundtrip_FrameCount) {
    QTemporaryDir dir;
    QString path = dir.filePath("rt.mcan");
    CanRecorder rec;

    QVector<CanFrame> frames = {
        makeFrame(0x100, 3, {0xAA, 0xBB, 0xCC}, 1000000),
        makeFrame(0x200, 2, {0x11, 0x22},        2000000),
        makeFrame(0x300, 1, {0xFF},               3000000),
    };
    recordAndWait(rec, path, frames);

    CanPlayer player;
    int loaded = player.loadFile(path);
    EXPECT_EQ(loaded, frames.size());
}

TEST(CanRecorderTest, Roundtrip_FrameDataPreserved) {
    QTemporaryDir dir;
    QString path = dir.filePath("data.mcan");
    CanRecorder rec;

    CanFrame orig = makeFrame(0x123, 3, {0xDE, 0xAD, 0xBE}, 9876543);
    recordAndWait(rec, path, {orig});

    CanPlayer player;
    ASSERT_EQ(player.loadFile(path), 1);
    const CanFrame loaded = player.frameAt(0);
    EXPECT_EQ(loaded.id,        0x123u);
    EXPECT_EQ(loaded.dlc,       3);
    EXPECT_EQ(loaded.data[0],   0xDE);
    EXPECT_EQ(loaded.data[1],   0xAD);
    EXPECT_EQ(loaded.data[2],   0xBE);
    EXPECT_EQ(loaded.timestamp, 9876543u);
}

TEST(CanRecorderTest, Roundtrip_ExtendedFlagPreserved) {
    QTemporaryDir dir;
    QString path = dir.filePath("ext.mcan");
    CanRecorder rec;

    CanFrame orig = makeFrame(0x18DB33F1, 2, {0xAA, 0xBB}, 5000, true);
    recordAndWait(rec, path, {orig});

    CanPlayer player;
    ASSERT_EQ(player.loadFile(path), 1);
    EXPECT_TRUE(player.frameAt(0).extended);
    EXPECT_EQ(player.frameAt(0).id, 0x18DB33F1u);
}

TEST(CanRecorderTest, Roundtrip_TimestampOrdering) {
    QTemporaryDir dir;
    QString path = dir.filePath("ts.mcan");
    CanRecorder rec;

    QVector<CanFrame> frames;
    for (int i = 0; i < 4; ++i)
        frames.append(makeFrame(0x100, 1, {static_cast<uint8_t>(i)},
                                static_cast<uint64_t>(i) * 1000));
    recordAndWait(rec, path, frames);

    CanPlayer player;
    ASSERT_EQ(player.loadFile(path), 4);
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(player.frameAt(i).timestamp, static_cast<uint64_t>(i) * 1000);
}

TEST(CanRecorderTest, Roundtrip_MultipleIds) {
    QTemporaryDir dir;
    QString path = dir.filePath("multi.mcan");
    CanRecorder rec;

    QVector<CanFrame> frames = {
        makeFrame(0x001, 1, {0x01}, 1000),
        makeFrame(0x7FF, 4, {0xAA, 0xBB, 0xCC, 0xDD}, 2000),
        makeFrame(0x400, 8, {0,1,2,3,4,5,6,7}, 3000),
    };
    recordAndWait(rec, path, frames);

    CanPlayer player;
    ASSERT_EQ(player.loadFile(path), 3);
    EXPECT_EQ(player.frameAt(0).id, 0x001u);
    EXPECT_EQ(player.frameAt(1).id, 0x7FFu);
    EXPECT_EQ(player.frameAt(2).id, 0x400u);
}
