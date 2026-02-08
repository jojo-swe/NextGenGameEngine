#pragma once

#include "engine/core/memory/allocator.h"
#include "engine/core/assert.h"

namespace nge {

// ─── Linear (Bump) Allocator ──────────────────────────────────────────────
// O(1) allocation, bulk reset. Ideal for per-frame temporary data.
class LinearAllocator final : public IAllocator {
public:
    LinearAllocator(void* buffer, usize capacity, const char* name = "Linear")
        : m_buffer(static_cast<byte*>(buffer))
        , m_capacity(capacity)
        , m_offset(0)
        , m_name(name)
    {
        NGE_ASSERT(buffer != nullptr);
        NGE_ASSERT(capacity > 0);
    }

    [[nodiscard]] void* Allocate(usize size, usize alignment = alignof(std::max_align_t)) override {
        usize aligned = AlignUp(m_offset, alignment);
        if (aligned + size > m_capacity) {
            return nullptr; // Out of memory
        }
        void* ptr = m_buffer + aligned;
        m_offset = aligned + size;
        m_peakUsage = m_offset > m_peakUsage ? m_offset : m_peakUsage;
        return ptr;
    }

    void Free(void* /*ptr*/) override {
        // Linear allocator does not support individual frees
    }

    void Reset() override {
        m_offset = 0;
    }

    usize GetAllocatedSize() const override { return m_offset; }
    usize GetCapacity() const { return m_capacity; }
    usize GetPeakUsage() const { return m_peakUsage; }
    usize GetFreeSpace() const { return m_capacity - m_offset; }
    const char* GetName() const override { return m_name; }

private:
    byte*       m_buffer;
    usize       m_capacity;
    usize       m_offset;
    usize       m_peakUsage = 0;
    const char* m_name;
};

} // namespace nge
