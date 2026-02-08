#pragma once

#include "engine/core/types.h"
#include "engine/core/assert.h"
#include <atomic>
#include <new>

namespace nge {

// ─── Lock-Free SPSC Ring Buffer ──────────────────────────────────────────
// Single-producer, single-consumer. Used by job system for task queues.
// Capacity must be power of two.
template <typename T, usize Capacity>
class RingBuffer {
    static_assert(IsPowerOfTwo(Capacity), "Capacity must be power of two");
    static constexpr usize MASK = Capacity - 1;

public:
    RingBuffer() = default;

    bool TryPush(const T& item) {
        usize head = m_head.load(std::memory_order_relaxed);
        usize tail = m_tail.load(std::memory_order_acquire);
        if (head - tail >= Capacity) return false; // full

        new (&m_buffer[head & MASK]) T(item);
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    bool TryPush(T&& item) {
        usize head = m_head.load(std::memory_order_relaxed);
        usize tail = m_tail.load(std::memory_order_acquire);
        if (head - tail >= Capacity) return false;

        new (&m_buffer[head & MASK]) T(static_cast<T&&>(item));
        m_head.store(head + 1, std::memory_order_release);
        return true;
    }

    bool TryPop(T& item) {
        usize tail = m_tail.load(std::memory_order_relaxed);
        usize head = m_head.load(std::memory_order_acquire);
        if (tail >= head) return false; // empty

        item = static_cast<T&&>(m_buffer[tail & MASK]);
        m_buffer[tail & MASK].~T();
        m_tail.store(tail + 1, std::memory_order_release);
        return true;
    }

    usize Size() const {
        usize head = m_head.load(std::memory_order_acquire);
        usize tail = m_tail.load(std::memory_order_acquire);
        return head - tail;
    }

    bool IsEmpty() const { return Size() == 0; }
    bool IsFull() const { return Size() >= Capacity; }
    static constexpr usize GetCapacity() { return Capacity; }

private:
    alignas(CACHE_LINE_SIZE) std::atomic<usize> m_head{0};
    alignas(CACHE_LINE_SIZE) std::atomic<usize> m_tail{0};
    alignas(CACHE_LINE_SIZE) T m_buffer[Capacity];
};

} // namespace nge
