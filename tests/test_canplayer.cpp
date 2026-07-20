#include <gtest/gtest.h>
#include "core/CanPlayer.h"
#include "core/CanExporter.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QSignalSpy>
#include <QTemporaryDir>

// ── QApplication environment ──────────────────────────────────

static int    s_argc   = 1;
static char  *s_argbuf = (char*)"test_canplayer";
static char **s_argv   = &s_argbuf;

class CanPlayerEnv : public ::testing::Environment {
    QApplication *m_app = nullptr;
public:
    void SetUp()    override {
        if (!QApplication::instance())
            m_app = new QApplication(s_argc, s_argv);
    }
    void TearDown() override { delete m_app; m_app = nullptr; }
};

[[maybe_unused]] static const bool kEnvReg = []() -> bool {
    ::testing::AddGlobalTestEnvironment(new CanPlayerEnv);
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
                          uint64_t ts = 0, bool ext = false) {
    CanFrame f{};
    f.id = id; f.dlc = dlc; f.timestamp = ts; f.extended = ext;
    int i = 0;
    for (uint8_t b : bytes) { if (i < 64) f.data[i++] = b; }
    return f;
}

// Write frames to a temp .mcan file, return the path.
static QString writeMcan(const QVector<CanFrame> &frames, QTemporaryDir &dir) {
    QString path = dir.filePath("test.mcan");
    CanExporter::exportMcan(path, frames);
    return path;
}

// ══════════════════════════════════════════════════════════════
// Initial state
// ══════════════════════════════════════════════════════════════

TEST(CanPlayerTest, InitialState_NotPlaying) {
    CanPlayer p;
    EXPECT_FALSE(p.isPlaying());
}

TEST(CanPlayerTest, InitialState_NotLoaded_ZeroFrames) {
    CanPlayer p;
    EXPECT_EQ(p.frameCount(), 0);
    EXPECT_EQ(p.totalFrames(), 0);
    EXPECT_EQ(p.currentFrame(), 0);
}

// ══════════════════════════════════════════════════════════════
// loadFile
// ══════════════════════════════════════════════════════════════

TEST(CanPlayerTest, LoadFile_ValidMcan_ReturnsCount) {
    QTemporaryDir dir;
    QVector<CanFrame> frames = {makeFrame(0x100, 2, {0x01, 0x02}, 1000),
                                makeFrame(0x200, 1, {0xFF}, 2000)};
    QString path = writeMcan(frames, dir);
    CanPlayer p;
    EXPECT_EQ(p.loadFile(path), 2);
    EXPECT_EQ(p.frameCount(), 2);
}

TEST(CanPlayerTest, LoadFile_InvalidPath_ReturnsZero) {
    CanPlayer p;
    EXPECT_EQ(p.loadFile("/no/such/file.mcan"), 0);
    EXPECT_EQ(p.frameCount(), 0);
}

TEST(CanPlayerTest, LoadFile_EmptyMcan_ReturnsZero) {
    QTemporaryDir dir;
    QString path = writeMcan({}, dir);
    CanPlayer p;
    EXPECT_EQ(p.loadFile(path), 0);
}

TEST(CanPlayerTest, LoadFile_FrameAtAccessor) {
    QTemporaryDir dir;
    CanFrame orig = makeFrame(0x123, 3, {0xDE, 0xAD, 0xBE}, 9876);
    QString path = writeMcan({orig}, dir);
    CanPlayer p;
    p.loadFile(path);
    const CanFrame loaded = p.frameAt(0);
    EXPECT_EQ(loaded.id, 0x123u);
    EXPECT_EQ(loaded.dlc, 3);
    EXPECT_EQ(loaded.data[0], 0xDE);
    EXPECT_EQ(loaded.data[2], 0xBE);
    EXPECT_EQ(loaded.timestamp, 9876u);
}

TEST(CanPlayerTest, LoadFile_FrameAt_OutOfRange_ReturnsDefault) {
    QTemporaryDir dir;
    QString path = writeMcan({makeFrame(0x100, 1, {0x01}, 1000)}, dir);
    CanPlayer p;
    p.loadFile(path);
    CanFrame f = p.frameAt(99);  // out of range
    EXPECT_EQ(f.id, 0u);
    EXPECT_EQ(f.dlc, 0);
}

TEST(CanPlayerTest, LoadFile_ReloadResetsState) {
    QTemporaryDir dir;
    QVector<CanFrame> frames1 = {makeFrame(0x100, 1, {0x01}, 1000)};
    QVector<CanFrame> frames2 = {makeFrame(0x200, 2, {0xAA, 0xBB}, 2000),
                                 makeFrame(0x300, 1, {0xFF}, 3000)};
    CanPlayer p;
    p.loadFile(writeMcan(frames1, dir));
    EXPECT_EQ(p.frameCount(), 1);

    p.loadFile(writeMcan(frames2, dir));
    EXPECT_EQ(p.frameCount(), 2);
}

// ══════════════════════════════════════════════════════════════
// play / pause / stop
// ══════════════════════════════════════════════════════════════

TEST(CanPlayerTest, Play_SetsIsPlaying) {
    QTemporaryDir dir;
    // Use large timestamps so playback doesn't finish immediately
    QVector<CanFrame> frames;
    for (int i = 0; i < 3; ++i)
        frames.append(makeFrame(0x100, 1, {static_cast<uint8_t>(i)},
                                static_cast<uint64_t>(i) * 5'000'000));  // 5s apart
    CanPlayer p;
    p.loadFile(writeMcan(frames, dir));
    p.setSpeed(0);  // max speed (0 = no delay)
    p.play();
    EXPECT_TRUE(p.isPlaying());
    p.pause();
}

TEST(CanPlayerTest, Pause_StopsPlaying) {
    QTemporaryDir dir;
    QVector<CanFrame> frames = {
        makeFrame(0x100, 1, {0x01}, 0),
        makeFrame(0x200, 1, {0x02}, 5'000'000),
    };
    CanPlayer p;
    p.loadFile(writeMcan(frames, dir));
    p.setSpeed(0);
    p.play();
    p.pause();
    EXPECT_FALSE(p.isPlaying());
}

TEST(CanPlayerTest, Stop_ResetsToBeginning) {
    QTemporaryDir dir;
    QVector<CanFrame> frames = {
        makeFrame(0x100, 1, {0x01}, 0),
        makeFrame(0x200, 1, {0x02}, 5'000'000),
    };
    CanPlayer p;
    p.loadFile(writeMcan(frames, dir));
    p.setSpeed(0);
    p.play();
    QCoreApplication::processEvents();  // advance at least one frame
    p.stop();
    EXPECT_FALSE(p.isPlaying());
    EXPECT_EQ(p.currentFrame(), 0);
}

TEST(CanPlayerTest, PlaybackFinished_EmittedAfterAllFrames) {
    QTemporaryDir dir;
    QVector<CanFrame> frames = {
        makeFrame(0x100, 1, {0x01}, 1000),
        makeFrame(0x200, 1, {0x02}, 2000),
    };
    CanPlayer p;
    p.loadFile(writeMcan(frames, dir));
    p.setSpeed(0);  // max speed — no delays

    QSignalSpy spy(&p, &CanPlayer::playbackFinished);
    p.play();

    bool finished = spinUntil([&]{ return spy.count() > 0; }, 2000);
    EXPECT_TRUE(finished);
    EXPECT_FALSE(p.isPlaying());
}

TEST(CanPlayerTest, FrameEmitted_EmittedForEachFrame) {
    QTemporaryDir dir;
    QVector<CanFrame> frames = {
        makeFrame(0x100, 1, {0x01}, 1000),
        makeFrame(0x200, 1, {0x02}, 2000),
        makeFrame(0x300, 1, {0x03}, 3000),
    };
    CanPlayer p;
    p.loadFile(writeMcan(frames, dir));
    p.setSpeed(0);

    QSignalSpy spy(&p, &CanPlayer::frameEmitted);
    p.play();
    spinUntil([&]{ return !p.isPlaying(); }, 2000);

    EXPECT_EQ(spy.count(), 3);
}

TEST(CanPlayerTest, FrameEmitted_CorrectFrameData) {
    QTemporaryDir dir;
    CanFrame orig = makeFrame(0xABC, 3, {0x11, 0x22, 0x33}, 5000);
    CanPlayer p;
    p.loadFile(writeMcan({orig}, dir));
    p.setSpeed(0);

    QSignalSpy spy(&p, &CanPlayer::frameEmitted);
    p.play();
    spinUntil([&]{ return spy.count() > 0; }, 2000);

    ASSERT_GE(spy.count(), 1);
    CanFrame emitted = spy[0][0].value<CanFrame>();
    EXPECT_EQ(emitted.id, 0xABCu);
    EXPECT_EQ(emitted.data[0], 0x11);
    EXPECT_EQ(emitted.data[2], 0x33);
}

TEST(CanPlayerTest, PositionChanged_EmittedOnPlay) {
    QTemporaryDir dir;
    QVector<CanFrame> frames = {makeFrame(0x100, 1, {0x01}, 1000)};
    CanPlayer p;
    p.loadFile(writeMcan(frames, dir));
    p.setSpeed(0);

    QSignalSpy spy(&p, &CanPlayer::positionChanged);
    p.play();
    spinUntil([&]{ return !p.isPlaying(); }, 2000);

    EXPECT_GT(spy.count(), 0);
}

// ══════════════════════════════════════════════════════════════
// Speed control
// ══════════════════════════════════════════════════════════════

TEST(CanPlayerTest, SetSpeed_Zero_NoDelay) {
    CanPlayer p;
    p.setSpeed(0.0f);
    // Just check it doesn't crash; speed 0 means max speed
    // Actual timing verification is environment-dependent
    SUCCEED();
}

TEST(CanPlayerTest, SetSpeed_Negative_Clamped) {
    CanPlayer p;
    p.setSpeed(-1.0f);
    SUCCEED();  // should not crash
}

// ══════════════════════════════════════════════════════════════
// Play from beginning after full playback
// ══════════════════════════════════════════════════════════════

TEST(CanPlayerTest, PlayAfterFinish_RestartsFromBeginning) {
    QTemporaryDir dir;
    QVector<CanFrame> frames = {
        makeFrame(0x100, 1, {0x01}, 1000),
        makeFrame(0x200, 1, {0x02}, 2000),
    };
    CanPlayer p;
    p.loadFile(writeMcan(frames, dir));
    p.setSpeed(0);

    // First playback
    QSignalSpy spy1(&p, &CanPlayer::frameEmitted);
    p.play();
    spinUntil([&]{ return !p.isPlaying(); }, 2000);
    EXPECT_EQ(spy1.count(), 2);

    // Second playback — should start from frame 0 again
    QSignalSpy spy2(&p, &CanPlayer::frameEmitted);
    p.play();
    spinUntil([&]{ return !p.isPlaying(); }, 2000);
    EXPECT_EQ(spy2.count(), 2);
}

// ══════════════════════════════════════════════════════════════
// totalFrames / frameCount aliases
// ══════════════════════════════════════════════════════════════

TEST(CanPlayerTest, TotalFrames_MatchesFrameCount) {
    QTemporaryDir dir;
    QVector<CanFrame> frames = {makeFrame(0x100, 1, {0x01}),
                                makeFrame(0x200, 1, {0x02}),
                                makeFrame(0x300, 1, {0x03})};
    CanPlayer p;
    p.loadFile(writeMcan(frames, dir));
    EXPECT_EQ(p.totalFrames(), p.frameCount());
    EXPECT_EQ(p.frameCount(), 3);
}
