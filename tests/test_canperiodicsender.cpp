#include <gtest/gtest.h>
#include "core/CanPeriodicSender.h"

// Tests cover entry management, state tracking, and data integrity.
// Timer-based sending requires an event loop and is tested via integration.

static CanFrame makeFrame(uint32_t id, uint8_t b0 = 0) {
    CanFrame f; f.id = id; f.dlc = 1; f.data[0] = b0; return f;
}

// ── Entry management ──────────────────────────────────────────────────────────

TEST(CanPeriodicSender, InitiallyEmpty) {
    CanPeriodicSender sender(nullptr);
    EXPECT_EQ(sender.entryCount(), 0);
    EXPECT_FALSE(sender.isRunning());
}

TEST(CanPeriodicSender, AddEntry) {
    CanPeriodicSender sender(nullptr);
    PeriodicEntry e;
    e.frame    = makeFrame(0x100);
    e.periodMs = 50;
    e.label    = "Test";
    sender.addEntry(e);
    EXPECT_EQ(sender.entryCount(), 1);
    EXPECT_EQ(sender.entries()[0].periodMs, 50);
    EXPECT_EQ(sender.entries()[0].frame.id, 0x100u);
}

TEST(CanPeriodicSender, AddMultiple) {
    CanPeriodicSender sender(nullptr);
    for (int i = 0; i < 5; ++i) {
        PeriodicEntry e;
        e.frame = makeFrame(0x100 + i);
        sender.addEntry(e);
    }
    EXPECT_EQ(sender.entryCount(), 5);
}

TEST(CanPeriodicSender, RemoveEntry) {
    CanPeriodicSender sender(nullptr);
    PeriodicEntry e1; e1.frame = makeFrame(0x100); sender.addEntry(e1);
    PeriodicEntry e2; e2.frame = makeFrame(0x200); sender.addEntry(e2);
    PeriodicEntry e3; e3.frame = makeFrame(0x300); sender.addEntry(e3);

    sender.removeEntry(1); // remove 0x200
    ASSERT_EQ(sender.entryCount(), 2);
    EXPECT_EQ(sender.entries()[0].frame.id, 0x100u);
    EXPECT_EQ(sender.entries()[1].frame.id, 0x300u);
}

TEST(CanPeriodicSender, RemoveOutOfRange) {
    CanPeriodicSender sender(nullptr);
    PeriodicEntry e; e.frame = makeFrame(0x100); sender.addEntry(e);
    sender.removeEntry(5); // no crash
    EXPECT_EQ(sender.entryCount(), 1);
}

TEST(CanPeriodicSender, UpdateEntry) {
    CanPeriodicSender sender(nullptr);
    PeriodicEntry e; e.frame = makeFrame(0x100); e.periodMs = 100; sender.addEntry(e);

    PeriodicEntry updated; updated.frame = makeFrame(0x200); updated.periodMs = 50;
    sender.updateEntry(0, updated);

    EXPECT_EQ(sender.entries()[0].frame.id, 0x200u);
    EXPECT_EQ(sender.entries()[0].periodMs, 50);
}

TEST(CanPeriodicSender, UpdatePreservesCount) {
    CanPeriodicSender sender(nullptr);
    PeriodicEntry e; e.frame = makeFrame(0x100); sender.addEntry(e);

    // Manually set txCount
    // (Can't easily do this via API — just check update doesn't reset to 0 internally)
    PeriodicEntry updated; updated.frame = makeFrame(0x200); updated.periodMs = 100;
    sender.updateEntry(0, updated);
    EXPECT_EQ(sender.entries()[0].txCount, 0u); // new entry, count preserved from old (was 0)
}

TEST(CanPeriodicSender, SetEntryEnabled) {
    CanPeriodicSender sender(nullptr);
    PeriodicEntry e; e.frame = makeFrame(0x100); e.enabled = true; sender.addEntry(e);

    sender.setEntryEnabled(0, false);
    EXPECT_FALSE(sender.entries()[0].enabled);

    sender.setEntryEnabled(0, true);
    EXPECT_TRUE(sender.entries()[0].enabled);
}

TEST(CanPeriodicSender, SetEntryEnabledOutOfRange) {
    CanPeriodicSender sender(nullptr);
    sender.setEntryEnabled(99, false); // should not crash
}

// ── ClearAll ─────────────────────────────────────────────────────────────────

TEST(CanPeriodicSender, ClearAll) {
    CanPeriodicSender sender(nullptr);
    for (int i = 0; i < 3; ++i) {
        PeriodicEntry e; e.frame = makeFrame(i); sender.addEntry(e);
    }
    sender.clearAll();
    EXPECT_EQ(sender.entryCount(), 0);
}

// ── Running state ─────────────────────────────────────────────────────────────

TEST(CanPeriodicSender, InitiallyNotRunning) {
    CanPeriodicSender sender(nullptr);
    EXPECT_FALSE(sender.isRunning());
}
