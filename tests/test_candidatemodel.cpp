/**
 * @file test_candidatemodel.cpp
 * @brief Unit tests for CandidateModel (QAbstractTableModel wrapper).
 */
#include <gtest/gtest.h>
#include "core/CandidateModel.h"
#include <QCoreApplication>
#include <QSignalSpy>
#include <QColor>

// ── Helpers ──────────────────────────────────────────────────────────────────

static Candidate makeCandidate(uint32_t id, const QString &desc,
                                float score, int occurrences) {
    Candidate c;
    c.canId          = id;
    c.description    = desc;
    c.score          = score;
    c.occurrenceCount = occurrences;
    return c;
}

// ── Initial state ──────────────────────────────────────────────────────────

TEST(CandidateModel, InitialRowCountZero) {
    CandidateModel m;
    EXPECT_EQ(m.rowCount(), 0);
}

TEST(CandidateModel, ColumnCountEqualsEnum) {
    CandidateModel m;
    EXPECT_EQ(m.columnCount(), static_cast<int>(CandidateModel::Count));
}

TEST(CandidateModel, ColumnCountIs4) {
    CandidateModel m;
    EXPECT_EQ(m.columnCount(), 4);
}

// ── setCandidates ─────────────────────────────────────────────────────────

TEST(CandidateModel, SetCandidates_RowCountUpdates) {
    CandidateModel m;
    QVector<Candidate> candidates = {
        makeCandidate(0x100, "A", 0.9f, 5),
        makeCandidate(0x200, "B", 0.5f, 2),
    };
    m.setCandidates(candidates);
    EXPECT_EQ(m.rowCount(), 2);
}

TEST(CandidateModel, SetCandidates_EmptyList_RowCountZero) {
    CandidateModel m;
    m.setCandidates({makeCandidate(0x100, "X", 0.5f, 1)});
    m.setCandidates({});
    EXPECT_EQ(m.rowCount(), 0);
}

TEST(CandidateModel, SetCandidates_EmitsModelReset) {
    CandidateModel m;
    QSignalSpy spy(&m, &CandidateModel::modelReset);
    m.setCandidates({makeCandidate(0x100, "A", 0.5f, 1)});
    EXPECT_EQ(spy.count(), 1);
}

// ── clear ─────────────────────────────────────────────────────────────────

TEST(CandidateModel, Clear_RowCountZero) {
    CandidateModel m;
    m.setCandidates({makeCandidate(0x100, "A", 0.5f, 1)});
    m.clear();
    EXPECT_EQ(m.rowCount(), 0);
}

TEST(CandidateModel, Clear_EmitsModelReset) {
    CandidateModel m;
    m.setCandidates({makeCandidate(0x100, "A", 0.5f, 1)});
    QSignalSpy spy(&m, &CandidateModel::modelReset);
    m.clear();
    EXPECT_EQ(spy.count(), 1);
}

// ── data() — DisplayRole ──────────────────────────────────────────────────

TEST(CandidateModel, Data_ID_Column_HexFormat) {
    CandidateModel m;
    m.setCandidates({makeCandidate(0x1AB, "Test", 0.5f, 3)});
    QModelIndex idx = m.index(0, CandidateModel::ID);
    QString id = m.data(idx, Qt::DisplayRole).toString();
    EXPECT_TRUE(id.contains("1AB", Qt::CaseInsensitive) ||
                id.contains("0x", Qt::CaseInsensitive));
}

TEST(CandidateModel, Data_Description_Column) {
    CandidateModel m;
    m.setCandidates({makeCandidate(0x100, "MySignal", 0.5f, 1)});
    QModelIndex idx = m.index(0, CandidateModel::DESCRIPTION);
    EXPECT_EQ(m.data(idx, Qt::DisplayRole).toString(), "MySignal");
}

TEST(CandidateModel, Data_Score_Column_ContainsPercent) {
    CandidateModel m;
    m.setCandidates({makeCandidate(0x100, "X", 0.75f, 1)});
    QModelIndex idx = m.index(0, CandidateModel::SCORE);
    QString s = m.data(idx, Qt::DisplayRole).toString();
    EXPECT_TRUE(s.contains("%") || s.contains("75"));
}

TEST(CandidateModel, Data_Score_100Percent) {
    CandidateModel m;
    m.setCandidates({makeCandidate(0x100, "X", 1.0f, 1)});
    QModelIndex idx = m.index(0, CandidateModel::SCORE);
    QString s = m.data(idx, Qt::DisplayRole).toString();
    EXPECT_TRUE(s.contains("100"));
}

TEST(CandidateModel, Data_Score_0Percent) {
    CandidateModel m;
    m.setCandidates({makeCandidate(0x100, "X", 0.0f, 1)});
    QModelIndex idx = m.index(0, CandidateModel::SCORE);
    QString s = m.data(idx, Qt::DisplayRole).toString();
    EXPECT_TRUE(s.contains("0.0") || s.contains("0 %") || s.contains("0%"));
}

TEST(CandidateModel, Data_Occurrences_Column) {
    CandidateModel m;
    m.setCandidates({makeCandidate(0x100, "X", 0.5f, 42)});
    QModelIndex idx = m.index(0, CandidateModel::OCCURRENCES);
    EXPECT_EQ(m.data(idx, Qt::DisplayRole).toInt(), 42);
}

// ── data() — invalid index returns null QVariant ──────────────────────────

TEST(CandidateModel, Data_InvalidIndex_ReturnsNull) {
    CandidateModel m;
    QModelIndex invalid;
    EXPECT_FALSE(m.data(invalid, Qt::DisplayRole).isValid());
}

TEST(CandidateModel, Data_OutOfBoundsRow_ReturnsNull) {
    CandidateModel m;
    m.setCandidates({makeCandidate(0x100, "A", 0.5f, 1)});
    QModelIndex idx = m.index(5, CandidateModel::ID);
    // index(5,0) on a 1-row model — Qt returns invalid index
    EXPECT_FALSE(idx.isValid());
}

// ── data() — ForegroundRole ────────────────────────────────────────────────

TEST(CandidateModel, Data_ForegroundRole_ReturnsColor) {
    CandidateModel m;
    m.setCandidates({makeCandidate(0x100, "X", 0.9f, 1)});
    QModelIndex idx = m.index(0, CandidateModel::ID);
    QVariant v = m.data(idx, Qt::ForegroundRole);
    EXPECT_TRUE(v.isValid());
}

TEST(CandidateModel, Data_BackgroundRole_ReturnsColor) {
    CandidateModel m;
    m.setCandidates({makeCandidate(0x100, "X", 0.5f, 1)});
    QModelIndex idx = m.index(0, CandidateModel::ID);
    QVariant v = m.data(idx, Qt::BackgroundRole);
    EXPECT_TRUE(v.isValid());
}

// ── data() — AlternatingRows have different backgrounds ───────────────────

TEST(CandidateModel, Data_AlternatingRowsHaveDifferentBackground) {
    CandidateModel m;
    m.setCandidates({
        makeCandidate(0x100, "A", 0.5f, 1),
        makeCandidate(0x200, "B", 0.5f, 1),
    });
    QColor c0 = m.data(m.index(0, 0), Qt::BackgroundRole).value<QColor>();
    QColor c1 = m.data(m.index(1, 0), Qt::BackgroundRole).value<QColor>();
    EXPECT_NE(c0, c1);
}

// ── headerData ───────────────────────────────────────────────────────────

TEST(CandidateModel, HeaderData_HorizontalNotEmpty) {
    CandidateModel m;
    for (int col = 0; col < m.columnCount(); ++col) {
        QVariant h = m.headerData(col, Qt::Horizontal, Qt::DisplayRole);
        EXPECT_TRUE(h.isValid());
        EXPECT_FALSE(h.toString().isEmpty());
    }
}

TEST(CandidateModel, HeaderData_VerticalReturnsInvalid) {
    CandidateModel m;
    QVariant h = m.headerData(0, Qt::Vertical, Qt::DisplayRole);
    // vertical headers not implemented — should return null
    EXPECT_FALSE(h.isValid());
}

// ── rowCount/columnCount with valid parent → 0 ────────────────────────────

TEST(CandidateModel, RowCount_ValidParent_Zero) {
    CandidateModel m;
    m.setCandidates({makeCandidate(0x100, "A", 0.5f, 1)});
    EXPECT_EQ(m.rowCount(m.index(0, 0)), 0);
}

TEST(CandidateModel, ColumnCount_ValidParent_Zero) {
    CandidateModel m;
    m.setCandidates({makeCandidate(0x100, "A", 0.5f, 1)});
    EXPECT_EQ(m.columnCount(m.index(0, 0)), 0);
}

// ── Multiple rows, data integrity ────────────────────────────────────────

TEST(CandidateModel, MultipleRows_DataIntegrity) {
    CandidateModel m;
    QVector<Candidate> cands = {
        makeCandidate(0x111, "First",  0.95f, 10),
        makeCandidate(0x222, "Second", 0.50f,  5),
        makeCandidate(0x333, "Third",  0.10f,  1),
    };
    m.setCandidates(cands);

    EXPECT_EQ(m.rowCount(), 3);
    EXPECT_TRUE(m.data(m.index(0, CandidateModel::DESCRIPTION), Qt::DisplayRole).toString() == "First");
    EXPECT_TRUE(m.data(m.index(1, CandidateModel::DESCRIPTION), Qt::DisplayRole).toString() == "Second");
    EXPECT_TRUE(m.data(m.index(2, CandidateModel::DESCRIPTION), Qt::DisplayRole).toString() == "Third");
    EXPECT_EQ(m.data(m.index(2, CandidateModel::OCCURRENCES), Qt::DisplayRole).toInt(), 1);
}

// ── Replace candidates keeps count correct ────────────────────────────────

TEST(CandidateModel, ReplaceCandidates_RowCountCorrect) {
    CandidateModel m;
    m.setCandidates({makeCandidate(0x100, "A", 0.5f, 1),
                     makeCandidate(0x200, "B", 0.5f, 1)});
    EXPECT_EQ(m.rowCount(), 2);

    m.setCandidates({makeCandidate(0x300, "C", 0.5f, 1)});
    EXPECT_EQ(m.rowCount(), 1);
}
