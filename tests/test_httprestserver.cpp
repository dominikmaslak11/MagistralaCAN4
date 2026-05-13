#include <gtest/gtest.h>
#include "core/HttpRestServer.h"
#include "core/CanFrameModel.h"
#include "core/CanAlertEngine.h"
#include <QApplication>
#include <QDeadlineTimer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// ── Test environment ──────────────────────────────────────────────────────────

static int    s_argc   = 1;
static char  *s_argbuf = (char*)"test";
static char **s_argv   = &s_argbuf;

class QtNetEnvironment : public ::testing::Environment {
    QApplication *m_app = nullptr;
public:
    void SetUp() override {
        if (!QApplication::instance())
            m_app = new QApplication(s_argc, s_argv);
    }
    void TearDown() override { delete m_app; m_app = nullptr; }
};

[[maybe_unused]] static const bool kReg = []() -> bool {
    ::testing::AddGlobalTestEnvironment(new QtNetEnvironment);
    return true;
}();

// ── HTTP helper ───────────────────────────────────────────────────────────────
// QTcpSocket::waitFor* methods use OS-level select(), not the Qt event loop.
// The server relies on Qt signals (readyRead, newConnection) which need the
// event loop. We spin processEvents() instead of using blocking wait calls.

static QByteArray doRequest(quint16 port, const QByteArray &raw) {
    QTcpSocket sock;
    sock.connectToHost("127.0.0.1", port);

    // Wait for connection (event-loop friendly)
    QDeadlineTimer deadline(2000);
    while (sock.state() != QAbstractSocket::ConnectedState && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (sock.state() == QAbstractSocket::UnconnectedState) break;
    }
    if (sock.state() != QAbstractSocket::ConnectedState) return {};

    // Send request, then yield so the server can dispatch readyRead
    sock.write(raw);
    sock.flush();

    // Spin until the server closes the connection (response fully sent)
    QByteArray resp;
    deadline = QDeadlineTimer(2000);
    while (!deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (sock.bytesAvailable() > 0)
            resp += sock.readAll();
        if (sock.state() == QAbstractSocket::UnconnectedState)
            break;
    }
    resp += sock.readAll();
    return resp;
}

static QByteArray httpGet(quint16 port, const QString &path) {
    return doRequest(port, QString("GET %1 HTTP/1.0\r\nHost: localhost\r\n\r\n")
                               .arg(path).toUtf8());
}

static QByteArray httpPost(quint16 port, const QString &path, const QByteArray &body) {
    QString req = QString("POST %1 HTTP/1.0\r\nHost: localhost\r\nContent-Type: application/json\r\nContent-Length: %2\r\n\r\n")
                      .arg(path).arg(body.size());
    return doRequest(port, req.toUtf8() + body);
}

// Extract JSON body (after first blank line in HTTP response)
static QJsonDocument parseBody(const QByteArray &httpResp) {
    int sep = httpResp.indexOf("\r\n\r\n");
    if (sep < 0) return {};
    return QJsonDocument::fromJson(httpResp.mid(sep + 4));
}

static int httpStatus(const QByteArray &httpResp) {
    // "HTTP/1.1 200 OK\r\n..."
    int sp1 = httpResp.indexOf(' ');
    if (sp1 < 0) return 0;
    int sp2 = httpResp.indexOf(' ', sp1 + 1);
    return httpResp.mid(sp1 + 1, sp2 - sp1 - 1).toInt();
}

// ── Fixtures ──────────────────────────────────────────────────────────────────

class RestApiTest : public ::testing::Test {
protected:
    HttpRestServer server;
    CanFrameModel  model;
    quint16        port = 19080;

    void SetUp() override {
        server.setModel(&model);
        server.fps       = 12.5;
        server.uniqueIds = 3;
        ASSERT_TRUE(server.start(port));
    }
    void TearDown() override { server.stop(); }
};

// ── Tests ─────────────────────────────────────────────────────────────────────

TEST_F(RestApiTest, StatusEndpoint) {
    auto resp = httpGet(port, "/api/status");
    ASSERT_FALSE(resp.isEmpty());
    EXPECT_EQ(httpStatus(resp), 200);
    auto doc = parseBody(resp);
    ASSERT_TRUE(doc.isObject());
    EXPECT_TRUE(doc.object().contains("fps"));
    EXPECT_TRUE(doc.object().contains("uniqueIds"));
}

TEST_F(RestApiTest, StatsEndpoint) {
    auto resp = httpGet(port, "/api/stats");
    ASSERT_EQ(httpStatus(resp), 200);
    auto obj = parseBody(resp).object();
    EXPECT_DOUBLE_EQ(obj["fps"].toDouble(), 12.5);
    EXPECT_EQ(obj["uniqueIds"].toInt(), 3);
    EXPECT_TRUE(obj.contains("totalFrames"));
    EXPECT_TRUE(obj.contains("busLoadPct"));
    EXPECT_TRUE(obj.contains("alertCount"));
}

TEST_F(RestApiTest, FramesEmptyModel) {
    auto resp = httpGet(port, "/api/frames");
    ASSERT_EQ(httpStatus(resp), 200);
    auto doc = parseBody(resp);
    ASSERT_TRUE(doc.isArray());
    EXPECT_EQ(doc.array().size(), 0);
}

TEST_F(RestApiTest, IdsEmptyModel) {
    auto resp = httpGet(port, "/api/ids");
    ASSERT_EQ(httpStatus(resp), 200);
    ASSERT_TRUE(parseBody(resp).isArray());
}

TEST_F(RestApiTest, AlertsEmptyInitially) {
    auto resp = httpGet(port, "/api/alerts");
    ASSERT_EQ(httpStatus(resp), 200);
    auto doc = parseBody(resp);
    ASSERT_TRUE(doc.isArray());
    EXPECT_EQ(doc.array().size(), 0);
}

TEST_F(RestApiTest, NotFoundReturns404) {
    auto resp = httpGet(port, "/api/nonexistent");
    EXPECT_EQ(httpStatus(resp), 404);
}

TEST_F(RestApiTest, SendFrameMissingId) {
    auto resp = httpPost(port, "/api/send", R"({"dlc":4})");
    EXPECT_EQ(httpStatus(resp), 400);
    auto obj = parseBody(resp).object();
    EXPECT_TRUE(obj.contains("error"));
}

TEST_F(RestApiTest, SendFrameValidJson) {
    bool signalFired = false;
    QObject::connect(&server, &HttpRestServer::sendFrameRequested,
                     &server, [&](const CanFrame &f) {
                         signalFired = true;
                         EXPECT_EQ(f.id, 0x1A0u);
                         EXPECT_EQ(f.dlc, 4);
                     });
    auto resp = httpPost(port, "/api/send",
                         R"({"id":"0x1A0","dlc":4,"data":[1,2,3,4]})");
    EXPECT_EQ(httpStatus(resp), 202);
    auto obj = parseBody(resp).object();
    EXPECT_EQ(obj["status"].toString(), "queued");
    EXPECT_TRUE(signalFired);
}

TEST_F(RestApiTest, SendFrameDecimalId) {
    CanFrame captured{};
    QObject::connect(&server, &HttpRestServer::sendFrameRequested,
                     &server, [&](const CanFrame &f) { captured = f; });
    httpPost(port, "/api/send", R"({"id":256,"dlc":2,"data":[170,187]})");
    EXPECT_EQ(captured.id, 256u);
}

TEST_F(RestApiTest, SendFrameEmptyBody) {
    auto resp = httpPost(port, "/api/send", "");
    EXPECT_EQ(httpStatus(resp), 400);
}

TEST_F(RestApiTest, SendFrameInvalidJson) {
    auto resp = httpPost(port, "/api/send", "{bad json}");
    EXPECT_EQ(httpStatus(resp), 400);
}

TEST_F(RestApiTest, CorsOptionsReturns204) {
    auto resp = doRequest(port, "OPTIONS /api/frames HTTP/1.0\r\nHost: localhost\r\n\r\n");
    EXPECT_EQ(httpStatus(resp), 204);
    EXPECT_TRUE(resp.contains("Access-Control-Allow-Origin"));
}

TEST_F(RestApiTest, CorsHeaderOnGet) {
    auto resp = httpGet(port, "/api/stats");
    EXPECT_TRUE(resp.contains("Access-Control-Allow-Origin"));
}

TEST_F(RestApiTest, StartStopSignals) {
    bool started = false, stopped = false;
    QObject::connect(&server, &HttpRestServer::startRequested, &server, [&] { started = true; });
    QObject::connect(&server, &HttpRestServer::stopRequested,  &server, [&] { stopped = true; });
    httpPost(port, "/api/start", "");
    EXPECT_TRUE(started);
    httpPost(port, "/api/stop", "");
    EXPECT_TRUE(stopped);
}

TEST_F(RestApiTest, AlertsAfterEngineEvent) {
    CanAlertEngine engine;
    server.setAlertEngine(&engine);
    CanAlertRule rule;
    rule.name   = "TestNew";
    rule.type   = AlertType::NewCanId;
    rule.action = AlertAction::Log;
    engine.addRule(rule);

    CanFrame f{};
    f.id  = 0xBEEF;
    f.dlc = 3;
    engine.evaluate(f, 42000);
    QCoreApplication::processEvents();

    auto resp = httpGet(port, "/api/alerts");
    ASSERT_EQ(httpStatus(resp), 200);
    auto arr = parseBody(resp).array();
    ASSERT_GE(arr.size(), 1);
    EXPECT_EQ(arr[0].toObject()["ruleName"].toString(), "TestNew");
}

TEST_F(RestApiTest, FramesLimitQueryParam) {
    auto resp = httpGet(port, "/api/frames?limit=10");
    EXPECT_EQ(httpStatus(resp), 200);
    ASSERT_TRUE(parseBody(resp).isArray());
}

TEST_F(RestApiTest, FramesIdFilterQueryParam) {
    auto resp = httpGet(port, "/api/frames?id=0x1A0");
    EXPECT_EQ(httpStatus(resp), 200);
    ASSERT_TRUE(parseBody(resp).isArray());
}
