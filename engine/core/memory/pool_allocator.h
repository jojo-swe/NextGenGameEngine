#pragma once

#include "engine/core/memory/allocator.h"
#include "engine/core/assert.h"
#include <cstring>

namespace nge {

// ─── Pool Allocator ───────────────────────────────────────────────────────
// O(1) alloc/free for fixed-size objects via embedded free list.
template <usize BlockSize, usize BlockAlign = alignof(std::max_align_t)>
class PoolAllocator final : public IAllocator {
    static_assert(BlockSize >= sizeof(void*), "Block size must be >= pointer size for free list");

    struct FreeNode {
        FreeNode* next;
    };

public:
    PoolAllocator(void* buffer, usize capacity, const char* name = "Pool")
        : m_buffer(static_cast<byte*>(buffer))
        , m_capacity(capacity)
        , m_name(name)
    {
        NGE_ASSERT(buffer != nullptr);
        NGE_ASSERT(capacity > 0);
        Reset();
    }

    [[nodiscard]] void* Allocate(usize size, usize /*alignment*/) override {
        NGE_ASSERT(size <= BlockSize);
        if (!m_freeList) return nullptr;

        FreeNode* node = m_freeList;
        m_freeList = node->next;
        m_allocCount++;
        return node;
    }

    void Free(void* ptr) override {
        if (!ptr) return;
        NGE_ASSERT(ptr >= m_buffer && ptr < m_buffer + m_capacity);

        FreeNode* node = static_cast<FreeNode*>(ptr);
        node->next = m_freeList;
        m_freeList = node;
        m_allocCount--;
    }

    void Reset() override {
        m_freeList = nullptr;
        m_allocCount = 0;

        usize alignedBlockSize = AlignUp(BlockSize, BlockAlign);
        usize blockCount = m_capacity / alignedBlockSize;

        for (usize i = 0; i < blockCount; ++i) {
            byte* blockAddr = m_buffer + i * alignedBlockSize;
            FreeNode* node = reinterpret_cast<FreeNode*>(blockAddr);
            node->next = m_freeList;
            m_freeList = node;
        }
    }

    usize GetAllocatedSize() const override { return m_allocCount * BlockSize; }
    usize GetBlockCount() const { return m_capacity / AlignUp(BlockSize, BlockAlign); }
    usize GetFreeCount() const { return GetBlockCount() - m_allocCount; }
    const char* GetName() const override { return m_name; }

private:
    byte*       m_buffer;
    usize       m_capacity;
    FreeNode*   m_freeList   = nullptr;
    usize       m_allocCount = 0;
    const char* m_name;
};

} // namespace nge
