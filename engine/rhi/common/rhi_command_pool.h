#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <mutex>

namespace nge::rhi {

// ─── Command Pool Manager ────────────────────────────────────────────────
// Per-frame command buffer recycling. Each frame gets its own pool
// so that buffers are only reset when the GPU is done with them.
//
// Triple-buffered: one pool recording, one executing, one waiting.
// Thread-safe: each worker thread can request its own command buffer
// from the current frame's pool.

class CommandPoolManager {
public:
    bool Init(IDevice* device, u32 framesInFlight = 3, QueueType queueType = QueueType::Graphics);
    void Shutdown();

    // Call at frame start — resets the oldest pool for reuse
    void BeginFrame(u32 frameIndex);

    // Get a command buffer from the current frame's pool
    ICommandList* GetCommandList();

    // Get a secondary command buffer (for parallel recording)
    ICommandList* GetSecondaryCommandList();

    // Submit all command buffers from the current frame's pool
    void SubmitAll();

    // Reset a specific frame's pool (called when GPU confirms completion)
    void ResetPool(u32 frameIndex);

    // Stats
    u32 GetActiveCommandListCount() const;
    u32 GetPoolCount() const { return static_cast<u32>(m_pools.size()); }

private:
    struct FramePool {
        u64 poolHandle = 0;    // VkCommandPool
        std::vector<ICommandList*> primaryBuffers;
        std::vector<ICommandList*> secondaryBuffers;
        u32 primaryUsed = 0;
        u32 secondaryUsed = 0;
        bool recording = false;
    };

    IDevice* m_device = nullptr;
    QueueType m_queueType = QueueType::Graphics;
    u32 m_framesInFlight = 3;
    u32 m_currentFrame = 0;

    std::vector<FramePool> m_pools;
    std::mutex m_mutex;

    static constexpr u32 INITIAL_BUFFERS_PER_POOL = 8;
};

// ─── Per-Thread Command Buffer ───────────────────────────────────────────
// For parallel command recording from job system workers.
// Each thread gets its own command pool to avoid synchronization.

class ThreadLocalCommandPool {
public:
    bool Init(IDevice* device, QueueType queueType = QueueType::Graphics);
    void Shutdown();

    // Get command buffer for current thread
    ICommandList* GetCommandList();

    // Reset (call when GPU is done with all buffers from this pool)
    void Reset();

private:
    IDevice* m_device = nullptr;
    u64 m_poolHandle = 0; // VkCommandPool
    std::vector<ICommandList*> m_buffers;
    u32 m_used = 0;
};

} // namespace nge::rhi
