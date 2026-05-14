#include <gtest/gtest.h>
#include "core/CanNodeSimulator.h"
#include "core/CanFrame.h"
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryFile>

static void ensureApp() {
    if (!QCoreApplication::instance()) {
        static int argc = 1;
        static char *argv[] = { (char*)"test", nullptr };
        new QCoreApplication(argc, argv);
    }
}

static CanFrame makeFrame(uint32_t id, std::initializer_list<uint8_t> bytes = {}) {
    CanFrame f{};
    f.id  = id;
    f.dlc = static_cast<uint8_t>(bytes.size());
    int i = 0;
    for (uint8_t b : bytes) f.data[i++] = b;
    return f;
}

class NodeSimTest : public ::testing::Test {
protected:
    void SetUp() override { ensureApp(); }
};

// ── CanNodeDefinition JSON roundtrip ─────────────────────────

TEST_F(NodeSimTest, JsonRoundtrip_BasicNode) {
    CanNodeDefinition n;
    n.name            = "TestNode";
    n.triggerId       = 0x7DF;
    n.triggerIdMask   = 0x7FF;
    n.responseId      = 0x7E8;
    n.staticResponse  = {0x04, 0x41, 0x0C, 0x0F, 0xA0};
    n.responseDelayMs = 10;
    n.enabled         = true;

    QJsonObject obj = n.toJson();
    CanNodeDefinition n2 = CanNodeDefinition::fromJson(obj);

    EXPECT_EQ(n2.name,            "TestNode");
    EXPECT_EQ(n2.triggerId,       0x7DFu);
    EXPECT_EQ(n2.triggerIdMask,   0x7FFu);
    EXPECT_EQ(n2.responseId,      0x7E8u);
    EXPECT_EQ(n2.staticResponse,  (QVector<uint8_t>{0x04, 0x41, 0x0C, 0x0F, 0xA0}));
    EXPECT_EQ(n2.responseDelayMs, 10);
    EXPECT_TRUE(n2.enabled);
}

TEST_F(NodeSimTest, JsonRoundtrip_DisabledNode) {
    CanNodeDefinition n;
    n.name    = "Disabled";
    n.enabled = false;
    auto n2 = CanNodeDefinition::fromJson(n.toJson());
    EXPECT_FALSE(n2.enabled);
}

TEST_F(NodeSimTest, JsonRoundtrip_TriggerData) {
    CanNodeDefinition n;
    n.triggerData     = {0x02, 0x01};
    n.triggerDataMask = {0xFF, 0xFF};
    auto n2 = CanNodeDefinition::fromJson(n.toJson());
    EXPECT_EQ(n2.triggerData,     (QVector<uint8_t>{0x02, 0x01}));
    EXPECT_EQ(n2.triggerDataMask, (QVector<uint8_t>{0xFF, 0xFF}));
}

TEST_F(NodeSimTest, JsonRoundtrip_LuaFunc) {
    CanNodeDefinition n;
    n.responseLuaFunc = "onEngineTrigger";
    auto n2 = CanNodeDefinition::fromJson(n.toJson());
    EXPECT_EQ(n2.responseLuaFunc, "onEngineTrigger");
}

// ── Config save/load ─────────────────────────────────────────

TEST_F(NodeSimTest, SaveAndLoadConfig) {
    CanNodeSimulator sim;
    CanNodeDefinition n;
    n.name      = "SaveTest";
    n.triggerId = 0x100;
    n.responseId = 0x101;
    n.staticResponse = {0xAA, 0xBB};
    sim.nodes().append(n);

    QTemporaryFile tmpFile;
    ASSERT_TRUE(tmpFile.open());
    QString path = tmpFile.fileName();
    tmpFile.close();

    EXPECT_TRUE(sim.saveConfig(path));

    CanNodeSimulator sim2;
    EXPECT_TRUE(sim2.loadConfig(path));
    ASSERT_EQ(sim2.nodes().size(), 1);
    EXPECT_EQ(sim2.nodes()[0].name,       "SaveTest");
    EXPECT_EQ(sim2.nodes()[0].triggerId,  0x100u);
    EXPECT_EQ(sim2.nodes()[0].responseId, 0x101u);
}

TEST_F(NodeSimTest, LoadNonexistentConfig_ReturnsFalse) {
    CanNodeSimulator sim;
    EXPECT_FALSE(sim.loadConfig("/tmp/nonexistent_magistrala_test_xyz.json"));
}

TEST_F(NodeSimTest, SaveEmptyConfig) {
    CanNodeSimulator sim;
    QTemporaryFile f; f.open(); QString path = f.fileName(); f.close();
    EXPECT_TRUE(sim.saveConfig(path));

    CanNodeSimulator sim2;
    EXPECT_TRUE(sim2.loadConfig(path));
    EXPECT_EQ(sim2.nodes().size(), 0);
}

// ── Trigger matching via onNewFrame ──────────────────────────

TEST_F(NodeSimTest, ExactIdMatch_TriggersResponse) {
    CanNodeSimulator sim;
    CanNodeDefinition n;
    n.triggerId     = 0x7DF;
    n.triggerIdMask = 0xFFFFFFFF;
    n.responseId    = 0x7E8;
    n.staticResponse = {0x01};
    n.responseDelayMs = 0;
    sim.nodes().append(n);

    // Without sniffer, no crash and counter stays at 0 (writeFrame not called)
    sim.onNewFrame(makeFrame(0x7DF, {0x02, 0x01, 0x0C}));
    QCoreApplication::processEvents();
    EXPECT_EQ(sim.nodes()[0].matchCount, 1u);
}

TEST_F(NodeSimTest, WrongId_NoMatch) {
    CanNodeSimulator sim;
    CanNodeDefinition n;
    n.triggerId     = 0x7DF;
    n.triggerIdMask = 0xFFFFFFFF;
    n.staticResponse = {0x01};
    sim.nodes().append(n);

    sim.onNewFrame(makeFrame(0x100));
    EXPECT_EQ(sim.nodes()[0].matchCount, 0u);
}

TEST_F(NodeSimTest, IdMask_BroadcastGroup) {
    CanNodeSimulator sim;
    CanNodeDefinition n;
    // Trigger on any ID with upper nibble = 7 (0x7xx)
    n.triggerId     = 0x700;
    n.triggerIdMask = 0x700;
    n.staticResponse = {0xFF};
    sim.nodes().append(n);

    sim.onNewFrame(makeFrame(0x7E8));
    sim.onNewFrame(makeFrame(0x7DF));
    sim.onNewFrame(makeFrame(0x100));  // no match
    EXPECT_EQ(sim.nodes()[0].matchCount, 2u);
}

TEST_F(NodeSimTest, DataFilter_Match) {
    CanNodeSimulator sim;
    CanNodeDefinition n;
    n.triggerId      = 0x7DF;
    n.triggerIdMask  = 0xFFFFFFFF;
    n.triggerData     = {0x02, 0x01};   // match first 2 bytes
    n.triggerDataMask = {0xFF, 0xFF};
    n.staticResponse  = {0xAA};
    sim.nodes().append(n);

    sim.onNewFrame(makeFrame(0x7DF, {0x02, 0x01, 0x0C}));  // matches
    sim.onNewFrame(makeFrame(0x7DF, {0x03, 0x01, 0x0C}));  // different byte[0]
    EXPECT_EQ(sim.nodes()[0].matchCount, 1u);
}

TEST_F(NodeSimTest, DataMask_PartialMatch) {
    CanNodeSimulator sim;
    CanNodeDefinition n;
    n.triggerId      = 0x200;
    n.triggerIdMask  = 0xFFFFFFFF;
    n.triggerData     = {0xF0};   // high nibble of byte[0] = 0xF
    n.triggerDataMask = {0xF0};   // only high nibble matters
    n.staticResponse  = {0x01};
    sim.nodes().append(n);

    sim.onNewFrame(makeFrame(0x200, {0xFA}));  // 0xFA & 0xF0 = 0xF0 — match
    sim.onNewFrame(makeFrame(0x200, {0xF1}));  // 0xF1 & 0xF0 = 0xF0 — match
    sim.onNewFrame(makeFrame(0x200, {0x0F}));  // 0x0F & 0xF0 = 0x00 — no match
    EXPECT_EQ(sim.nodes()[0].matchCount, 2u);
}

TEST_F(NodeSimTest, DisabledNode_NotTriggered) {
    CanNodeSimulator sim;
    CanNodeDefinition n;
    n.triggerId     = 0x300;
    n.triggerIdMask = 0xFFFFFFFF;
    n.staticResponse = {0x01};
    n.enabled = false;
    sim.nodes().append(n);

    sim.onNewFrame(makeFrame(0x300));
    EXPECT_EQ(sim.nodes()[0].matchCount, 0u);
}

TEST_F(NodeSimTest, MultipleNodes_BothMatch) {
    CanNodeSimulator sim;
    CanNodeDefinition n1, n2;
    n1.triggerId = 0x100; n1.triggerIdMask = 0xFFFFFFFF; n1.staticResponse = {0x01};
    n2.triggerId = 0x100; n2.triggerIdMask = 0xFFFFFFFF; n2.staticResponse = {0x02};
    sim.nodes().append(n1);
    sim.nodes().append(n2);

    sim.onNewFrame(makeFrame(0x100));
    EXPECT_EQ(sim.nodes()[0].matchCount, 1u);
    EXPECT_EQ(sim.nodes()[1].matchCount, 1u);
}

TEST_F(NodeSimTest, EmptyStaticResponse_NoResponse) {
    CanNodeSimulator sim;
    CanNodeDefinition n;
    n.triggerId     = 0x400;
    n.triggerIdMask = 0xFFFFFFFF;
    n.staticResponse = {};  // empty — no response sent
    sim.nodes().append(n);

    sim.onNewFrame(makeFrame(0x400));
    // Still matches the trigger
    EXPECT_EQ(sim.nodes()[0].matchCount, 1u);
    // But responseCount stays 0 since no frame is sent
    EXPECT_EQ(sim.nodes()[0].responseCount, 0u);
}

TEST_F(NodeSimTest, NoNodes_NoOp) {
    CanNodeSimulator sim;
    EXPECT_NO_FATAL_FAILURE(sim.onNewFrame(makeFrame(0x100)));
}
