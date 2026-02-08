#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <queue>
#include <mutex>

namespace nge::rhi {

// ─── GPU Timeline Semaphore Pool ─────────────────────────────────────────
// Reusable pool of VkSemaphore (timeline type) for cross-queue
// synchronization. Avoids creating/destroying semaphores per frame.
//
// Timeline semaphores are monotonically incrementing counters that allow
// fine-grained GPU-GPU and CPU-GPU synchronization without binary
// signal/wait pairing.
//
// Used by:
//   - Render graph async compute queue sync
//   - Transfer queue completion signaling
//   - Multi-queue work graph scheduling

struct TimelineSemaphoreInfo {
    u64         handle;       // VkSemaphore
    u64         currentValue; // Last signaled value
    std::string debugName;
    bool        inUse;
};

struct TimelineSemaphorePoolConfig {
    u32 initialPoolSize = 16;
    u32 maxPoolSize = 128;
    bool allowGrowth = true;
};

struct TimelineSemaphorePoolStats {
    u32 totalSemaphores;
    u32 inUseSemaphores;
    u32 availableSemaphores;
    u32 growthEvents;
    u64 highestSignaledValue;
};

class TimelineSemaphorePool {
public:
    bool Init(IDevice* device, const TimelineSemaphorePoolConfig& config = {});
    void Shutdown();

    // Acquire a timeline semaphore from the pool
    u32 Acquire(const std::string& debugName = "");

    // Release a semaphore back to the pool
    void Release(u32 semaphoreId);

    // Get the VkSemaphore handle
    u64 GetHandle(u32 semaphoreId) const;

    // Signal a new value (GPU-side, returns value to signal)
    u64 GetNextSignalValue(u32 semaphoreId);

    // Get the current (last signaled) value
    u64 GetCurrentValue(u32 semaphoreId) const;

    // CPU-side wait for a semaphore to reach a value
    bool CpuWait(u32 semaphoreId, u64 value, u64 timeoutNs = 5000000000ULL);

    // CPU-side signal (for CPU→GPU sync)
    void CpuSignal(u32 semaphoreId, u64 value);

    // Check if a value has been reached (non-blocking)
    bool HasReached(u32 semaphoreId, u64 value) const;

    // Reset all semaphores (usually at engine shutdown or reset)
    void ResetAll();

    TimelineSemaphorePoolStats GetStats() const;

private:
    void GrowPool(u32 count);

    IDevice* m_device = nullptr;
    TimelineSemaphorePoolConfig m_config;
    std::vector<TimelineSemaphoreInfo> m_semaphores;
    std::queue<u32> m_available;
    u32 m_growthEvents = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
