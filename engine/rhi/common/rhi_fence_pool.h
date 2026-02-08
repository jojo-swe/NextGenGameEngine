#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <mutex>

namespace nge::rhi {

// ─── GPU Fence Pool ──────────────────────────────────────────────────────
// Recyclable fence objects for command buffer submission synchronization.
// Avoids creating/destroying VkFence objects every frame by maintaining
// a pool of reusable fences.
//
// Usage:
//   auto fence = pool.Acquire();
//   vkQueueSubmit(..., fence);
//   // Later:
//   pool.WaitAndRecycle(fence);

struct FenceHandle {
    u64 handle = 0;
    u64 id = 0;
    bool IsValid() const { return handle != 0; }
};

class FencePool {
public:
    bool Init(IDevice* device, u32 initialCount = 16);
    void Shutdown();

    // Acquire an unsignaled fence
    FenceHandle Acquire();

    // Wait for a fence to be signaled, then reset and return to pool
    void WaitAndRecycle(FenceHandle fence);

    // Check if a fence is signaled (non-blocking)
    bool IsSignaled(FenceHandle fence) const;

    // Wait for a fence (blocking)
    void Wait(FenceHandle fence, u64 timeoutNs = UINT64_MAX) const;

    // Recycle all signaled fences back to the pool (non-blocking sweep)
    u32 RecycleSignaled();

    // Stats
    u32 GetActiveCount() const { return m_activeCount; }
    u32 GetPooledCount() const;
    u32 GetTotalCreated() const { return m_totalCreated; }

private:
    FenceHandle CreateFence();

    IDevice* m_device = nullptr;

    std::vector<FenceHandle> m_available;  // Ready to acquire (reset/unsignaled)
    std::vector<FenceHandle> m_active;     // Currently in-flight
    u32 m_activeCount = 0;
    u32 m_totalCreated = 0;
    u64 m_nextId = 1;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
