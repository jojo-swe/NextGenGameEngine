#pragma once

#include "engine/core/memory/allocator.h"
#include "engine/core/assert.h"
#include <cstring>
#include <bit>

namespace nge {

// ─── TLSF (Two-Level Segregated Fit) Allocator ───────────────────────────
// O(1) alloc/free, low fragmentation general-purpose heap.
//
// First level: log2 of block size (FL_INDEX_COUNT buckets)
// Second level: linear subdivision within each first-level range (SL_INDEX_COUNT per FL)
//
// Based on: http://www.gii.upv.es/tlsf/
class TLSFAllocator final : public IAllocator {
    static constexpr u32 FL_INDEX_MAX   = 30;    // Supports up to 1 GB
    static constexpr u32 SL_INDEX_LOG2  = 5;
    static constexpr u32 SL_INDEX_COUNT = 1u << SL_INDEX_LOG2; // 32 sub-divisions
    static constexpr u32 FL_INDEX_SHIFT = SL_INDEX_LOG2;
    static constexpr u32 FL_INDEX_COUNT = FL_INDEX_MAX - FL_INDEX_SHIFT + 1;
    static constexpr usize MIN_BLOCK_SIZE = 32;
    static constexpr usize BLOCK_HEADER_SIZE = sizeof(usize) * 2; // size + prev_phys

    struct BlockHeader {
        // Bit 0 of size: free flag (1 = free, 0 = used)
        // Bit 1 of size: prev-phys-free flag
        usize size;
        usize prevPhysBlock; // offset to previous physical block (0 if first)

        // Free blocks have these additional fields overlaid on user data
        BlockHeader* nextFree;
        BlockHeader* prevFree;

        bool IsFree() const { return (size & 1) != 0; }
        bool IsPrevFree() const { return (size & 2) != 0; }
        usize GetSize() const { return size & ~(usize(3)); }

        void SetSize(usize s) { size = s | (size & 3); }
        void SetFree() { size |= 1; }
        void SetUsed() { size &= ~usize(1); }
        void SetPrevFree() { size |= 2; }
        void SetPrevUsed() { size &= ~usize(2); }

        void* ToUserPtr() { return reinterpret_cast<byte*>(this) + BLOCK_HEADER_SIZE; }
        static BlockHeader* FromUserPtr(void* ptr) {
            return reinterpret_cast<BlockHeader*>(static_cast<byte*>(ptr) - BLOCK_HEADER_SIZE);
        }
    };

public:
    TLSFAllocator(void* buffer, usize capacity, const char* name = "TLSF")
        : m_buffer(static_cast<byte*>(buffer))
        , m_capacity(capacity)
        , m_name(name)
    {
        NGE_ASSERT(buffer != nullptr);
        NGE_ASSERT(capacity >= MIN_BLOCK_SIZE * 4);
        Reset();
    }

    [[nodiscard]] void* Allocate(usize size, usize alignment = alignof(std::max_align_t)) override {
        usize adjustedSize = AdjustSize(size, alignment);
        if (adjustedSize == 0) return nullptr;

        u32 fl, sl;
        BlockHeader* block = FindSuitableBlock(adjustedSize, fl, sl);
        if (!block) return nullptr;

        RemoveFreeBlock(block, fl, sl);

        // Split if remainder is large enough
        usize remain = block->GetSize() - adjustedSize;
        if (remain >= MIN_BLOCK_SIZE + BLOCK_HEADER_SIZE) {
            BlockHeader* split = reinterpret_cast<BlockHeader*>(
                reinterpret_cast<byte*>(block) + BLOCK_HEADER_SIZE + adjustedSize);
            split->SetSize(remain - BLOCK_HEADER_SIZE);
            split->SetFree();
            block->SetSize(adjustedSize);

            InsertFreeBlock(split);
        }

        block->SetUsed();
        m_usedSize += block->GetSize();

        return block->ToUserPtr();
    }

    void Free(void* ptr) override {
        if (!ptr) return;

        BlockHeader* block = BlockHeader::FromUserPtr(ptr);
        NGE_ASSERT(!block->IsFree());

        m_usedSize -= block->GetSize();
        block->SetFree();

        // Coalesce with next physical block
        BlockHeader* next = GetNextPhysBlock(block);
        if (next && next->IsFree()) {
            RemoveFreeBlockByHeader(next);
            block->SetSize(block->GetSize() + BLOCK_HEADER_SIZE + next->GetSize());
        }

        // Coalesce with previous physical block
        if (block->IsPrevFree() && block->prevPhysBlock != 0) {
            BlockHeader* prev = reinterpret_cast<BlockHeader*>(
                reinterpret_cast<byte*>(block) - block->prevPhysBlock);
            if (prev->IsFree()) {
                RemoveFreeBlockByHeader(prev);
                prev->SetSize(prev->GetSize() + BLOCK_HEADER_SIZE + block->GetSize());
                block = prev;
            }
        }

        InsertFreeBlock(block);

        // Mark next block's prev-free flag
        BlockHeader* nextAfter = GetNextPhysBlock(block);
        if (nextAfter) {
            nextAfter->SetPrevFree();
            nextAfter->prevPhysBlock = static_cast<usize>(
                reinterpret_cast<byte*>(nextAfter) - reinterpret_cast<byte*>(block));
        }
    }

    void Reset() override {
        m_usedSize = 0;
        std::memset(&m_flBitmapTop, 0, sizeof(m_flBitmapTop));
        std::memset(m_slBitmap, 0, sizeof(m_slBitmap));
        std::memset(m_freeBlocks, 0, sizeof(m_freeBlocks));
        m_flBitmapTop = 0;

        // Create one big free block spanning the entire buffer
        BlockHeader* block = reinterpret_cast<BlockHeader*>(m_buffer);
        block->size = 0;
        block->prevPhysBlock = 0;
        block->SetSize(m_capacity - BLOCK_HEADER_SIZE);
        block->SetFree();
        block->nextFree = nullptr;
        block->prevFree = nullptr;

        InsertFreeBlock(block);
    }

    usize GetAllocatedSize() const override { return m_usedSize; }
    const char* GetName() const override { return m_name; }

private:
    static void MappingInsert(usize size, u32& fl, u32& sl) {
        if (size < MIN_BLOCK_SIZE) {
            fl = 0;
            sl = static_cast<u32>(size / (MIN_BLOCK_SIZE / SL_INDEX_COUNT));
        } else {
            fl = static_cast<u32>(std::bit_width(size)) - 1;
            sl = static_cast<u32>((size >> (fl - SL_INDEX_LOG2)) ^ (1u << SL_INDEX_LOG2));
            fl -= FL_INDEX_SHIFT;
        }
    }

    static void MappingSearch(usize size, u32& fl, u32& sl) {
        // Round up to next block size for search
        usize roundUp = size + (1u << (std::bit_width(size) - 1 - SL_INDEX_LOG2)) - 1;
        MappingInsert(roundUp, fl, sl);
    }

    usize AdjustSize(usize size, usize alignment) const {
        usize adjusted = size < MIN_BLOCK_SIZE ? MIN_BLOCK_SIZE : AlignUp(size, alignment);
        return adjusted;
    }

    BlockHeader* FindSuitableBlock(usize size, u32& fl, u32& sl) {
        MappingSearch(size, fl, sl);
        if (fl >= FL_INDEX_COUNT) return nullptr;

        // Search in current sl bucket
        u32 slMap = m_slBitmap[fl] & (~0u << sl);
        if (slMap == 0) {
            // Search in higher fl
            u32 flMap = m_flBitmapTop & (~0u << (fl + 1));
            if (flMap == 0) return nullptr;
            fl = static_cast<u32>(std::countr_zero(flMap));
            slMap = m_slBitmap[fl];
        }
        sl = static_cast<u32>(std::countr_zero(slMap));
        return m_freeBlocks[fl][sl];
    }

    void InsertFreeBlock(BlockHeader* block) {
        u32 fl, sl;
        MappingInsert(block->GetSize(), fl, sl);
        if (fl >= FL_INDEX_COUNT || sl >= SL_INDEX_COUNT) return;

        BlockHeader* head = m_freeBlocks[fl][sl];
        block->nextFree = head;
        block->prevFree = nullptr;
        if (head) head->prevFree = block;
        m_freeBlocks[fl][sl] = block;

        m_flBitmapTop |= (1u << fl);
        m_slBitmap[fl] |= (1u << sl);
    }

    void RemoveFreeBlock(BlockHeader* block, u32 fl, u32 sl) {
        if (block->prevFree) block->prevFree->nextFree = block->nextFree;
        else m_freeBlocks[fl][sl] = block->nextFree;
        if (block->nextFree) block->nextFree->prevFree = block->prevFree;

        if (m_freeBlocks[fl][sl] == nullptr) {
            m_slBitmap[fl] &= ~(1u << sl);
            if (m_slBitmap[fl] == 0) m_flBitmapTop &= ~(1u << fl);
        }
    }

    void RemoveFreeBlockByHeader(BlockHeader* block) {
        u32 fl, sl;
        MappingInsert(block->GetSize(), fl, sl);
        RemoveFreeBlock(block, fl, sl);
    }

    BlockHeader* GetNextPhysBlock(BlockHeader* block) {
        byte* next = reinterpret_cast<byte*>(block) + BLOCK_HEADER_SIZE + block->GetSize();
        if (next >= m_buffer + m_capacity) return nullptr;
        return reinterpret_cast<BlockHeader*>(next);
    }

    byte*       m_buffer;
    usize       m_capacity;
    usize       m_usedSize    = 0;
    const char* m_name;

    u32          m_flBitmapTop = 0;
    u32          m_slBitmap[FL_INDEX_COUNT] = {};
    BlockHeader* m_freeBlocks[FL_INDEX_COUNT][SL_INDEX_COUNT] = {};
};

} // namespace nge
