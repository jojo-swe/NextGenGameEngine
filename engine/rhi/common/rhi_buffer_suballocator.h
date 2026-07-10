#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <mutex>
#include <string>

namespace nge::rhi {

// ─── GPU Buffer Suballocator ─────────────────────────────────────────────
// Sub-allocates regions from large GPU buffer heaps. Reduces the number
// of VkBuffer objects and VkDeviceMemory allocations by packing multiple
// logical buffers into shared physical buffers.
//
// Uses a free-list allocator with first-fit strategy and coalescing of
// adjacent free blocks. Supports alignment requirements.

struct SubAllocation {
    BufferHandle buffer{};   // Parent buffer
    u64          offset = 0;      // Byte offset within parent buffer
    u64          size = 0;        // Allocated size (may be > requested due to alignment)
    u32          heapIndex = 0;   // Which heap this came from
    bool         valid = false;
};

struct SubAllocatorConfig {
    u64 heapSize = 64 * 1024 * 1024;  // 64 MB per heap
    u32 maxHeaps = 8;
    u32 defaultAlignment = 256;
    BufferUsage usage = BufferUsage::Storage | BufferUsage::TransferDst;
    MemoryUsage memoryUsage = MemoryUsage::GPU_Only;
    const char* debugName = "SubAllocHeap";
};

struct SubAllocatorStats {
    u32 heapCount;
    u64 totalCapacity;
    u64 totalAllocated;
    u64 totalFree;
    u32 allocationCount;
    u32 freeBlockCount;
    f32 fragmentation;       // 1 - (largest_free / total_free)
};

class BufferSubAllocator {
public:
    bool Init(IDevice* device, const SubAllocatorConfig& config = {});
    void Shutdown();

    // Allocate a region from a heap
    SubAllocation Allocate(u64 size, u32 alignment = 0);

    // Free a previously allocated region
    void Free(const SubAllocation& alloc);

    // Defragment (coalesce adjacent free blocks)
    u32 Coalesce();

    // Stats
    SubAllocatorStats GetStats() const;
    const SubAllocatorConfig& GetConfig() const { return m_config; }

private:
    struct FreeBlock {
        u64 offset;
        u64 size;
    };

    struct Heap {
        BufferHandle buffer;
        u64          capacity;
        u64          allocated;
        std::vector<FreeBlock> freeBlocks;
    };

    SubAllocation AllocateFromHeap(u32 heapIndex, u64 size, u32 alignment);
    u32 CreateHeap();
    void CoalesceHeap(Heap& heap);

    IDevice* m_device = nullptr;
    SubAllocatorConfig m_config;
    std::vector<Heap> m_heaps;
    u32 m_allocationCount = 0;
    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
