/**
 * Cross-language integration tests for Remote CAN.
 *
 * These tests prove interoperability between the C++ implementation and a
 * Python WebSocket endpoint (separate OS process). They complement the
 * unit tests in test_remote_can.cpp which test C++↔C++ loopback.
 *
 * Two scenarios:
 *   A) Python WS server  → C++ RemoteCanClient  (validates client parsing)
 *   B) C++ WebSocketServer → Python WS client   (validates server encoding)
 */
#include <gtest/gtest.h>
#include "core/WebSocketServer.h"
#include "core/RemoteCanClient.h"
#include "core/CanFrame.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QProcess>
#include <QDir>
#include <QRegularExpression>

// ── QApplication environment (shared with test_remote_can.cpp) ───────────────

namespace {

bool spinUntil(std::function<bool()> pred, int ms = 4000) {
    QDeadlineTimer dl(ms);
    while (!pred() && !dl.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return pred();
}

// Returns absolute path to the scripts/ directory, relative to the test binary.
QString scriptsDir() {
    // The test binary is in build_linux/; scripts/ is at MagistralaCAN4/scripts/
    QDir d = QDir(QCoreApplication::applicationDirPath());
    // Walk up until we find CMakeLists.txt (project root)
    for (int i = 0; i < 5; ++i) {
        if (QDir(d.absolutePath() + "/scripts").exists())
            return d.absolutePath() + "/scripts";
        d.cdUp();
    }
    return {};
}

// Read all stdout from process into a single string (non-blocking, processEvents).
QString drainStdout(QProcess &proc, int timeoutMs = 3000) {
    QString out;
    QDeadlineTimer dl(timeoutMs);
    while (!dl.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QByteArray chunk = proc.readAllStandardOutput();
        out += QString::fromUtf8(chunk);
        if (proc.state() == QProcess::NotRunning && chunk.isEmpty()) break;
    }
    out += QString::fromUtf8(proc.readAllStandardOutput());
    return out;
}

// Block until process stdout contains the given substring.
bool waitForLine(QProcess &proc, const QString &marker, int timeoutMs = 4000) {
    QString buf;
    QDeadlineTimer dl(timeoutMs);
    while (!dl.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        buf += QString::fromUtf8(proc.readAllStandardOutput());
        if (buf.contains(marker)) return true;
    }
    buf += QString::fromUtf8(proc.readAllStandardOutput());
    return buf.contains(marker);
}

struct ParsedFrame { uint32_t id; int dlc; QByteArray data; };

QList<ParsedFrame> parseClientOutput(const QString &output) {
    QList<ParsedFrame> result;
    static QRegularExpression re(R"(FRAME:([0-9A-F]+):(\d+):([0-9A-F]*))");
    auto it = re.globalMatch(output);
    while (it.hasNext()) {
        auto m = it.next();
        ParsedFrame f;
        f.id  = m.captured(1).toUInt(nullptr, 16);
        f.dlc = m.captured(2).toInt();
        QString hex = m.captured(3);
        for (int i = 0; i + 1 < hex.length(); i += 2)
            f.data.append(static_cast<char>(hex.mid(i, 2).toUInt(nullptr, 16)));
        result.append(f);
    }
    return result;
}

} // namespace

// ── Fixture ───────────────────────────────────────────────────────────────────

class RemoteCanIntegrationTest : public ::testing::Test {
protected:
    QString m_scripts;

    void SetUp() override {
        m_scripts = scriptsDir();
        if (m_scripts.isEmpty())
            GTEST_SKIP() << "Katalog scripts/ nie znaleziony (buduj z poziomu projektu)";
        if (QProcess::execute("python3", {"--version"}) != 0)
            GTEST_SKIP() << "python3 niedostępny";
    }
};

// ═══════════════════════════════════════════════════════════════════════════════
// Scenariusz A: Python server → C++ client
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(RemoteCanIntegrationTest, ScenarioA_CppClientReceivesFromPythonServer) {
    // Start Python WS server fixture
    QProcess pyServer;
    pyServer.setProgram("python3");
    pyServer.setArguments({m_scripts + "/ws_server_fixture.py", "--port", "19400"});
    pyServer.setReadChannel(QProcess::StandardOutput);
    pyServer.start();
    ASSERT_TRUE(pyServer.waitForStarted(3000));

    // Wait for READY signal from Python server
    ASSERT_TRUE(waitForLine(pyServer, "READY", 4000))
        << "Python server did not signal READY";

    // Connect C++ client
    RemoteCanClient client;
    bool connected = false;
    QObject::connect(&client, &RemoteCanClient::statusChanged,
                     [&](bool c, const QString &) { if (c) connected = true; });

    QVector<CanFrame> received;
    QObject::connect(&client, &RemoteCanClient::newFrame,
                     [&](const CanFrame &f) { received.append(f); });

    client.connectToServer("ws://127.0.0.1:19400", {});
    ASSERT_TRUE(spinUntil([&]{ return connected; }, 3000))
        << "C++ client failed to connect to Python server";

    // Wait for all 5 frames (Python server sends them on connect)
    ASSERT_TRUE(spinUntil([&]{ return received.size() >= 5; }, 4000))
        << "C++ client received " << received.size() << "/5 frames from Python server";

    // Verify frame IDs match what ws_server_fixture.py sends
    EXPECT_EQ(received[0].id, 0x100u);
    EXPECT_EQ(received[1].id, 0x200u);
    EXPECT_EQ(received[2].id, 0x300u);
    EXPECT_EQ(received[3].id, 0x7DFu);
    EXPECT_EQ(received[4].id, 0x18DAF110u);

    // Verify DLC
    EXPECT_EQ(received[0].dlc, 1);
    EXPECT_EQ(received[1].dlc, 2);
    EXPECT_EQ(received[2].dlc, 3);
    EXPECT_EQ(received[3].dlc, 4);
    EXPECT_EQ(received[4].dlc, 5);

    // Verify payload bytes (fixture sends range(i, i+dlc))
    EXPECT_EQ(received[0].data[0], 0x00);
    EXPECT_EQ(received[1].data[0], 0x01);
    EXPECT_EQ(received[1].data[1], 0x02);
    EXPECT_EQ(received[2].data[0], 0x02);
    EXPECT_EQ(received[2].data[2], 0x04);

    // Verify extended flag
    EXPECT_FALSE(received[0].extended);
    EXPECT_FALSE(received[3].extended);
    EXPECT_TRUE(received[4].extended);  // 0x18DAF110 > 0x7FF

    client.disconnect();
    pyServer.waitForFinished(2000);
}

TEST_F(RemoteCanIntegrationTest, ScenarioA_LargerBatch_AllFramesDelivered) {
    // Send 10 frames at once and verify all arrive
    QStringList ids;
    for (int i = 0; i < 10; ++i) ids << QString("0x%1").arg(0x100 + i * 0x10, 0, 16);

    QProcess pyServer;
    pyServer.setProgram("python3");
    QStringList serverArgs = {m_scripts + "/ws_server_fixture.py",
                              "--port", "19401", "--ids"};
    serverArgs += ids;
    pyServer.setArguments(serverArgs);
    pyServer.setReadChannel(QProcess::StandardOutput);
    pyServer.start();
    ASSERT_TRUE(pyServer.waitForStarted(3000));
    ASSERT_TRUE(waitForLine(pyServer, "READY", 4000));

    RemoteCanClient client;
    bool connected = false;
    QObject::connect(&client, &RemoteCanClient::statusChanged,
                     [&](bool c, const QString &) { if (c) connected = true; });
    QVector<CanFrame> received;
    QObject::connect(&client, &RemoteCanClient::newFrame,
                     [&](const CanFrame &f) { received.append(f); });

    client.connectToServer("ws://127.0.0.1:19401", {});
    ASSERT_TRUE(spinUntil([&]{ return connected; }, 3000));
    ASSERT_TRUE(spinUntil([&]{ return received.size() >= 10; }, 4000))
        << "Only received " << received.size() << "/10 frames";

    for (int i = 0; i < 10; ++i)
        EXPECT_EQ(received[i].id, uint32_t(0x100 + i * 0x10)) << "frame " << i;

    client.disconnect();
    pyServer.waitForFinished(2000);
}

TEST_F(RemoteCanIntegrationTest, ScenarioA_DataBytesCorrectlyDecoded) {
    // Verify hex data decoding: Python sends known pattern, C++ parses it
    QProcess pyServer;
    pyServer.setProgram("python3");
    pyServer.setArguments({m_scripts + "/ws_server_fixture.py",
                           "--port", "19402", "--ids", "0x1A2"});
    pyServer.setReadChannel(QProcess::StandardOutput);
    pyServer.start();
    ASSERT_TRUE(pyServer.waitForStarted(3000));
    ASSERT_TRUE(waitForLine(pyServer, "READY", 4000));

    RemoteCanClient client;
    bool connected = false;
    CanFrame received;
    bool got = false;
    QObject::connect(&client, &RemoteCanClient::statusChanged,
                     [&](bool c, const QString &) { if (c) connected = true; });
    QObject::connect(&client, &RemoteCanClient::newFrame,
                     [&](const CanFrame &f) { received = f; got = true; });

    client.connectToServer("ws://127.0.0.1:19402", {});
    ASSERT_TRUE(spinUntil([&]{ return connected; }, 3000));
    ASSERT_TRUE(spinUntil([&]{ return got; }, 4000));

    // fixture: id=0x1A2 (i=0), dlc=1, data=bytes(range(0,1))=[0x00]
    EXPECT_EQ(received.id, 0x1A2u);
    EXPECT_EQ(received.dlc, 1);
    EXPECT_EQ(received.data[0], 0x00);

    client.disconnect();
    pyServer.waitForFinished(2000);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Scenariusz B: C++ server → Python client
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(RemoteCanIntegrationTest, ScenarioB_PythonClientReceivesFromCppServer) {
    // Start C++ WebSocket server
    WebSocketServer server;
    server.start(19410);
    ASSERT_TRUE(server.isRunning());

    // Start Python client fixture
    QProcess pyClient;
    pyClient.setProgram("python3");
    pyClient.setArguments({m_scripts + "/ws_client_fixture.py",
                           "--url", "ws://127.0.0.1:19410", "--expect", "3"});
    pyClient.setReadChannel(QProcess::StandardOutput);
    pyClient.start();
    ASSERT_TRUE(pyClient.waitForStarted(3000));

    // Wait for Python client to connect
    ASSERT_TRUE(waitForLine(pyClient, "CONNECTED", 4000))
        << "Python client failed to connect to C++ server";

    // Let the event loop run so server registers the client
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    EXPECT_GE(server.clientCount(), 1);

    // Broadcast 3 frames from C++ server
    auto makeFrame = [](uint32_t id, std::initializer_list<uint8_t> bytes) {
        CanFrame f; f.id = id; f.dlc = uint8_t(bytes.size());
        int i = 0; for (auto b : bytes) f.data[i++] = b;
        return f;
    };
    server.broadcastFrameBatch({
        makeFrame(0x7DF, {0x02, 0x01, 0x0C}),
        makeFrame(0x300, {0xDE, 0xAD}),
        makeFrame(0x100, {0xBE, 0xEF, 0xCA, 0xFE}),
    });

    // spinUntil keeps processEvents() running so Qt can flush socket buffers
    // to the Python client (waitForFinished would block the event loop).
    ASSERT_TRUE(spinUntil([&]{ return pyClient.state() == QProcess::NotRunning; }, 6000))
        << "Python client did not finish in time\nstderr: "
        << pyClient.readAllStandardError().toStdString();
    EXPECT_EQ(pyClient.exitCode(), 0);

    QString output = drainStdout(pyClient, 200);
    auto frames = parseClientOutput(output);

    // Verify
    EXPECT_TRUE(output.contains("RECEIVED:3"))
        << "Python output was:\n" << output.toStdString();

    if (frames.size() >= 3) {
        EXPECT_EQ(frames[0].id, 0x7DFu);
        EXPECT_EQ(frames[0].dlc, 3);
        EXPECT_EQ(static_cast<uint8_t>(frames[0].data[0]), 0x02);
        EXPECT_EQ(static_cast<uint8_t>(frames[0].data[1]), 0x01);
        EXPECT_EQ(static_cast<uint8_t>(frames[0].data[2]), 0x0C);

        EXPECT_EQ(frames[1].id, 0x300u);
        EXPECT_EQ(frames[1].dlc, 2);
        EXPECT_EQ(static_cast<uint8_t>(frames[1].data[0]), 0xDE);
        EXPECT_EQ(static_cast<uint8_t>(frames[1].data[1]), 0xAD);

        EXPECT_EQ(frames[2].id, 0x100u);
        EXPECT_EQ(frames[2].dlc, 4);
        EXPECT_EQ(static_cast<uint8_t>(frames[2].data[0]), 0xBE);
        EXPECT_EQ(static_cast<uint8_t>(frames[2].data[3]), 0xFE);
    }

    server.stop();
}

TEST_F(RemoteCanIntegrationTest, ScenarioB_MultipleClientsReceiveSameBatch) {
    WebSocketServer server;
    server.start(19411);
    ASSERT_TRUE(server.isRunning());

    // Two Python clients connect simultaneously
    QProcess client1, client2;
    for (auto *c : {&client1, &client2}) {
        c->setProgram("python3");
        c->setArguments({m_scripts + "/ws_client_fixture.py",
                         "--url", "ws://127.0.0.1:19411", "--expect", "2"});
        c->setReadChannel(QProcess::StandardOutput);
        c->start();
        ASSERT_TRUE(c->waitForStarted(3000));
    }

    ASSERT_TRUE(waitForLine(client1, "CONNECTED", 4000));
    ASSERT_TRUE(waitForLine(client2, "CONNECTED", 4000));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 200);
    EXPECT_GE(server.clientCount(), 2);

    auto makeFrame = [](uint32_t id, uint8_t b) {
        CanFrame f; f.id = id; f.dlc = 1; f.data[0] = b; return f;
    };
    server.broadcastFrameBatch({ makeFrame(0xAAA, 0x11), makeFrame(0xBBB, 0x22) });

    ASSERT_TRUE(spinUntil([&]{ return client1.state() == QProcess::NotRunning &&
                                      client2.state() == QProcess::NotRunning; }, 6000));

    QString out1 = drainStdout(client1, 100);
    QString out2 = drainStdout(client2, 100);

    EXPECT_TRUE(out1.contains("RECEIVED:2")) << "Client 1: " << out1.toStdString();
    EXPECT_TRUE(out2.contains("RECEIVED:2")) << "Client 2: " << out2.toStdString();

    server.stop();
}

TEST_F(RemoteCanIntegrationTest, ScenarioB_EmptyBatchDoesNotCrashPythonClient) {
    WebSocketServer server;
    server.start(19412);
    ASSERT_TRUE(server.isRunning());

    // Broadcast an empty batch — Python client should not crash
    server.broadcastFrameBatch({});  // server should skip (isEmpty guard)

    // Now broadcast a real frame
    QProcess pyClient;
    pyClient.setProgram("python3");
    pyClient.setArguments({m_scripts + "/ws_client_fixture.py",
                           "--url", "ws://127.0.0.1:19412", "--expect", "1"});
    pyClient.setReadChannel(QProcess::StandardOutput);
    pyClient.start();
    ASSERT_TRUE(pyClient.waitForStarted(3000));
    ASSERT_TRUE(waitForLine(pyClient, "CONNECTED", 4000));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 100);

    CanFrame f; f.id = 0x123; f.dlc = 1; f.data[0] = 0xAB;
    server.broadcastFrameBatch({ f });

    ASSERT_TRUE(spinUntil([&]{ return pyClient.state() == QProcess::NotRunning; }, 6000));
    EXPECT_EQ(pyClient.exitCode(), 0);
    server.stop();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Scenariusz C: protocol conformance (Python↔Python validates JSON format)
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
// Scenariusz D: Python client → C++ server (send_frame + frame_ack)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(RemoteCanIntegrationTest, ScenarioD_PythonClientSendsFrame_CppServerEmitsSignal) {
    WebSocketServer server;
    server.start(19430);
    ASSERT_TRUE(server.isRunning());

    CanFrame rxFrame; bool got = false;
    QObject::connect(&server, &WebSocketServer::frameReceivedFromClient,
                     [&](const CanFrame &f) { rxFrame = f; got = true; });

    QProcess pyClient;
    pyClient.setProgram("python3");
    pyClient.setArguments({m_scripts + "/ws_client_fixture.py",
                           "--url",     "ws://127.0.0.1:19430",
                           "--expect",  "0",
                           "--send-id", "0x7DF",
                           "--send-data", "02 01 0C",
                           "--wait-ack"});
    pyClient.setReadChannel(QProcess::StandardOutput);
    pyClient.start();
    ASSERT_TRUE(pyClient.waitForStarted(3000));

    // Wait for C++ server to receive the frame
    ASSERT_TRUE(spinUntil([&]{ return got; }, 5000))
        << "Server never emitted frameReceivedFromClient";

    EXPECT_EQ(rxFrame.id,  0x7DFu);
    EXPECT_EQ(rxFrame.dlc, 3);
    EXPECT_EQ(rxFrame.data[0], 0x02);
    EXPECT_EQ(rxFrame.data[1], 0x01);
    EXPECT_EQ(rxFrame.data[2], 0x0C);

    // Wait for Python process to receive ack and exit
    ASSERT_TRUE(spinUntil([&]{ return pyClient.state() == QProcess::NotRunning; }, 4000));
    EXPECT_EQ(pyClient.exitCode(), 0);

    QString out = drainStdout(pyClient, 100);
    EXPECT_TRUE(out.contains("SENT:0x7DF")) << out.toStdString();
    EXPECT_TRUE(out.contains("ACK:0x7DF:ok")) << out.toStdString();

    server.stop();
}

TEST_F(RemoteCanIntegrationTest, ScenarioD_AckContainsCorrectId) {
    WebSocketServer server;
    server.start(19431);
    ASSERT_TRUE(server.isRunning());

    QProcess pyClient;
    pyClient.setProgram("python3");
    pyClient.setArguments({m_scripts + "/ws_client_fixture.py",
                           "--url",     "ws://127.0.0.1:19431",
                           "--expect",  "0",
                           "--send-id", "0x1A2B3C4D",
                           "--send-data", "DE AD BE EF",
                           "--wait-ack"});
    pyClient.setReadChannel(QProcess::StandardOutput);
    pyClient.start();
    ASSERT_TRUE(pyClient.waitForStarted(3000));
    ASSERT_TRUE(spinUntil([&]{ return pyClient.state() == QProcess::NotRunning; }, 6000));

    QString out = drainStdout(pyClient, 100);
    EXPECT_TRUE(out.contains("ACK:0x1A2B3C4D:ok")) << out.toStdString();
    server.stop();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Scenariusz C: protocol conformance (Python↔Python validates JSON format)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_F(RemoteCanIntegrationTest, ScenarioC_PythonServerPythonClientProtocol) {
    // This test validates that the JSON format is self-consistent in Python
    // (fixture server → fixture client). Acts as a baseline for Scenarios A & B.
    QProcess pyServer;
    pyServer.setProgram("python3");
    pyServer.setArguments({m_scripts + "/ws_server_fixture.py", "--port", "19420"});
    pyServer.setReadChannel(QProcess::StandardOutput);
    pyServer.start();
    ASSERT_TRUE(pyServer.waitForStarted(3000));
    ASSERT_TRUE(waitForLine(pyServer, "READY", 4000));

    QProcess pyClient;
    pyClient.setProgram("python3");
    pyClient.setArguments({m_scripts + "/ws_client_fixture.py",
                           "--url", "ws://127.0.0.1:19420", "--expect", "5"});
    pyClient.setReadChannel(QProcess::StandardOutput);
    pyClient.start();
    ASSERT_TRUE(pyClient.waitForStarted(3000));
    ASSERT_TRUE(pyClient.waitForFinished(8000));
    EXPECT_EQ(pyClient.exitCode(), 0);

    QString out = pyClient.readAllStandardOutput();
    EXPECT_TRUE(out.contains("RECEIVED:5")) << out.toStdString();

    auto frames = parseClientOutput(out);
    ASSERT_EQ(frames.size(), 5);
    EXPECT_EQ(frames[0].id, 0x100u);
    EXPECT_EQ(frames[4].id, 0x18DAF110u);

    pyServer.waitForFinished(2000);
}
