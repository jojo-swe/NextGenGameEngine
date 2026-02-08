#pragma once

#include "engine/core/memory/allocator.h"
#include "engine/core/assert.h"

namespace nge {

// ─── Stack Allocator ──────────────────────────────────────────────────────
// LIFO allocation with markers. Ideal for nested scoped temporaries.
class StackAllocator final : public IAllocator {
public:
    using Marker = usize;

    StackAllocator(void* buffer, usize capacity, const char* name = "Stack")
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
            return nullptr;
        }
        void* ptr = m_buffer + aligned;
        m_offset = aligned + size;
        return ptr;
    }

    void Free(void* /*ptr*/) override {
        // Use FreeToMarker() instead
    }

    void Reset() override {
        m_offset = 0;
    }

    Marker GetMarker() const { return m_offset; }

    void FreeToMarker(Marker marker) {
        NGE_ASSERT(marker <= m_offset);
        m_offset = marker;
    }

    usize GetAllocatedSize() const override { return m_offset; }
    const char* GetName() const override { return m_name; }

private:
    byte*       m_buffer;
    usize       m_capacity;
    usize       m_offset;
    const char* m_name;
};

// RAII scope guard for stack allocator
class StackScope {
public:
    explicit StackScope(StackAllocator& alloc)
        : m_allocator(alloc), m_marker(alloc.GetMarker()) {}

    ~StackScope() { m_allocator.FreeToMarker(m_marker); }

    StackScope(const StackScope&) = delete;
    StackScope& operator=(const StackScope&) = delete;

private:
    StackAllocator&         m_allocator;
    StackAllocator::Marker  m_marker;
};

} // namespace nge
