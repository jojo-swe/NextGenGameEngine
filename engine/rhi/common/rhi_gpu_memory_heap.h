#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Memory Heap Manager ─────────────────────────────────────────────
// Manages GPU memory heaps for sub-allocation. Wraps Vulkan memory types
// and provides typed heap allocation with defragmentation hints, budget
// tracking per heap, and allocation statistics.
//
// Use cases:
//   - Track per-heap usage against VK_EXT_memory_budget limits
//   - Select optimal heap for allocation requests
//   - Monitor fragmentation per heap
//   - Provide defragmentation candidates
//   - Debug visualization of heap occupancy

enum class HeapType : u8 {
    DeviceLocal,           // GPU-only (VRAM)
    HostVisible,           // CPU-writable, GPU-readable (staging)
    HostCached,            // CPU-cached readback
    DeviceLocalHostVisible, // Resizable BAR / SAM
};

enum class AllocUsage : u8 {
    Buffer,
    Image,
    AccelStruct,
    ScratchBuffer,
    Staging,
};

struct HeapInfo {
    u32      heapIndex;
    HeapType type;
    u64      totalSize;       // Total heap capacity
    u64      usedSize;        // Currently allocated
    u64      budgetSize;      // VK_EXT_memory_budget limit
    u64      peakUsed;
    u32      allocationCount;
    u32      peakAllocations;
    float    fragmentation;   // Estimated fragmentation ratio (0..1)
    std::string debugName;
};

struct HeapAllocation {
    u64       id;
    u32       heapIndex;
    u64       offset;
    u64       size;
    u64       alignment;
    AllocUsage usage;
    std::string debugName;
};

struct HeapManagerConfig {
    u32  maxHeaps = 16;
    u32  maxAllocationsPerHeap = 65536;
    bool trackFragmentation = true;
    bool respectBudget = true;
    float budgetWarningThreshold = 0.9f; // Warn at 90% budget
};

struct HeapManagerStats {
    u32 totalHeaps;
    u32 totalAllocations;
    u64 totalUsed;
    u64 totalCapacity;
    u64 totalBudget;
    float overallUtilization;     // totalUsed / totalCapacity
    float worstFragmentation;     // Highest fragmentation across heaps
    u32 heapsOverBudgetWarning;   // Heaps above warning threshold
};

class GPUMemoryHeapManager {
public:
    bool Init(const HeapManagerConfig& config = {});
    void Shutdown();

    // Register a heap (call once per VkMemoryHeap)
    u32 RegisterHeap(HeapType type, u64 totalSize, u64 budgetSize, const std::string& name = "");

    // Update budget from VK_EXT_memory_budget query
    void UpdateBudget(u32 heapIndex, u64 newBudget);

    // Record an allocation on a heap
    u64 RecordAllocation(u32 heapIndex, u64 size, u64 alignment, AllocUsage usage,
                          const std::string& name = "");

    // Record a free
    void RecordFree(u64 allocationId);

    // Find best heap for an allocation
    u32 FindBestHeap(HeapType preferredType, u64 size, AllocUsage usage) const;

    // Check if a heap has room (respecting budget)
    bool HasRoom(u32 heapIndex, u64 size) const;

    // Get heap info
    const HeapInfo* GetHeapInfo(u32 heapIndex) const;

    // Get allocation info
    const HeapAllocation* GetAllocation(u64 allocationId) const;

    // Get heaps that are candidates for defragmentation
    std::vector<u32> GetDefragCandidates(float minFragmentation = 0.3f) const;

    // Get all allocations on a heap (for defrag planning)
    std::vector<u64> GetHeapAllocations(u32 heapIndex) const;

    u32 GetHeapCount() const;
    u32 GetTotalAllocationCount() const;

    void Reset();

    HeapManagerStats GetStats() const;

private:
    void UpdateFragmentation(u32 heapIndex);

    HeapManagerConfig m_config;
    std::vector<HeapInfo> m_heaps;
    std::unordered_map<u64, HeapAllocation> m_allocations;

    u64 m_nextAllocId = 1;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
