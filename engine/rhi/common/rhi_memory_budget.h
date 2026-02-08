#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <mutex>

namespace nge::rhi {

// ─── GPU Memory Budget Tracker ───────────────────────────────────────────
// Monitors VRAM usage against device limits using VK_EXT_memory_budget.
// Provides per-heap tracking, budget warnings, and allocation advice.
//
// Vulkan exposes multiple memory heaps (device-local, host-visible, etc.)
// each with a budget that may change dynamically (e.g., other apps using VRAM).

struct MemoryHeapInfo {
    u32  heapIndex;
    u64  totalSize;          // Physical heap size
    u64  budget;             // Current budget (may be < totalSize)
    u64  usage;              // Current allocation usage
    bool deviceLocal;
    bool hostVisible;
    f32  usagePercent;       // usage / budget * 100
};

struct MemoryBudgetConfig {
    f32 warningThreshold = 0.85f;   // Warn at 85% usage
    f32 criticalThreshold = 0.95f;  // Critical at 95%
    u32 pollIntervalFrames = 30;    // Re-query budget every N frames
};

struct MemoryBudgetStats {
    u64 totalDeviceLocalBudget;
    u64 totalDeviceLocalUsage;
    u64 totalHostVisibleBudget;
    u64 totalHostVisibleUsage;
    f32 deviceLocalUsagePercent;
    f32 hostVisibleUsagePercent;
    u32 heapCount;
    bool budgetWarning;
    bool budgetCritical;
};

enum class AllocationAdvice : u8 {
    Ok,              // Plenty of budget remaining
    Cautious,        // Approaching budget limit
    EvictUnused,     // Should evict unused resources
    DenyAllocation,  // Over budget, deny new allocations
};

class MemoryBudgetTracker {
public:
    bool Init(IDevice* device, const MemoryBudgetConfig& config = {});
    void Shutdown();

    // Per-frame update (polls budget at configured interval)
    void Update(u64 frameNumber);

    // Force immediate budget query
    void PollBudget();

    // Track an allocation (call when allocating GPU memory)
    void TrackAllocation(u32 heapIndex, u64 sizeBytes);

    // Track a free (call when freeing GPU memory)
    void TrackFree(u32 heapIndex, u64 sizeBytes);

    // Get allocation advice for a potential allocation
    AllocationAdvice GetAdvice(u32 heapIndex, u64 requestedBytes) const;

    // Get advice for device-local allocation (finds best heap)
    AllocationAdvice GetDeviceLocalAdvice(u64 requestedBytes) const;

    // Query per-heap info
    const std::vector<MemoryHeapInfo>& GetHeapInfo() const { return m_heaps; }

    // Aggregate stats
    MemoryBudgetStats GetStats() const;

    // Get available budget for device-local memory
    u64 GetAvailableDeviceLocal() const;

    // Get available budget for host-visible memory
    u64 GetAvailableHostVisible() const;

    static const char* AdviceName(AllocationAdvice advice);

private:
    IDevice* m_device = nullptr;
    MemoryBudgetConfig m_config;
    std::vector<MemoryHeapInfo> m_heaps;
    u64 m_lastPollFrame = 0;
    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
