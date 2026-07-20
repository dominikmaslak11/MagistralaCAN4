#include <gtest/gtest.h>
#include "core/PluginLoader.h"
#include "core/ProtocolPlugin.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QFile>

// ── QApplication environment ──────────────────────────────────

static int    s_argc   = 1;
static char  *s_argbuf = (char*)"test_pluginloader";
static char **s_argv   = &s_argbuf;

class PluginLoaderEnv : public ::testing::Environment {
    QApplication *m_app = nullptr;
public:
    void SetUp()    override {
        if (!QApplication::instance())
            m_app = new QApplication(s_argc, s_argv);
    }
    void TearDown() override { delete m_app; m_app = nullptr; }
};

[[maybe_unused]] static const bool kEnvReg = []() -> bool {
    ::testing::AddGlobalTestEnvironment(new PluginLoaderEnv);
    return true;
}();

// ── Stub plugin ───────────────────────────────────────────────

class StubPlugin : public ProtocolPlugin {
public:
    QString name() const override { return "StubPlugin"; }
    QString description() const override { return "Test stub"; }
    void processFrame(const CanFrame &) override { ++processCount; }
    QWidget *widget() override { return nullptr; }
    bool isRelevant(const CanFrame &f) override { return f.id == targetId; }

    int processCount = 0;
    uint32_t targetId = 0xFFFFFFFF;  // default: match nothing
};

// ══════════════════════════════════════════════════════════════
// Initial state
// ══════════════════════════════════════════════════════════════

TEST(PluginLoaderTest, InitialState_NoPlugins) {
    PluginLoader loader;
    EXPECT_TRUE(loader.plugins().isEmpty());
}

// ══════════════════════════════════════════════════════════════
// loadFromDirectory
// ══════════════════════════════════════════════════════════════

TEST(PluginLoaderTest, LoadFromDirectory_Nonexistent_ReturnsZero) {
    PluginLoader loader;
    EXPECT_EQ(loader.loadFromDirectory("/no/such/dir"), 0);
    EXPECT_TRUE(loader.plugins().isEmpty());
}

TEST(PluginLoaderTest, LoadFromDirectory_EmptyDir_ReturnsZero) {
    QTemporaryDir dir;
    PluginLoader loader;
    EXPECT_EQ(loader.loadFromDirectory(dir.path()), 0);
    EXPECT_TRUE(loader.plugins().isEmpty());
}

TEST(PluginLoaderTest, LoadFromDirectory_DirWithNonSoFiles_ReturnsZero) {
    QTemporaryDir dir;
    QFile f(dir.filePath("not_a_plugin.txt"));
    f.open(QIODevice::WriteOnly);
    f.write("not a shared library");
    f.close();

    PluginLoader loader;
    EXPECT_EQ(loader.loadFromDirectory(dir.path()), 0);
    EXPECT_TRUE(loader.plugins().isEmpty());
}

TEST(PluginLoaderTest, LoadFromDirectory_InvalidSo_SkipsGracefully) {
    QTemporaryDir dir;
    // Create a fake .so file with invalid content
    QFile f(dir.filePath("fake.so"));
    f.open(QIODevice::WriteOnly);
    f.write(QByteArray(64, 0));  // not a valid ELF
    f.close();

    PluginLoader loader;
    int count = loader.loadFromDirectory(dir.path());
    EXPECT_EQ(count, 0);        // can't load invalid ELF
    EXPECT_TRUE(loader.plugins().isEmpty());
}

// ══════════════════════════════════════════════════════════════
// broadcastFrame with no plugins
// ══════════════════════════════════════════════════════════════

TEST(PluginLoaderTest, BroadcastFrame_NoPlugins_DoesNotCrash) {
    PluginLoader loader;
    CanFrame f{};
    f.id = 0x100; f.dlc = 1;
    EXPECT_NO_THROW(loader.broadcastFrame(f));
}

TEST(PluginLoaderTest, BroadcastFrame_NoPlugins_MultipleFrames) {
    PluginLoader loader;
    for (int i = 0; i < 10; ++i) {
        CanFrame f{};
        f.id = static_cast<uint32_t>(i);
        loader.broadcastFrame(f);
    }
    SUCCEED();
}

// ══════════════════════════════════════════════════════════════
// Plugin interface
// ══════════════════════════════════════════════════════════════

TEST(PluginLoaderTest, StubPlugin_Name) {
    StubPlugin p;
    EXPECT_EQ(p.name(), "StubPlugin");
}

TEST(PluginLoaderTest, StubPlugin_Widget_Null) {
    StubPlugin p;
    EXPECT_EQ(p.widget(), nullptr);
}

TEST(PluginLoaderTest, StubPlugin_IsRelevant_MatchesTargetId) {
    StubPlugin p;
    p.targetId = 0x200;
    CanFrame f{};
    f.id = 0x200;
    EXPECT_TRUE(p.isRelevant(f));

    f.id = 0x100;
    EXPECT_FALSE(p.isRelevant(f));
}

TEST(PluginLoaderTest, StubPlugin_ProcessFrame_CountsInvocations) {
    StubPlugin p;
    p.targetId = 0x100;
    CanFrame f{};
    f.id = 0x100;
    p.processFrame(f);
    p.processFrame(f);
    EXPECT_EQ(p.processCount, 2);
}

// ══════════════════════════════════════════════════════════════
// StubPlugin additional interface coverage
// ══════════════════════════════════════════════════════════════

TEST(PluginLoaderTest, StubPlugin_Description_ReturnsText) {
    StubPlugin p;
    EXPECT_FALSE(p.description().isEmpty());
}

TEST(PluginLoaderTest, StubPlugin_ProcessCount_StartsAtZero) {
    StubPlugin p;
    EXPECT_EQ(p.processCount, 0);
}

TEST(PluginLoaderTest, StubPlugin_IsRelevant_DefaultTargetMatchesNothing) {
    StubPlugin p; // targetId = 0xFFFFFFFF by default
    CanFrame f{};
    f.id = 0x7FF; // max standard CAN id
    EXPECT_FALSE(p.isRelevant(f));
    f.id = 0x1FFFFFFF; // max extended CAN id
    EXPECT_FALSE(p.isRelevant(f));
}

TEST(PluginLoaderTest, StubPlugin_IsRelevant_ZeroIdMatchesZeroTarget) {
    StubPlugin p;
    p.targetId = 0;
    CanFrame f{};
    f.id = 0;
    EXPECT_TRUE(p.isRelevant(f));
}

// ══════════════════════════════════════════════════════════════
// Two loaders are independent
// ══════════════════════════════════════════════════════════════

TEST(PluginLoaderTest, TwoLoaders_IndependentState) {
    PluginLoader a, b;
    EXPECT_EQ(a.plugins().size(), b.plugins().size());
    // Loading from a bad dir into one should not affect the other
    a.loadFromDirectory("/no/such/dir");
    EXPECT_EQ(a.plugins().size(), 0);
    EXPECT_EQ(b.plugins().size(), 0);
}

// ══════════════════════════════════════════════════════════════
// broadcastFrame with no plugins — idempotent, no state leak
// ══════════════════════════════════════════════════════════════

TEST(PluginLoaderTest, BroadcastFrame_ManyFrames_NoStateAccumulation) {
    PluginLoader loader;
    for (int i = 0; i < 1000; ++i) {
        CanFrame f{};
        f.id = static_cast<uint32_t>(i % 256);
        f.dlc = 8;
        loader.broadcastFrame(f);
    }
    // Must remain empty — 1000 broadcasts don't magically load plugins
    EXPECT_TRUE(loader.plugins().isEmpty());
}
