#pragma once

#include "engine/core/types.h"
#include <cstdlib>
#include <cassert>
#include <mutex>

namespace nge::memory {

// ─── Frame Allocator ─────────────────────────────────────────────────────
// Ultra-fast linear allocator for per-frame scratch memory.
// Allocations are O(1) bump-pointer. All memory is freed at once via Reset().
// No individual deallocation — ideal for transient per-frame data.
//
// Double-buffered: two backing blocks alternate each frame so that
// previous-frame data remains valid during the current frame.
//
// Usage:
//   frameAlloc.BeginFrame();
//   auto* data = frameAlloc.Allocate<MyStruct>(count);
//   // ... use data this frame ...
//   // Next BeginFrame() invalidates previous frame's allocations

class FrameAllocator {
public:
    explicit FrameAllocator(usize blockSize = 8 * 1024 * 1024); // 8 MB default
    ~FrameAllocator();

    FrameAllocator(const FrameAllocator&) = delete;
    FrameAllocator& operator=(const FrameAllocator&) = delete;

    // Call at frame start — resets the current frame's block
    void BeginFrame();

    // Allocate raw bytes (aligned)
    void* Allocate(usize size, usize alignment = 16);

    // Typed allocation
    template<typename T>
    T* Allocate(usize count = 1) {
        return static_cast<T*>(Allocate(sizeof(T) * count, alignof(T)));
    }

    // Get remaining capacity in the current block
    usize GetRemainingBytes() const;

    // Get total bytes allocated this frame
    usize GetAllocatedBytes() const { return m_offset; }

    // Get total capacity
    usize GetCapacity() const { return m_blockSize; }

    // Get peak usage (across all frames)
    usize GetPeakUsage() const { return m_peakUsage; }

    // Reset statistics
    void ResetStats() { m_peakUsage = 0; }

private:
    usize AlignUp(usize value, usize alignment) const {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    u8*   m_blocks[2] = {nullptr, nullptr}; // Double-buffered
    usize m_blockSize;
    usize m_offset = 0;
    u32   m_currentBlock = 0;
    usize m_peakUsage = 0;
};

// ─── Thread-Safe Frame Allocator ─────────────────────────────────────────
// Wraps FrameAllocator with a mutex for multi-threaded allocation.

class ThreadSafeFrameAllocator {
public:
    explicit ThreadSafeFrameAllocator(usize blockSize = 8 * 1024 * 1024)
        : m_inner(blockSize) {}

    void BeginFrame() {
        std::lock_guard lock(m_mutex);
        m_inner.BeginFrame();
    }

    void* Allocate(usize size, usize alignment = 16) {
        std::lock_guard lock(m_mutex);
        return m_inner.Allocate(size, alignment);
    }

    template<typename T>
    T* Allocate(usize count = 1) {
        std::lock_guard lock(m_mutex);
        return m_inner.Allocate<T>(count);
    }

    usize GetRemainingBytes() const { return m_inner.GetRemainingBytes(); }
    usize GetAllocatedBytes() const { return m_inner.GetAllocatedBytes(); }
    usize GetCapacity() const { return m_inner.GetCapacity(); }

private:
    FrameAllocator m_inner;
    std::mutex m_mutex;
};

} // namespace nge::memory
