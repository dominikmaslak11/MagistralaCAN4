/**
 * @file test_ringbuffer.cpp
 * @brief Unit tests for RingBuffer<T> — SPSC lock-free push/pop/drain/overflow.
 *
 * RingBuffer is header-only (template). QAtomicInteger needs Qt6::Core.
 */

#include <gtest/gtest.h>
#include <QVector>
#include <atomic>
#include <thread>
#include <chrono>
#include "core/RingBuffer.h"

// ── Construction ──

TEST(RingBuffer, defaultCapacityIsZero) {
    RingBuffer<int> rb;
    EXPECT_EQ(rb.capacity(), 0);
    EXPECT_EQ(rb.size(), 0);
}

// ── reserve / capacity (power-of-2 rounding) ──

TEST(RingBuffer, reserveRoundsUpToPowerOf2) {
    RingBuffer<int> rb;
    rb.reserve(3);
    // Usable = pow2 - 1. For minCapacity=3, smallest pow2 >= 3+1 = 4, usable=3.
    EXPECT_EQ(rb.capacity(), 3);
}

TEST(RingBuffer, reserveExactPowerOf2) {
    RingBuffer<int> rb;
    rb.reserve(7);  // next pow2 after 7+1=8 is 8, usable=7
    EXPECT_EQ(rb.capacity(), 7);
}

TEST(RingBuffer, reserveLargeCapacity) {
    RingBuffer<int> rb;
    rb.reserve(1024);
    // 1024+1=1025 → next pow2=2048, usable=2047
    EXPECT_EQ(rb.capacity(), 2047);
}

// ── Empty buffer ──

TEST(RingBuffer, sizeIsZeroWhenEmpty) {
    RingBuffer<int> rb;
    rb.reserve(16);
    EXPECT_EQ(rb.size(), 0);
}

TEST(RingBuffer, drainEmptyBufferReturnsZero) {
    RingBuffer<int> rb;
    rb.reserve(16);
    QVector<int> out;
    EXPECT_EQ(rb.drain(out), 0);
    EXPECT_TRUE(out.isEmpty());
}

// ── Single push / drain ──

TEST(RingBuffer, pushAndDrainSingleItem) {
    RingBuffer<int> rb;
    rb.reserve(16);
    EXPECT_TRUE(rb.tryPush(42));
    EXPECT_EQ(rb.size(), 1);

    QVector<int> out;
    int drained = rb.drain(out);
    EXPECT_EQ(drained, 1);
    EXPECT_EQ(out.size(), 1);
    EXPECT_EQ(out[0], 42);
    EXPECT_EQ(rb.size(), 0);
}

// ── Multiple pushes ──

TEST(RingBuffer, pushAndDrainMultipleItems) {
    RingBuffer<int> rb;
    rb.reserve(16);  // capacity = 15
    for (int i = 0; i < 10; ++i)
        EXPECT_TRUE(rb.tryPush(i * 10));
    EXPECT_EQ(rb.size(), 10);

    QVector<int> out;
    int drained = rb.drain(out);
    EXPECT_EQ(drained, 10);
    for (int i = 0; i < 10; ++i)
        EXPECT_EQ(out[i], i * 10);
    EXPECT_EQ(rb.size(), 0);
}

// ── Full buffer ──

TEST(RingBuffer, tryPushReturnsFalseWhenFull) {
    RingBuffer<int> rb;
    rb.reserve(4);  // capacity = 7
    // Fill 7 slots
    for (int i = 0; i < 7; ++i)
        EXPECT_TRUE(rb.tryPush(i));
    // 8th should fail
    EXPECT_FALSE(rb.tryPush(99));
    EXPECT_EQ(rb.size(), 7);
}

// ── Drain appends (doesn't overwrite existing output data) ──

TEST(RingBuffer, drainAppendsToExistingVector) {
    RingBuffer<int> rb;
    rb.reserve(8);
    rb.tryPush(1);
    rb.tryPush(2);

    QVector<int> out{100, 200};  // existing data
    int drained = rb.drain(out);
    EXPECT_EQ(drained, 2);
    EXPECT_EQ(out.size(), 4);
    EXPECT_EQ(out[0], 100);
    EXPECT_EQ(out[1], 200);
    EXPECT_EQ(out[2], 1);
    EXPECT_EQ(out[3], 2);
}

// ── Wrap-around ──

TEST(RingBuffer, wrapAroundAfterDrainAndRefill) {
    RingBuffer<int> rb;
    rb.reserve(4);  // capacity = 7
    for (int i = 0; i < 5; ++i) rb.tryPush(i);

    QVector<int> out;
    rb.drain(out);  // drain all 5, tail now at 5
    EXPECT_EQ(rb.size(), 0);

    // Push 4 more — should wrap around
    for (int i = 100; i < 104; ++i)
        EXPECT_TRUE(rb.tryPush(i));

    out.clear();
    int drained = rb.drain(out);
    EXPECT_EQ(drained, 4);
    EXPECT_EQ(out[0], 100);
    EXPECT_EQ(out[3], 103);
}

// ── SPSC concurrency stress test ──

TEST(RingBuffer, spscConcurrency) {
    RingBuffer<int> rb;
    rb.reserve(1024);
    std::atomic<bool> running{true};
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};

    std::thread producer([&]() {
        int count = 0;
        while (running.load(std::memory_order_relaxed)) {
            if (rb.tryPush(count)) {
                ++count;
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    std::thread consumer([&]() {
        QVector<int> out;
        while (running.load(std::memory_order_relaxed) || rb.size() > 0) {
            int d = rb.drain(out);
            consumed.fetch_add(d, std::memory_order_relaxed);
            out.clear();
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    running.store(false, std::memory_order_relaxed);
    producer.join();
    consumer.join();

    // Final drain to pick up remaining items
    QVector<int> remaining;
    consumed.fetch_add(rb.drain(remaining), std::memory_order_relaxed);

    EXPECT_EQ(produced.load(), consumed.load());
    EXPECT_GT(produced.load(), 100);  // at least some throughput
}
