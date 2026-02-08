#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <mutex>

namespace nge::rhi {

// ─── GPU Command Buffer Ring Allocator ───────────────────────────────────
// Per-frame command pool recycling for triple-buffered rendering.
// Each frame gets its own command pool that is reset at the start of
// the next cycle. Avoids per-frame command pool creation overhead.
//
// Usage:
//   ring.BeginFrame(frameIndex);
//   auto* cmd = ring.Allocate(QueueType::Graphics);
//   // record commands ...
//   ring.EndFrame();

struct CommandPoolRingConfig {
    u32 framesInFlight = 3;
    u32 initialPoolsPerFrame = 4;   // Pre-allocate pools per frame
    u32 maxPoolsPerFrame = 32;
};

struct CommandPoolRingStats {
    u32 activeFrame;
    u32 totalPools;
    u32 poolsUsedThisFrame;
    u32 commandBuffersAllocated;
};

class CommandPoolRing {
public:
    bool Init(IDevice* device, const CommandPoolRingConfig& config = {});
    void Shutdown();

    // Begin a new frame — resets the pools for this frame index
    void BeginFrame(u32 frameIndex);

    // Allocate a command buffer from the current frame's pool
    ICommandList* Allocate(QueueType queue = QueueType::Graphics);

    // Allocate a secondary command buffer
    ICommandList* AllocateSecondary(QueueType queue = QueueType::Graphics);

    // End the current frame
    void EndFrame();

    // Get current frame index
    u32 GetCurrentFrame() const { return m_currentFrame; }

    CommandPoolRingStats GetStats() const;

private:
    struct FrameCommandPool {
        u64  poolHandle = 0;         // VkCommandPool
        std::vector<ICommandList*> primaryBuffers;
        std::vector<ICommandList*> secondaryBuffers;
        u32  nextPrimary = 0;
        u32  nextSecondary = 0;
        QueueType queue = QueueType::Graphics;
    };

    struct FrameData {
        std::vector<FrameCommandPool> pools;
    };

    FrameCommandPool& GetOrCreatePool(u32 frameIdx, QueueType queue);

    IDevice* m_device = nullptr;
    CommandPoolRingConfig m_config;
    std::vector<FrameData> m_frames;
    u32 m_currentFrame = 0;
    u32 m_totalAllocated = 0;
    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
