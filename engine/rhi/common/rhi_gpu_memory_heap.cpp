#include "engine/rhi/common/rhi_gpu_memory_heap.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool GPUMemoryHeapManager::Init(const HeapManagerConfig& config) {
    m_config = config;
    m_heaps.clear();
    m_heaps.reserve(config.maxHeaps);
    m_allocations.clear();
    m_nextAllocId = 1;

    NGE_LOG_INFO("GPU memory heap manager initialized: maxHeaps={}, trackFrag={}, respectBudget={}",
                 config.maxHeaps, config.trackFragmentation, config.respectBudget);
    return true;
}

void GPUMemoryHeapManager::Shutdown() {
    if (!m_allocations.empty()) {
        NGE_LOG_WARN("GPU memory heap manager: {} allocations still alive at shutdown", m_allocations.size());
    }
    m_heaps.clear();
    m_allocations.clear();
}

u32 GPUMemoryHeapManager::RegisterHeap(HeapType type, u64 totalSize, u64 budgetSize,
                                         const std::string& name) {
    std::lock_guard lock(m_mutex);

    if (m_heaps.size() >= m_config.maxHeaps) {
        return UINT32_MAX;
    }

    u32 index = static_cast<u32>(m_heaps.size());
    m_heaps.emplace_back(HeapInfo{
        index,        // heapIndex
        type,         // type
        totalSize,    // totalSize
        0,            // usedSize
        budgetSize > 0 ? budgetSize : totalSize, // budgetSize
        0,            // peakUsed
        0,            // allocationCount
        0,            // peakAllocations
        0.0f,         // fragmentation
        name          // debugName
    });

    return index;
}

void GPUMemoryHeapManager::UpdateBudget(u32 heapIndex, u64 newBudget) {
    std::lock_guard lock(m_mutex);

    if (heapIndex >= m_heaps.size()) return;

    m_heaps[heapIndex].budgetSize = newBudget;

    if (m_config.respectBudget) {
        float usage = static_cast<float>(m_heaps[heapIndex].usedSize) / static_cast<float>(newBudget);
        if (usage > m_config.budgetWarningThreshold) {
            NGE_LOG_WARN("Heap {} '{}' at {:.1f}% of budget ({}/{} MB)",
                         heapIndex, m_heaps[heapIndex].debugName, usage * 100.0f,
                         m_heaps[heapIndex].usedSize / (1024 * 1024),
                         newBudget / (1024 * 1024));
        }
    }
}

u64 GPUMemoryHeapManager::RecordAllocation(u32 heapIndex, u64 size, u64 alignment,
                                             AllocUsage usage, const std::string& name) {
    std::lock_guard lock(m_mutex);

    if (heapIndex >= m_heaps.size()) return 0;

    auto& heap = m_heaps[heapIndex];

    if (m_config.respectBudget && heap.usedSize + size > heap.budgetSize) {
        NGE_LOG_WARN("Heap {} allocation of {} bytes would exceed budget ({}/{})",
                     heapIndex, size, heap.usedSize + size, heap.budgetSize);
    }

    u64 id = m_nextAllocId++;

    HeapAllocation alloc;
    alloc.id = id;
    alloc.heapIndex = heapIndex;
    alloc.offset = heap.usedSize; // Simplified: linear allocation tracking
    alloc.size = size;
    alloc.alignment = alignment;
    alloc.usage = usage;
    alloc.debugName = name;

    m_allocations[id] = std::move(alloc);

    heap.usedSize += size;
    heap.allocationCount++;
    if (heap.usedSize > heap.peakUsed) heap.peakUsed = heap.usedSize;
    if (heap.allocationCount > heap.peakAllocations) heap.peakAllocations = heap.allocationCount;

    if (m_config.trackFragmentation) {
        UpdateFragmentation(heapIndex);
    }

    return id;
}

void GPUMemoryHeapManager::RecordFree(u64 allocationId) {
    std::lock_guard lock(m_mutex);

    auto it = m_allocations.find(allocationId);
    if (it == m_allocations.end()) return;

    u32 heapIndex = it->second.heapIndex;
    u64 size = it->second.size;

    if (heapIndex < m_heaps.size()) {
        auto& heap = m_heaps[heapIndex];
        heap.usedSize = heap.usedSize > size ? heap.usedSize - size : 0;
        heap.allocationCount--;

        if (m_config.trackFragmentation) {
            UpdateFragmentation(heapIndex);
        }
    }

    m_allocations.erase(it);
}

u32 GPUMemoryHeapManager::FindBestHeap(HeapType preferredType, u64 size, [[maybe_unused]] AllocUsage usage) const {
    std::lock_guard lock(m_mutex);

    u32 bestHeap = UINT32_MAX;
    u64 bestAvailable = 0;

    for (u32 i = 0; i < static_cast<u32>(m_heaps.size()); ++i) {
        const auto& heap = m_heaps[i];

        if (heap.type != preferredType) continue;

        u64 available = heap.budgetSize > heap.usedSize ? heap.budgetSize - heap.usedSize : 0;
        if (available < size) continue;

        // Prefer heap with most available space
        if (available > bestAvailable) {
            bestAvailable = available;
            bestHeap = i;
        }
    }

    // Fallback: try any heap with enough room
    if (bestHeap == UINT32_MAX) {
        for (u32 i = 0; i < static_cast<u32>(m_heaps.size()); ++i) {
            u64 available = m_heaps[i].totalSize > m_heaps[i].usedSize
                            ? m_heaps[i].totalSize - m_heaps[i].usedSize : 0;
            if (available >= size) {
                bestHeap = i;
                break;
            }
        }
    }

    return bestHeap;
}

bool GPUMemoryHeapManager::HasRoom(u32 heapIndex, u64 size) const {
    std::lock_guard lock(m_mutex);

    if (heapIndex >= m_heaps.size()) return false;

    const auto& heap = m_heaps[heapIndex];
    u64 limit = m_config.respectBudget ? heap.budgetSize : heap.totalSize;
    return (heap.usedSize + size) <= limit;
}

const HeapInfo* GPUMemoryHeapManager::GetHeapInfo(u32 heapIndex) const {
    std::lock_guard lock(m_mutex);

    if (heapIndex >= m_heaps.size()) return nullptr;
    return &m_heaps[heapIndex];
}

const HeapAllocation* GPUMemoryHeapManager::GetAllocation(u64 allocationId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_allocations.find(allocationId);
    if (it == m_allocations.end()) return nullptr;
    return &it->second;
}

std::vector<u32> GPUMemoryHeapManager::GetDefragCandidates(float minFragmentation) const {
    std::lock_guard lock(m_mutex);

    std::vector<u32> candidates;
    for (u32 i = 0; i < static_cast<u32>(m_heaps.size()); ++i) {
        if (m_heaps[i].fragmentation >= minFragmentation) {
            candidates.push_back(i);
        }
    }
    return candidates;
}

std::vector<u64> GPUMemoryHeapManager::GetHeapAllocations(u32 heapIndex) const {
    std::lock_guard lock(m_mutex);

    std::vector<u64> result;
    for (const auto& [id, alloc] : m_allocations) {
        if (alloc.heapIndex == heapIndex) {
            result.push_back(id);
        }
    }
    return result;
}

u32 GPUMemoryHeapManager::GetHeapCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_heaps.size());
}

u32 GPUMemoryHeapManager::GetTotalAllocationCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_allocations.size());
}

void GPUMemoryHeapManager::Reset() {
    std::lock_guard lock(m_mutex);
    m_heaps.clear();
    m_allocations.clear();
    m_nextAllocId = 1;
}

HeapManagerStats GPUMemoryHeapManager::GetStats() const {
    std::lock_guard lock(m_mutex);

    HeapManagerStats stats{};
    stats.totalHeaps = static_cast<u32>(m_heaps.size());
    stats.totalAllocations = static_cast<u32>(m_allocations.size());

    u64 totalUsed = 0, totalCapacity = 0, totalBudget = 0;
    float worstFrag = 0.0f;
    u32 overWarning = 0;

    for (const auto& heap : m_heaps) {
        totalUsed += heap.usedSize;
        totalCapacity += heap.totalSize;
        totalBudget += heap.budgetSize;
        if (heap.fragmentation > worstFrag) worstFrag = heap.fragmentation;

        if (heap.budgetSize > 0) {
            float usage = static_cast<float>(heap.usedSize) / static_cast<float>(heap.budgetSize);
            if (usage > m_config.budgetWarningThreshold) overWarning++;
        }
    }

    stats.totalUsed = totalUsed;
    stats.totalCapacity = totalCapacity;
    stats.totalBudget = totalBudget;
    stats.overallUtilization = totalCapacity > 0 ? static_cast<float>(totalUsed) / static_cast<float>(totalCapacity) : 0.0f;
    stats.worstFragmentation = worstFrag;
    stats.heapsOverBudgetWarning = overWarning;

    return stats;
}

void GPUMemoryHeapManager::UpdateFragmentation(u32 heapIndex) {
    if (heapIndex >= m_heaps.size()) return;

    auto& heap = m_heaps[heapIndex];

    // Simple fragmentation estimate based on allocation count vs used ratio
    // Higher allocation count with lower utilization = more fragmentation
    if (heap.allocationCount == 0 || heap.totalSize == 0) {
        heap.fragmentation = 0.0f;
        return;
    }

    float utilization = static_cast<float>(heap.usedSize) / static_cast<float>(heap.totalSize);
    float allocDensity = static_cast<float>(heap.allocationCount) / static_cast<float>(m_config.maxAllocationsPerHeap);

    // High alloc count + low utilization = fragmented
    heap.fragmentation = std::min(1.0f, allocDensity * (1.0f - utilization) * 4.0f);
}

} // namespace nge::rhi
