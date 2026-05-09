#pragma once
#include <QAtomicInteger>
#include <QVector>
#include <cstddef>
#include <type_traits>

/**
 * @brief Lock-free SPSC (Single Producer, Single Consumer) ring buffer.
 *
 * Producer thread writes via tryPush(); consumer thread drains via drain().
 * Uses atomic head/tail indices — no mutex needed.
 * Capacity is rounded up to the next power of 2; one slot is reserved
 * for unambiguous full/empty detection.
 *
 * @tparam T  Trivially copyable type.
 */
template<typename T>
class RingBuffer {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

public:
    RingBuffer() = default;

    /// Resize buffer to at least @p minCapacity slots (usable = pow2 - 1).
    void reserve(int minCapacity) {
        int cap = 1;
        while (cap < minCapacity + 1) cap <<= 1;  // +1 for sentinel slot
        m_mask = cap - 1;
        m_buffer.resize(cap);
    }

    /// Current usable capacity.
    int capacity() const { return m_mask + 1 - 1; }

    /// Number of items currently buffered.
    int size() const {
        int h = m_head.loadAcquire();
        int t = m_tail.loadAcquire();
        return (h - t) & m_mask;
    }

    /// Try to push one item. Returns true on success, false if full.
    bool tryPush(const T &item) {
        int h = m_head.loadAcquire();
        int next = (h + 1) & m_mask;
        if (next == m_tail.loadAcquire()) return false;  // full
        m_buffer[h] = item;
        m_head.storeRelease(next);
        return true;
    }

    /// Drain all available items into @p out (appended).
    /// Returns number of items drained.
    int drain(QVector<T> &out) {
        int t = m_tail.loadAcquire();
        int h = m_head.loadAcquire();
        int n = (h - t) & m_mask;
        if (n == 0) return 0;

        int startIdx = out.size();
        out.resize(startIdx + n);
        for (int i = 0; i < n; ++i)
            out[startIdx + i] = m_buffer[(t + i) & m_mask];

        m_tail.storeRelease((t + n) & m_mask);
        return n;
    }

private:
    QVector<T>          m_buffer;
    QAtomicInteger<int> m_head{0};
    QAtomicInteger<int> m_tail{0};
    int                 m_mask = 0;
};
