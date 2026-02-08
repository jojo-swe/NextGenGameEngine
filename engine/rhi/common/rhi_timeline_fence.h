#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <mutex>
#include <vector>
#include <functional>

namespace nge::rhi {

// ─── GPU Timeline Fence ──────────────────────────────────────────────────
// Wraps Vulkan timeline semaphores for CPU ↔ GPU synchronization.
// Each fence has a monotonically increasing u64 value.
//
// Usage:
//   fence.Init(device);
//   u64 val = fence.Signal(cmd);       // GPU signals after cmd completes
//   fence.WaitCPU(val);                // CPU blocks until GPU reaches val
//   bool done = fence.IsComplete(val); // Non-blocking check

class TimelineFence {
public:
    bool Init(IDevice* device, u64 initialValue = 0);
    void Shutdown();

    // Signal from GPU command list — returns the signaled value
    u64 Signal(ICommandList* cmd);

    // Signal a specific value from GPU
    void Signal(ICommandList* cmd, u64 value);

    // CPU wait until the fence reaches at least `value`
    void WaitCPU(u64 value, u64 timeoutNs = UINT64_MAX);

    // Non-blocking check
    bool IsComplete(u64 value) const;

    // Get the last signaled value (may lag behind GPU)
    u64 GetCompletedValue() const;

    // Get the next value that will be signaled
    u64 GetPendingValue() const { return m_nextValue; }

    // Get the underlying semaphore handle (VkSemaphore as u64)
    u64 GetSemaphoreHandle() const { return m_semaphore; }

private:
    IDevice* m_device = nullptr;
    u64      m_semaphore = 0;  // VkSemaphore cast to u64
    u64      m_nextValue = 1;
    mutable std::mutex m_mutex;
};

// ─── Frame Fence ─────────────────────────────────────────────────────────
// Tracks per-frame GPU completion for N frames in flight.
// Ensures the CPU doesn't overwrite resources still in use by the GPU.

class FrameFence {
public:
    bool Init(IDevice* device, u32 framesInFlight = 3);
    void Shutdown();

    // Call at start of frame — waits if GPU is too far behind
    void BeginFrame();

    // Call at end of frame — signals GPU completion
    void EndFrame(ICommandList* cmd);

    // Wait for all frames to complete
    void WaitAll();

    // Get the current frame index (0-based, wrapping)
    u32 GetFrameIndex() const { return m_frameIndex; }

    // Get how many frames the GPU is behind the CPU
    u32 GetGPULag() const;

    TimelineFence& GetFence() { return m_fence; }

private:
    TimelineFence m_fence;
    u32 m_framesInFlight = 3;
    u32 m_frameIndex = 0;
    u64 m_frameCount = 0;
};

// ─── Deletion Queue ──────────────────────────────────────────────────────
// Defers GPU resource destruction until the GPU is done using them.
// Uses timeline fence values to know when it's safe to destroy.

class DeletionQueue {
public:
    using DeleteFunc = std::function<void()>;

    void Init(TimelineFence* fence);

    // Queue a deletion to happen after the GPU reaches `afterValue`
    void Enqueue(DeleteFunc func, u64 afterValue);

    // Enqueue for deletion after the current pending fence value
    void Enqueue(DeleteFunc func);

    // Process completed deletions
    void Flush();

    // Force-flush all pending deletions (call on shutdown)
    void FlushAll();

    u32 GetPendingCount() const { return static_cast<u32>(m_pending.size()); }

private:
    struct PendingDelete {
        DeleteFunc func;
        u64 afterValue;
    };

    TimelineFence* m_fence = nullptr;
    std::vector<PendingDelete> m_pending;
};

} // namespace nge::rhi
