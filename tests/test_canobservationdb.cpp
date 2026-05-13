#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QTemporaryFile>
#include "core/CanObservationDb.h"
#include "core/CanFrame.h"

// QSQLITE plugin loader needs QCoreApplication — create one for the whole test binary
namespace {
static char   s_prog[] = "test";
static char  *s_argv[] = { s_prog, nullptr };
static int    s_argc   = 1;

class QtSqlEnvironment : public ::testing::Environment {
    QCoreApplication *m_app = nullptr;
public:
    void SetUp() override {
        if (!QCoreApplication::instance())
            m_app = new QCoreApplication(s_argc, s_argv);
    }
    void TearDown() override { delete m_app; m_app = nullptr; }
};

[[maybe_unused]] static const bool kReg = []() -> bool {
    ::testing::AddGlobalTestEnvironment(new QtSqlEnvironment);
    return true;
}();
} // namespace

class CanObservationDbTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_tmpFile.open();
        m_dbPath = m_tmpFile.fileName();
        m_tmpFile.close();
    }
    void TearDown() override {
        m_db.close();
    }

    static CanFrame makeFrame(uint32_t id, uint8_t dlc, uint8_t fill = 0xAB) {
        CanFrame f{};
        f.id  = id;
        f.dlc = dlc;
        for (int i = 0; i < dlc && i < 8; ++i) f.data[i] = fill;
        return f;
    }

    QTemporaryFile   m_tmpFile;
    QString          m_dbPath;
    CanObservationDb m_db{"test_conn"};
};

TEST_F(CanObservationDbTest, OpenClose) {
    EXPECT_FALSE(m_db.isOpen());
    ASSERT_TRUE(m_db.open(m_dbPath));
    EXPECT_TRUE(m_db.isOpen());
    m_db.close();
    EXPECT_FALSE(m_db.isOpen());
}

TEST_F(CanObservationDbTest, ReopenSameFile) {
    ASSERT_TRUE(m_db.open(m_dbPath));
    m_db.close();
    ASSERT_TRUE(m_db.open(m_dbPath));
    EXPECT_TRUE(m_db.isOpen());
}

TEST_F(CanObservationDbTest, BeginSessionReturnsPositiveId) {
    ASSERT_TRUE(m_db.open(m_dbPath));
    int64_t sid = m_db.beginSession("unit_test");
    EXPECT_GT(sid, 0);
    EXPECT_EQ(m_db.currentSessionId(), sid);
}

TEST_F(CanObservationDbTest, MultipleSessionsIncrease) {
    ASSERT_TRUE(m_db.open(m_dbPath));
    int64_t s1 = m_db.beginSession("s1");
    int64_t s2 = m_db.beginSession("s2");
    EXPECT_GT(s2, s1);
}

TEST_F(CanObservationDbTest, ListSessions) {
    ASSERT_TRUE(m_db.open(m_dbPath));
    m_db.beginSession("alpha");
    m_db.beginSession("beta");
    auto list = m_db.listSessions();
    ASSERT_EQ(list.size(), 2u);
}

TEST_F(CanObservationDbTest, RecordAndCount) {
    ASSERT_TRUE(m_db.open(m_dbPath));
    m_db.beginSession();
    m_db.recordFrame(makeFrame(0x100, 8), 1000);
    m_db.recordFrame(makeFrame(0x200, 4), 2000);
    m_db.flush();
    EXPECT_EQ(m_db.totalFrameCount(), 2);
}

TEST_F(CanObservationDbTest, CountPerSession) {
    ASSERT_TRUE(m_db.open(m_dbPath));
    int64_t s1 = m_db.beginSession("s1");
    m_db.recordFrame(makeFrame(0x100, 8), 1000);
    m_db.flush();
    int64_t s2 = m_db.beginSession("s2");
    m_db.recordFrame(makeFrame(0x200, 4), 2000);
    m_db.recordFrame(makeFrame(0x200, 4), 3000);
    m_db.flush();
    EXPECT_EQ(m_db.totalFrameCount(s1), 1);
    EXPECT_EQ(m_db.totalFrameCount(s2), 2);
    EXPECT_EQ(m_db.totalFrameCount(), 3);
}

TEST_F(CanObservationDbTest, QueryByCanId) {
    ASSERT_TRUE(m_db.open(m_dbPath));
    m_db.beginSession();
    for (int i = 0; i < 5; ++i)
        m_db.recordFrame(makeFrame(0x100, 8, static_cast<uint8_t>(i)), static_cast<uint64_t>(i) * 1000);
    m_db.recordFrame(makeFrame(0x200, 4), 9000);
    m_db.flush();

    auto rows = m_db.queryByCanId(0x100);
    ASSERT_EQ(rows.size(), 5u);
    for (const auto &r : rows)
        EXPECT_EQ(r.canId, 0x100u);
}

TEST_F(CanObservationDbTest, QueryByCanIdFilterSession) {
    ASSERT_TRUE(m_db.open(m_dbPath));
    int64_t s1 = m_db.beginSession("s1");
    m_db.recordFrame(makeFrame(0x100, 8), 1000);
    m_db.flush();
    int64_t s2 = m_db.beginSession("s2");
    m_db.recordFrame(makeFrame(0x100, 8), 2000);
    m_db.recordFrame(makeFrame(0x100, 8), 3000);
    m_db.flush();

    EXPECT_EQ(m_db.queryByCanId(0x100, s1).size(), 1u);
    EXPECT_EQ(m_db.queryByCanId(0x100, s2).size(), 2u);
}

TEST_F(CanObservationDbTest, DataRoundtrip) {
    ASSERT_TRUE(m_db.open(m_dbPath));
    m_db.beginSession();
    CanFrame f = makeFrame(0x3FF, 8, 0);
    for (int i = 0; i < 8; ++i) f.data[i] = static_cast<uint8_t>(i * 11);
    m_db.recordFrame(f, 42000);
    m_db.flush();

    auto rows = m_db.queryByCanId(0x3FF);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].dlc, 8);
    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(rows[0].data[i], static_cast<uint8_t>(i * 11)) << "byte " << i;
}

TEST_F(CanObservationDbTest, BatchInsertFlushes) {
    ASSERT_TRUE(m_db.open(m_dbPath));
    m_db.beginSession();
    for (int i = 0; i < CanObservationDb::kBatchSize; ++i)
        m_db.recordFrame(makeFrame(0x1, 1, static_cast<uint8_t>(i)), static_cast<uint64_t>(i));
    EXPECT_EQ(m_db.totalFrameCount(), CanObservationDb::kBatchSize);
}

TEST_F(CanObservationDbTest, ResetClearsAll) {
    ASSERT_TRUE(m_db.open(m_dbPath));
    m_db.beginSession("before_reset");
    m_db.recordFrame(makeFrame(0x10, 8), 1000);
    m_db.flush();
    ASSERT_GT(m_db.totalFrameCount(), 0);

    m_db.reset();
    EXPECT_EQ(m_db.totalFrameCount(), 0);
    EXPECT_TRUE(m_db.listSessions().empty());
}

TEST_F(CanObservationDbTest, NoOpenNoRecord) {
    m_db.recordFrame(makeFrame(0x100, 8), 0);
    m_db.flush();
    EXPECT_EQ(m_db.totalFrameCount(), 0);
}
