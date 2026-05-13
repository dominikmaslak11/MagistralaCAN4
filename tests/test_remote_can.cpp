#include <gtest/gtest.h>
#include "core/WebSocketServer.h"
#include "core/RemoteCanClient.h"
#include "core/CanFrame.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QSignalSpy>

// ── QApplication environment ──────────────────────────────────────────────────

static int    s_argc   = 1;
static char  *s_argbuf = (char*)"test_remote_can";
static char **s_argv   = &s_argbuf;

class RemoteCanEnvironment : public ::testing::Environment {
    QApplication *m_app = nullptr;
public:
    void SetUp() override {
        if (!QApplication::instance())
            m_app = new QApplication(s_argc, s_argv);
    }
    void TearDown() override { delete m_app; m_app = nullptr; }
};

[[maybe_unused]] static const bool kEnvReg = []() -> bool {
    ::testing::AddGlobalTestEnvironment(new RemoteCanEnvironment);
    return true;
}();

// ── Helpers ───────────────────────────────────────────────────────────────────

// Spin the Qt event loop until predicate() returns true or deadline expires.
static bool spinUntil(std::function<bool()> pred, int timeoutMs = 2000) {
    QDeadlineTimer dl(timeoutMs);
    while (!pred() && !dl.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return pred();
}

// Build a CanFrame for use in tests.
static CanFrame makeFrame(uint32_t id, std::initializer_list<uint8_t> bytes) {
    CanFrame f;
    f.id  = id;
    f.dlc = static_cast<uint8_t>(bytes.size());
    int i = 0;
    for (uint8_t b : bytes) f.data[i++] = b;
    f.timestamp = 123456789ULL;
    return f;
}

// ── Fixture ───────────────────────────────────────────────────────────────────

class RemoteCanTest : public ::testing::Test {
protected:
    WebSocketServer server;
    RemoteCanClient client;

    void TearDown() override {
        client.disconnect();
        server.stop();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
};

// ── Tests — WS plaintext mode ─────────────────────────────────────────────────

TEST_F(RemoteCanTest, ServerStartsOnFreePort) {
    server.start(19100);
    EXPECT_TRUE(server.isRunning());
    EXPECT_FALSE(server.isSecure());
}

TEST_F(RemoteCanTest, ClientConnectsWs) {
    server.start(19101);
    ASSERT_TRUE(server.isRunning());

    bool connected = false;
    QObject::connect(&client, &RemoteCanClient::statusChanged,
                     [&](bool c, const QString &) { if (c) connected = true; });

    client.connectToServer("ws://127.0.0.1:19101", {});

    EXPECT_TRUE(spinUntil([&]{ return connected; }, 2000));
    EXPECT_TRUE(client.isConnected());
}

TEST_F(RemoteCanTest, FrameBatchReachedClient) {
    server.start(19102);
    ASSERT_TRUE(server.isRunning());

    bool connected = false;
    QObject::connect(&client, &RemoteCanClient::statusChanged,
                     [&](bool c, const QString &) { if (c) connected = true; });
    client.connectToServer("ws://127.0.0.1:19102", {});
    ASSERT_TRUE(spinUntil([&]{ return connected; }, 2000));

    // Collect frames received by the client
    QVector<CanFrame> received;
    QObject::connect(&client, &RemoteCanClient::newFrame,
                     [&](const CanFrame &f) { received.append(f); });

    // Server broadcasts a batch of 3 frames
    QVector<CanFrame> batch = {
        makeFrame(0x123, {0x01, 0x02, 0x03}),
        makeFrame(0x456, {0xAA, 0xBB}),
        makeFrame(0x001, {0xFF}),
    };
    server.broadcastFrameBatch(batch);

    EXPECT_TRUE(spinUntil([&]{ return received.size() >= 3; }, 2000));
    ASSERT_EQ(received.size(), 3);

    EXPECT_EQ(received[0].id,  0x123u);
    EXPECT_EQ(received[0].dlc, 3);
    EXPECT_EQ(received[0].data[0], 0x01);
    EXPECT_EQ(received[0].data[1], 0x02);
    EXPECT_EQ(received[0].data[2], 0x03);

    EXPECT_EQ(received[1].id,  0x456u);
    EXPECT_EQ(received[1].dlc, 2);
    EXPECT_EQ(received[1].data[0], 0xAA);
    EXPECT_EQ(received[1].data[1], 0xBB);

    EXPECT_EQ(received[2].id,  0x001u);
    EXPECT_EQ(received[2].dlc, 1);
    EXPECT_EQ(received[2].data[0], 0xFF);
}

TEST_F(RemoteCanTest, FrameCounterIncrements) {
    server.start(19103);
    bool connected = false;
    QObject::connect(&client, &RemoteCanClient::statusChanged,
                     [&](bool c, const QString &) { if (c) connected = true; });
    client.connectToServer("ws://127.0.0.1:19103", {});
    ASSERT_TRUE(spinUntil([&]{ return connected; }, 2000));

    uint64_t prev = client.frameCount();
    server.broadcastFrameBatch({ makeFrame(0x10, {0x00}), makeFrame(0x11, {0x00}) });
    EXPECT_TRUE(spinUntil([&]{ return client.frameCount() >= prev + 2; }, 2000));
    EXPECT_GE(client.frameCount(), prev + 2);
}

TEST_F(RemoteCanTest, MultipleBatchesDelivered) {
    server.start(19104);
    bool connected = false;
    QObject::connect(&client, &RemoteCanClient::statusChanged,
                     [&](bool c, const QString &) { if (c) connected = true; });
    client.connectToServer("ws://127.0.0.1:19104", {});
    ASSERT_TRUE(spinUntil([&]{ return connected; }, 2000));

    int total = 0;
    QObject::connect(&client, &RemoteCanClient::newFrame,
                     [&](const CanFrame &) { ++total; });

    for (int i = 0; i < 5; ++i)
        server.broadcastFrameBatch({ makeFrame(uint32_t(i), {uint8_t(i)}) });

    EXPECT_TRUE(spinUntil([&]{ return total >= 5; }, 3000));
    EXPECT_EQ(total, 5);
}

TEST_F(RemoteCanTest, ExtendedAndFdFlagsPreserved) {
    server.start(19105);
    bool connected = false;
    QObject::connect(&client, &RemoteCanClient::statusChanged,
                     [&](bool c, const QString &) { if (c) connected = true; });
    client.connectToServer("ws://127.0.0.1:19105", {});
    ASSERT_TRUE(spinUntil([&]{ return connected; }, 2000));

    CanFrame src = makeFrame(0x18DAF110, {0x01, 0x02});
    src.extended = true;
    src.fd = true;

    CanFrame received;
    bool got = false;
    QObject::connect(&client, &RemoteCanClient::newFrame,
                     [&](const CanFrame &f) { received = f; got = true; });
    server.broadcastFrameBatch({ src });

    ASSERT_TRUE(spinUntil([&]{ return got; }, 2000));
    EXPECT_EQ(received.id, 0x18DAF110u);
    EXPECT_TRUE(received.extended);
    EXPECT_TRUE(received.fd);
}

TEST_F(RemoteCanTest, DisconnectStopsFrameDelivery) {
    server.start(19106);
    bool connected = false;
    QObject::connect(&client, &RemoteCanClient::statusChanged,
                     [&](bool c, const QString &) { if (c) connected = true; });
    client.connectToServer("ws://127.0.0.1:19106", {});
    ASSERT_TRUE(spinUntil([&]{ return connected; }, 2000));

    client.disconnect();
    spinUntil([&]{ return !client.isConnected(); }, 1000);

    int framesAfterDisconnect = 0;
    QObject::connect(&client, &RemoteCanClient::newFrame,
                     [&](const CanFrame &) { ++framesAfterDisconnect; });
    server.broadcastFrameBatch({ makeFrame(0x99, {0x00}) });
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);

    EXPECT_EQ(framesAfterDisconnect, 0);
}

// ── Tests — WSS token auth ────────────────────────────────────────────────────

TEST_F(RemoteCanTest, WssAuthCorrectTokenAccepted) {
    if (!server.ensureCertificate()) GTEST_SKIP() << "openssl niedostępny";

    server.startSecure(19110);
    ASSERT_TRUE(server.isRunning());
    ASSERT_TRUE(server.isSecure());

    bool connected = false;
    QObject::connect(&client, &RemoteCanClient::statusChanged,
                     [&](bool c, const QString &) { if (c) connected = true; });
    client.connectToServer("wss://127.0.0.1:19110", server.token());

    EXPECT_TRUE(spinUntil([&]{ return connected; }, 4000));
    EXPECT_TRUE(client.isConnected());
}

TEST_F(RemoteCanTest, WssWrongTokenRejected) {
    if (!server.ensureCertificate()) GTEST_SKIP() << "openssl niedostępny";

    server.startSecure(19111);
    ASSERT_TRUE(server.isRunning());

    bool errored = false;
    QObject::connect(&client, &RemoteCanClient::errorOccurred,
                     [&](const QString &) { errored = true; });

    client.connectToServer("wss://127.0.0.1:19111", "000000000000000000000000000000000000000000000000000000000000dead");
    EXPECT_TRUE(spinUntil([&]{ return errored; }, 4000));
    EXPECT_FALSE(client.isConnected());
}

TEST_F(RemoteCanTest, WssFramesBatchDeliveredAfterAuth) {
    if (!server.ensureCertificate()) GTEST_SKIP() << "openssl niedostępny";

    server.startSecure(19112);
    ASSERT_TRUE(server.isRunning());

    bool connected = false;
    QObject::connect(&client, &RemoteCanClient::statusChanged,
                     [&](bool c, const QString &) { if (c) connected = true; });
    client.connectToServer("wss://127.0.0.1:19112", server.token());
    ASSERT_TRUE(spinUntil([&]{ return connected; }, 4000));

    QVector<CanFrame> received;
    QObject::connect(&client, &RemoteCanClient::newFrame,
                     [&](const CanFrame &f) { received.append(f); });

    server.broadcastFrameBatch({ makeFrame(0x7DF, {0x02, 0x01, 0x0C}) });
    EXPECT_TRUE(spinUntil([&]{ return !received.isEmpty(); }, 2000));
    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0].id, 0x7DFu);
    EXPECT_EQ(received[0].dlc, 3);
    EXPECT_EQ(received[0].data[0], 0x02);
}

// ── Tests — server state ──────────────────────────────────────────────────────

TEST_F(RemoteCanTest, ServerStopDisconnectsClients) {
    server.start(19120);
    bool connected = false, disconnected = false;
    QObject::connect(&client, &RemoteCanClient::statusChanged,
                     [&](bool c, const QString &) { if (c) connected = true; else disconnected = true; });
    client.connectToServer("ws://127.0.0.1:19120", {});
    ASSERT_TRUE(spinUntil([&]{ return connected; }, 2000));

    server.stop();
    EXPECT_FALSE(server.isRunning());
    EXPECT_TRUE(spinUntil([&]{ return disconnected; }, 2000));
}

TEST_F(RemoteCanTest, BroadcastToNoClientsIsSilent) {
    server.start(19121);
    // Should not crash when no clients are connected
    server.broadcastFrameBatch({ makeFrame(0x100, {0x00}) });
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    SUCCEED();
}

TEST_F(RemoteCanTest, TokenIsConsistentAcrossRestarts) {
    server.start(19122);
    QString tok1 = server.token();
    server.stop();
    server.start(19122);
    QString tok2 = server.token();
    EXPECT_EQ(tok1, tok2);
    EXPECT_EQ(tok1.length(), 64);
}
