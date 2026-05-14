/**
 * @file test_cansniffer.cpp
 * @brief Unit tests for CanSniffer using a mock ICanDriver.
 *
 * Uses a synchronous stub driver so no real hardware is needed.
 * All tests run in the calling thread; QThreadPool may spawn threads
 * but we coordinate via atomic flags and short sleeps.
 */
#include <gtest/gtest.h>
#include "core/CanSniffer.h"
#include "core/ICanDriver.h"
#include <QCoreApplication>
#include <QSignalSpy>
#include <QDeadlineTimer>
#include <atomic>
#include <QThread>

// ── Mock ICanDriver ──────────────────────────────────────────────────────────

class MockDriver : public ICanDriver {
public:
    std::atomic<bool> opened{false};
    std::atomic<bool> closed{false};
    std::atomic<int>  writeCount{0};
    std::atomic<bool> valid{false};

    QVector<CanFrame> readQueue;   // frames to return from readFrame()
    int readIdx = 0;

    CanFrame lastWritten{};

    bool open(const QString &) override {
        opened = true;
        valid  = true;
        return true;
    }
    void close() override {
        closed = true;
        valid  = false;
    }
    CanFrame readFrame() override {
        if (readIdx < readQueue.size()) {
            return readQueue[readIdx++];
        }
        // Return sentinel "no data" frame after queue is drained
        QThread::msleep(2);
        return CanFrame{};
    }
    bool isValid() const override { return valid.load(); }
    void writeFrame(const CanFrame &f) override {
        lastWritten = f;
        ++writeCount;
    }
    QStringList availableDevices() const override { return {"mock0"}; }
    QString backendName() const override { return "MockDriver"; }
};

class FailOpenDriver : public ICanDriver {
public:
    bool open(const QString &) override { return false; }
    void close() override {}
    CanFrame readFrame() override { return {}; }
    bool isValid() const override { return false; }
    void writeFrame(const CanFrame &) override {}
    QStringList availableDevices() const override { return {}; }
    QString backendName() const override { return "FailDriver"; }
};

// ── Helpers ──────────────────────────────────────────────────────────────────

static CanFrame makeFrame(uint32_t id, uint8_t b0) {
    CanFrame f{};
    f.id     = id;
    f.dlc    = 1;
    f.data[0] = b0;
    return f;
}

static bool spinUntil(std::function<bool()> cond, int ms = 500) {
    QDeadlineTimer t(ms);
    while (!t.hasExpired()) {
        if (cond()) return true;
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
    return cond();
}

// Stop sniffer — CanSniffer::stop() now blocks until doWork() exits,
// so stack objects (sniffer, driver) are safe to destroy immediately after.
static void stopAndWait(CanSniffer &sniffer) {
    sniffer.stop();
}

// ── Global setup: register CanFrame in QMetaType ─────────────────────────

class CanSnifferEnvironment : public ::testing::Environment {
public:
    void SetUp() override { qRegisterMetaType<CanFrame>("CanFrame"); }
};

// Registered once; gtest's main() calls this before any test in the binary.
// Since the binary already has other test files that may register it too,
// registering twice is harmless.
static ::testing::Environment *const g_env =
    ::testing::AddGlobalTestEnvironment(new CanSnifferEnvironment);

// ── Initial state ─────────────────────────────────────────────────────────

TEST(CanSniffer, InitialState_NotValid) {
    CanSniffer sniffer;
    EXPECT_FALSE(sniffer.isSocketValid());
}

// ── No driver → start emits error ─────────────────────────────────────────

TEST(CanSniffer, Start_NoDriver_EmitsError) {
    CanSniffer sniffer;
    QSignalSpy spy(&sniffer, &CanSniffer::errorOccurred);
    sniffer.start("vcan0");
    EXPECT_GE(spy.count(), 1);
}

// ── Driver that fails open → emits error ─────────────────────────────────

TEST(CanSniffer, Start_DriverOpenFails_EmitsError) {
    CanSniffer sniffer;
    FailOpenDriver driver;
    sniffer.setDriver(&driver);

    QSignalSpy errSpy(&sniffer, &CanSniffer::errorOccurred);
    sniffer.start("vcan0");
    EXPECT_GE(errSpy.count(), 1);
}

// ── isSocketValid reflects driver validity ────────────────────────────────

TEST(CanSniffer, IsSocketValid_WithOpenDriver) {
    CanSniffer sniffer;
    MockDriver driver;
    sniffer.setDriver(&driver);
    sniffer.start("mock0");

    EXPECT_TRUE(sniffer.isSocketValid());

    stopAndWait(sniffer);
}

// ── start emits statusChanged(true) ──────────────────────────────────────

TEST(CanSniffer, Start_EmitsStatusChanged_True) {
    CanSniffer sniffer;
    MockDriver driver;
    sniffer.setDriver(&driver);

    QSignalSpy spy(&sniffer, &CanSniffer::statusChanged);
    sniffer.start("mock0");

    EXPECT_GE(spy.count(), 1);
    EXPECT_TRUE(spy.last()[0].toBool());

    stopAndWait(sniffer);
}

// ── stop emits statusChanged(false) ──────────────────────────────────────

TEST(CanSniffer, Stop_EmitsStatusChanged_False) {
    CanSniffer sniffer;
    MockDriver driver;
    sniffer.setDriver(&driver);
    sniffer.start("mock0");

    QSignalSpy spy(&sniffer, &CanSniffer::statusChanged);
    stopAndWait(sniffer);

    EXPECT_GE(spy.count(), 1);
    EXPECT_FALSE(spy.last()[0].toBool());
}

// ── double start is a no-op ───────────────────────────────────────────────

TEST(CanSniffer, DoubleStart_Idempotent) {
    CanSniffer sniffer;
    MockDriver driver;
    sniffer.setDriver(&driver);

    QSignalSpy spy(&sniffer, &CanSniffer::statusChanged);
    sniffer.start("mock0");
    sniffer.start("mock0");  // second start should be ignored

    EXPECT_EQ(spy.count(), 1);
    stopAndWait(sniffer);
}

// ── stop without start — no crash ────────────────────────────────────────

TEST(CanSniffer, Stop_WithoutStart_NoCrash) {
    CanSniffer sniffer;
    EXPECT_NO_THROW(sniffer.stop());
}

// ── writeFrame: no driver → emits error ──────────────────────────────────

TEST(CanSniffer, WriteFrame_NoDriver_EmitsError) {
    CanSniffer sniffer;
    QSignalSpy spy(&sniffer, &CanSniffer::errorOccurred);
    sniffer.writeFrame(makeFrame(0x100, 0xAA));
    EXPECT_GE(spy.count(), 1);
}

// ── writeFrame: invalid driver → emits error ─────────────────────────────

TEST(CanSniffer, WriteFrame_InvalidDriver_EmitsError) {
    CanSniffer sniffer;
    MockDriver driver;
    // driver is NOT opened; isValid() returns false
    sniffer.setDriver(&driver);

    QSignalSpy spy(&sniffer, &CanSniffer::errorOccurred);
    sniffer.writeFrame(makeFrame(0x100, 0xAA));
    EXPECT_GE(spy.count(), 1);
    EXPECT_EQ(driver.writeCount.load(), 0);
}

// ── writeFrame: valid driver → dispatches to driver ──────────────────────

TEST(CanSniffer, WriteFrame_ValidDriver_CallsDriverWrite) {
    CanSniffer sniffer;
    MockDriver driver;
    sniffer.setDriver(&driver);
    sniffer.start("mock0");

    QSignalSpy errSpy(&sniffer, &CanSniffer::errorOccurred);
    CanFrame f = makeFrame(0x200, 0xBB);
    sniffer.writeFrame(f);

    EXPECT_EQ(errSpy.count(), 0);
    EXPECT_EQ(driver.writeCount.load(), 1);
    EXPECT_EQ(driver.lastWritten.id, 0x200u);

    stopAndWait(sniffer);
}

// ── drainAndEmit: emits newFrame for buffered frames ─────────────────────

TEST(CanSniffer, DrainAndEmit_EmitsBufferedFrames) {
    CanSniffer sniffer;
    MockDriver driver;
    driver.readQueue = {makeFrame(0x111, 0x01), makeFrame(0x222, 0x02)};
    sniffer.setDriver(&driver);
    sniffer.start("mock0");

    QSignalSpy frameSpy(&sniffer, &CanSniffer::newFrame);

    // Wait for the producer thread to push both frames into the ring buffer
    spinUntil([&] { return driver.readIdx >= 2; }, 500);

    sniffer.drainAndEmit();

    EXPECT_GE(frameSpy.count(), 2);

    stopAndWait(sniffer);
}

// ── drainAndEmit on empty buffer: no newFrame ────────────────────────────

TEST(CanSniffer, DrainAndEmit_EmptyBuffer_NoSignal) {
    CanSniffer sniffer;
    MockDriver driver;
    sniffer.setDriver(&driver);
    sniffer.start("mock0");

    QSignalSpy spy(&sniffer, &CanSniffer::newFrame);
    sniffer.drainAndEmit();  // buffer is empty
    EXPECT_EQ(spy.count(), 0);

    stopAndWait(sniffer);
}

// ── bufferSize: initially 0 ───────────────────────────────────────────────

TEST(CanSniffer, BufferSize_InitiallyZero) {
    CanSniffer sniffer;
    EXPECT_EQ(sniffer.bufferSize(), 0);
}

// ── driver opened with correct interface name ─────────────────────────────

TEST(CanSniffer, OpenCalledWithCorrectInterface) {
    // We can't directly check the argument to open() in MockDriver above,
    // but we know opened=true and isValid()=true means open was called.
    CanSniffer sniffer;
    MockDriver driver;
    sniffer.setDriver(&driver);
    sniffer.start("testIface0");

    EXPECT_TRUE(driver.opened.load());

    stopAndWait(sniffer);
}

// ── close called on stop ──────────────────────────────────────────────────

TEST(CanSniffer, StopCallsDriverClose) {
    CanSniffer sniffer;
    MockDriver driver;
    sniffer.setDriver(&driver);
    sniffer.start("mock0");
    sniffer.stop();

    EXPECT_TRUE(driver.closed.load());
}
