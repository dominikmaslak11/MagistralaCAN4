#include <gtest/gtest.h>
#include "core/CanFilterProxy.h"
#include "core/CanFrameModel.h"
#include <QApplication>
#include <QCoreApplication>

// ── QApplication environment ──────────────────────────────────

static int    s_argc   = 1;
static char  *s_argbuf = (char*)"test_canfilterproxy";
static char **s_argv   = &s_argbuf;

class CanFilterProxyEnv : public ::testing::Environment {
    QApplication *m_app = nullptr;
public:
    void SetUp()    override {
        if (!QApplication::instance())
            m_app = new QApplication(s_argc, s_argv);
    }
    void TearDown() override { delete m_app; m_app = nullptr; }
};

[[maybe_unused]] static const bool kEnvReg = []() -> bool {
    ::testing::AddGlobalTestEnvironment(new CanFilterProxyEnv);
    return true;
}();

// ── Helpers ───────────────────────────────────────────────────

static CanFrame makeFrame(uint32_t id,
                          std::initializer_list<uint8_t> bytes = {0x00},
                          uint64_t ts = 0) {
    CanFrame f{};
    f.id = id; f.dlc = static_cast<uint8_t>(bytes.size()); f.timestamp = ts;
    int i = 0;
    for (uint8_t b : bytes) f.data[i++] = b;
    return f;
}

// Returns a model pre-loaded with the given frames (overwrite mode: unique IDs).
static CanFrameModel *makeModel(std::initializer_list<CanFrame> frames,
                                QObject *parent = nullptr) {
    auto *m = new CanFrameModel(parent);
    QVector<CanFrame> v(frames);
    m->processIncomingFrames(v);
    return m;
}

// ══════════════════════════════════════════════════════════════
// Initial state
// ══════════════════════════════════════════════════════════════

TEST(CanFilterProxyTest, InitialState_FilterInactive) {
    CanFilterProxy proxy;
    EXPECT_FALSE(proxy.isFilterActive());
    EXPECT_TRUE(proxy.currentFilterId().isEmpty());
}

TEST(CanFilterProxyTest, NoFilter_AllRowsVisible) {
    auto *model = makeModel({makeFrame(0x100), makeFrame(0x200), makeFrame(0x300)});
    CanFilterProxy proxy;
    proxy.setSourceModel(model);
    EXPECT_EQ(proxy.rowCount(), 3);
    delete model;
}

// ══════════════════════════════════════════════════════════════
// ID filter
// ══════════════════════════════════════════════════════════════

TEST(CanFilterProxyTest, IdFilter_MatchingId_OneRow) {
    auto *model = makeModel({makeFrame(0x100), makeFrame(0x200), makeFrame(0x300)});
    CanFilterProxy proxy;
    proxy.setSourceModel(model);
    proxy.setIdFilter("100");
    EXPECT_TRUE(proxy.isFilterActive());
    EXPECT_EQ(proxy.rowCount(), 1);
    delete model;
}

TEST(CanFilterProxyTest, IdFilter_NoMatch_ZeroRows) {
    auto *model = makeModel({makeFrame(0x100), makeFrame(0x200)});
    CanFilterProxy proxy;
    proxy.setSourceModel(model);
    proxy.setIdFilter("7FF");
    EXPECT_EQ(proxy.rowCount(), 0);
    delete model;
}

TEST(CanFilterProxyTest, IdFilter_HexPrefix_Works) {
    auto *model = makeModel({makeFrame(0x200), makeFrame(0x300)});
    CanFilterProxy proxy;
    proxy.setSourceModel(model);
    proxy.setIdFilter("0x200");
    EXPECT_EQ(proxy.rowCount(), 1);
    delete model;
}

TEST(CanFilterProxyTest, IdFilter_HexUpperCase_Works) {
    auto *model = makeModel({makeFrame(0x1AB), makeFrame(0x200)});
    CanFilterProxy proxy;
    proxy.setSourceModel(model);
    proxy.setIdFilter("1AB");
    EXPECT_EQ(proxy.rowCount(), 1);
    delete model;
}

TEST(CanFilterProxyTest, IdFilter_ClearFilter_AllRowsReturn) {
    auto *model = makeModel({makeFrame(0x100), makeFrame(0x200), makeFrame(0x300)});
    CanFilterProxy proxy;
    proxy.setSourceModel(model);
    proxy.setIdFilter("100");
    EXPECT_EQ(proxy.rowCount(), 1);

    proxy.setIdFilter("");  // clear
    EXPECT_FALSE(proxy.isFilterActive());
    EXPECT_EQ(proxy.rowCount(), 3);
    delete model;
}

TEST(CanFilterProxyTest, IdFilter_CurrentFilterId) {
    CanFilterProxy proxy;
    proxy.setIdFilter("1AB");
    EXPECT_EQ(proxy.currentFilterId(), "1AB");
}

TEST(CanFilterProxyTest, IdFilter_CurrentFilterId_AfterClear) {
    CanFilterProxy proxy;
    proxy.setIdFilter("100");
    proxy.setIdFilter("");
    EXPECT_TRUE(proxy.currentFilterId().isEmpty());
}

// ══════════════════════════════════════════════════════════════
// Expression filter
// ══════════════════════════════════════════════════════════════

TEST(CanFilterProxyTest, ExprFilter_ValidExpr_NoError) {
    CanFilterProxy proxy;
    QString err = proxy.setExpression("can.id==0x100");
    EXPECT_TRUE(err.isEmpty()) << err.toStdString();
}

TEST(CanFilterProxyTest, ExprFilter_EmptyExpr_Clears) {
    CanFilterProxy proxy;
    proxy.setExpression("can.id==0x100");
    proxy.setExpression("");
    EXPECT_FALSE(proxy.isFilterActive());
}

TEST(CanFilterProxyTest, ExprFilter_ById_FiltersCorrectly) {
    auto *model = makeModel({
        makeFrame(0x100, {0x01}),
        makeFrame(0x200, {0x02}),
        makeFrame(0x300, {0x03}),
    });
    CanFilterProxy proxy;
    proxy.setSourceModel(model);
    proxy.setExpression("can.id==0x200");
    EXPECT_EQ(proxy.rowCount(), 1);
    delete model;
}

TEST(CanFilterProxyTest, ExprFilter_ByData_FiltersCorrectly) {
    auto *model = makeModel({
        makeFrame(0x100, {0x01}),
        makeFrame(0x200, {0xFF}),
        makeFrame(0x300, {0x01}),
    });
    CanFilterProxy proxy;
    proxy.setSourceModel(model);
    proxy.setExpression("can.data[0]==0xFF");
    EXPECT_EQ(proxy.rowCount(), 1);
    delete model;
}

TEST(CanFilterProxyTest, ExprFilter_ByDlc_FiltersCorrectly) {
    auto *model = makeModel({
        makeFrame(0x100, {0x01}),                              // dlc=1
        makeFrame(0x200, {0x01, 0x02}),                       // dlc=2
        makeFrame(0x300, {0x01, 0x02, 0x03}),                 // dlc=3
    });
    CanFilterProxy proxy;
    proxy.setSourceModel(model);
    proxy.setExpression("can.dlc==3");
    EXPECT_EQ(proxy.rowCount(), 1);
    delete model;
}

TEST(CanFilterProxyTest, ExprFilter_AndCondition) {
    auto *model = makeModel({
        makeFrame(0x100, {0x01}),
        makeFrame(0x100, {0xFF}),  // same ID → overwrites in overwrite mode
        makeFrame(0x200, {0xFF}),
    });
    CanFilterProxy proxy;
    proxy.setSourceModel(model);
    proxy.setExpression("can.id==0x200 && can.data[0]==0xFF");
    EXPECT_EQ(proxy.rowCount(), 1);
    delete model;
}

TEST(CanFilterProxyTest, ExprFilter_ReplaceIdFilter) {
    auto *model = makeModel({makeFrame(0x100), makeFrame(0x200)});
    CanFilterProxy proxy;
    proxy.setSourceModel(model);
    proxy.setIdFilter("100");
    EXPECT_EQ(proxy.rowCount(), 1);
    // Setting expression replaces the ID filter
    proxy.setExpression("can.id==0x200");
    EXPECT_EQ(proxy.rowCount(), 1);
    // ID filter should be cleared
    EXPECT_TRUE(proxy.currentFilterId().isEmpty());
    delete model;
}

// ══════════════════════════════════════════════════════════════
// isFilterActive
// ══════════════════════════════════════════════════════════════

TEST(CanFilterProxyTest, IsFilterActive_IdFilter_True) {
    CanFilterProxy proxy;
    proxy.setIdFilter("123");
    EXPECT_TRUE(proxy.isFilterActive());
}

TEST(CanFilterProxyTest, IsFilterActive_ExprFilter_True) {
    CanFilterProxy proxy;
    proxy.setExpression("can.id==0x100");
    EXPECT_TRUE(proxy.isFilterActive());
}

TEST(CanFilterProxyTest, IsFilterActive_BothCleared_False) {
    CanFilterProxy proxy;
    proxy.setIdFilter("123");
    proxy.setExpression("can.id==0x100");
    proxy.setExpression("");
    // After clearing the expr, the ID filter was replaced — so both inactive
    EXPECT_FALSE(proxy.isFilterActive());
}

// ══════════════════════════════════════════════════════════════
// Edge cases
// ══════════════════════════════════════════════════════════════

TEST(CanFilterProxyTest, EmptyModel_NoFilter_ZeroRows) {
    auto *model = makeModel({});
    CanFilterProxy proxy;
    proxy.setSourceModel(model);
    EXPECT_EQ(proxy.rowCount(), 0);
    delete model;
}

TEST(CanFilterProxyTest, EmptyModel_WithFilter_ZeroRows) {
    auto *model = makeModel({});
    CanFilterProxy proxy;
    proxy.setSourceModel(model);
    proxy.setIdFilter("100");
    EXPECT_EQ(proxy.rowCount(), 0);
    delete model;
}

TEST(CanFilterProxyTest, IdFilter_CaseInsensitive) {
    auto *model = makeModel({makeFrame(0x1AB)});
    CanFilterProxy proxy;
    proxy.setSourceModel(model);
    proxy.setIdFilter("1ab");
    EXPECT_EQ(proxy.rowCount(), 1);
    delete model;
}
