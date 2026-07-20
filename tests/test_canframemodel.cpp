/**
 * @file test_canframemodel.cpp
 * @brief Unit tests for CanFrameModel — rowCount, columnCount, overwrite, sort, clear.
 *
 * CanFrameModel is a QAbstractTableModel. Requires QCoreApplication (Qt6::Core).
 * Does not require J1939Parser linkage (not used in tests).
 */

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QSignalSpy>
#include "core/CanFrameModel.h"

// ── Helper: create a CanFrame with given ID ──

static CanFrame makeFrame(uint32_t id, uint8_t byte0 = 0) {
    CanFrame f;
    f.id = id;
    f.data[0] = byte0;
    f.dlc = 1;
    return f;
}

// ── Initial state ──

TEST(CanFrameModel, initialRowCountIsZero) {
    CanFrameModel model;
    EXPECT_EQ(model.rowCount(), 0);
}

TEST(CanFrameModel, columnCountMatchesEnum) {
    CanFrameModel model;
    EXPECT_EQ(model.columnCount(), CanFrameModel::Count);
    EXPECT_GE(model.columnCount(), 8);
}

// ── Overwrite mode (default) ──

TEST(CanFrameModel, overwriteModeReplacesById) {
    CanFrameModel model;
    model.processIncomingFrames({makeFrame(0x100, 10)});
    EXPECT_EQ(model.rowCount(), 1);
    EXPECT_EQ(model.frameAt(0).data[0], 10);

    model.processIncomingFrames({makeFrame(0x100, 20)});
    EXPECT_EQ(model.rowCount(), 1);
    EXPECT_EQ(model.frameAt(0).data[0], 20);
}

TEST(CanFrameModel, overwriteModeKeepsDistinctIds) {
    CanFrameModel model;
    model.processIncomingFrames({
        makeFrame(0x100, 1),
        makeFrame(0x200, 2),
        makeFrame(0x300, 3)
    });
    EXPECT_EQ(model.rowCount(), 3);

    model.processIncomingFrames({makeFrame(0x200, 99)});
    EXPECT_EQ(model.rowCount(), 3);
    EXPECT_EQ(model.frameAt(0).id, 0x100u);
    EXPECT_EQ(model.frameAt(1).id, 0x200u);
    EXPECT_EQ(model.frameAt(1).data[0], 99);
    EXPECT_EQ(model.frameAt(2).id, 0x300u);
}

// ── Append mode ──

TEST(CanFrameModel, appendModeAddsDuplicateIds) {
    CanFrameModel model;
    model.setOverwriteMode(false);
    model.processIncomingFrames({makeFrame(0x100, 1)});
    model.processIncomingFrames({makeFrame(0x100, 2)});
    EXPECT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.frameAt(0).data[0], 1);
    EXPECT_EQ(model.frameAt(1).data[0], 2);
}

// ── maxFrames limits rows ──

TEST(CanFrameModel, maxFramesEnforced) {
    CanFrameModel model;
    model.setOverwriteMode(false);
    model.setMaxFrames(5);

    QVector<CanFrame> batch;
    for (int i = 0; i < 10; ++i)
        batch.append(makeFrame(0x100 + i, static_cast<uint8_t>(i)));
    model.processIncomingFrames(batch);

    EXPECT_EQ(model.rowCount(), 5);
}

// ── clear ──

TEST(CanFrameModel, clearEmptiesModel) {
    CanFrameModel model;
    model.processIncomingFrames({makeFrame(0x100), makeFrame(0x200)});
    EXPECT_EQ(model.rowCount(), 2);
    model.clear();
    EXPECT_EQ(model.rowCount(), 0);
}

// ── allFrames ──

TEST(CanFrameModel, allFramesReturnsAllFrames) {
    CanFrameModel model;
    QVector<CanFrame> batch = {makeFrame(0x100, 10), makeFrame(0x200, 20)};
    model.processIncomingFrames(batch);

    auto all = model.allFrames();
    ASSERT_EQ(all.size(), 2);
    EXPECT_EQ(all[0].data[0], 10);
    EXPECT_EQ(all[1].data[0], 20);
}

// ── frameAt bounds ──

TEST(CanFrameModel, frameAtReturnsDefaultForOutOfBounds) {
    CanFrameModel model;
    model.processIncomingFrames({makeFrame(0x100)});

    CanFrame f = model.frameAt(0);
    EXPECT_EQ(f.id, 0x100u);

    CanFrame fBad = model.frameAt(999);
    EXPECT_EQ(fBad.id, 0u);
}

// ── Header data ──

TEST(CanFrameModel, headerDataReturnsNonEmptyStrings) {
    CanFrameModel model;
    for (int col = 0; col < model.columnCount(); ++col) {
        QVariant h = model.headerData(col, Qt::Horizontal, Qt::DisplayRole);
        EXPECT_FALSE(h.toString().isEmpty()) << "Column " << col;
    }
}

// ── data() for display role ──

TEST(CanFrameModel, dataReturnsNonEmptyForDisplayRole) {
    CanFrameModel model;
    model.processIncomingFrames({makeFrame(0x123, 0xAB)});

    QVariant idData = model.data(model.index(0, CanFrameModel::ID), Qt::DisplayRole);
    EXPECT_TRUE(idData.isValid());

    QVariant dlcData = model.data(model.index(0, CanFrameModel::DLC), Qt::DisplayRole);
    EXPECT_TRUE(dlcData.isValid());
}

// ── Signal: frameBatchUpdated emitted ──

TEST(CanFrameModel, frameBatchUpdatedSignalEmitted) {
    CanFrameModel model;
    QSignalSpy spy(&model, &CanFrameModel::frameBatchUpdated);

    model.processIncomingFrames({makeFrame(0x100)});
    EXPECT_EQ(spy.count(), 1);
}

// ── Sorting doesn't crash ──

TEST(CanFrameModel, sortDoesNotCrash) {
    CanFrameModel model;
    model.processIncomingFrames({
        makeFrame(0x300), makeFrame(0x100), makeFrame(0x200)
    });
    EXPECT_NO_THROW(model.sort(CanFrameModel::ID, Qt::AscendingOrder));
    EXPECT_NO_THROW(model.sort(CanFrameModel::ID, Qt::DescendingOrder));
}

// ── Undo/Redo ──

TEST(CanFrameModel, canUndo_FalseInitially) {
    CanFrameModel model;
    EXPECT_FALSE(model.canUndo());
}

TEST(CanFrameModel, canRedo_FalseInitially) {
    CanFrameModel model;
    EXPECT_FALSE(model.canRedo());
}

TEST(CanFrameModel, canUndo_TrueAfterClear) {
    CanFrameModel model;
    model.processIncomingFrames({makeFrame(0x100), makeFrame(0x200)});
    EXPECT_FALSE(model.canUndo());
    model.clear();
    EXPECT_TRUE(model.canUndo());
}

TEST(CanFrameModel, undo_AfterClear_RestoresFrames) {
    CanFrameModel model;
    model.processIncomingFrames({makeFrame(0x100, 10), makeFrame(0x200, 20)});
    model.clear();
    ASSERT_EQ(model.rowCount(), 0);

    model.undo();
    EXPECT_EQ(model.rowCount(), 2);
    EXPECT_EQ(model.frameAt(0).id, 0x100u);
    EXPECT_EQ(model.frameAt(0).data[0], 10);
    EXPECT_EQ(model.frameAt(1).id, 0x200u);
}

TEST(CanFrameModel, canRedo_TrueAfterUndo) {
    CanFrameModel model;
    model.processIncomingFrames({makeFrame(0x100)});
    model.clear();
    model.undo();
    EXPECT_TRUE(model.canRedo());
}

TEST(CanFrameModel, redo_AfterUndo_ClearsAgain) {
    CanFrameModel model;
    model.processIncomingFrames({makeFrame(0x100), makeFrame(0x200)});
    model.clear();
    model.undo();
    EXPECT_EQ(model.rowCount(), 2);

    model.redo();
    EXPECT_EQ(model.rowCount(), 0);
}

TEST(CanFrameModel, undo_EmptyStack_DoesNotCrash) {
    CanFrameModel model;
    EXPECT_NO_THROW(model.undo());  // nothing to undo
    EXPECT_EQ(model.rowCount(), 0);
}

TEST(CanFrameModel, redo_EmptyStack_DoesNotCrash) {
    CanFrameModel model;
    EXPECT_NO_THROW(model.redo());
    EXPECT_EQ(model.rowCount(), 0);
}

TEST(CanFrameModel, undo_AfterSetOverwriteMode_RestoresFrames) {
    CanFrameModel model;
    model.processIncomingFrames({makeFrame(0x100, 5)});

    model.setOverwriteMode(false);  // triggers saveUndoState + clear
    ASSERT_EQ(model.rowCount(), 0);
    EXPECT_TRUE(model.canUndo());

    model.undo();
    EXPECT_EQ(model.rowCount(), 1);
    EXPECT_EQ(model.frameAt(0).data[0], 5);
}

TEST(CanFrameModel, newOperationClearsRedoStack) {
    CanFrameModel model;
    model.processIncomingFrames({makeFrame(0x100)});
    model.clear();
    model.undo();
    EXPECT_TRUE(model.canRedo());

    // A new destructive operation (clear again) should invalidate redo
    model.clear();
    EXPECT_FALSE(model.canRedo());
}

// ── changedMask ──

TEST(CanFrameModel, changedMask_FirstFrameIsZero) {
    CanFrameModel model;
    model.processIncomingFrames({makeFrame(0x100, 0xAA)});
    // First time seeing this ID → no previous data → mask is 0
    CanFrame f = model.frameAt(0);
    EXPECT_EQ(f.changedMask, 0u);
}

TEST(CanFrameModel, changedMask_SetForChangedByte) {
    CanFrameModel model;
    model.processIncomingFrames({makeFrame(0x100, 0x11)});
    model.processIncomingFrames({makeFrame(0x100, 0xFF)});  // byte[0] changed
    CanFrame f = model.frameAt(0);
    EXPECT_EQ(f.changedMask & 1u, 1u);  // bit 0 set for byte[0]
}

TEST(CanFrameModel, changedMask_ClearForUnchangedByte) {
    CanFrameModel model;
    model.processIncomingFrames({makeFrame(0x100, 0xAA)});
    model.processIncomingFrames({makeFrame(0x100, 0xAA)});  // no change
    CanFrame f = model.frameAt(0);
    EXPECT_EQ(f.changedMask, 0u);
}
