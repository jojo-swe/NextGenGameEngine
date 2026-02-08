#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <queue>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Command Buffer Recycler ─────────────────────────────────────────
// Reuses secondary command buffers across frames to avoid allocation
// overhead. Secondary command buffers are pre-recorded and can be
// executed inside primary command buffers (vkCmdExecuteCommands).
//
// Use cases:
//   - Static geometry draw calls (re-record only when dirty)
//   - Shadow map passes (same geometry, different view)
//   - UI rendering (rarely changes)
//   - Multi-threaded command recording

enum class CommandBufferLevel : u8 {
    Primary,
    Secondary,
};

struct RecycledCommandBuffer {
    u64                handle;       // VkCommandBuffer
    u64                poolHandle;   // VkCommandPool it belongs to
    CommandBufferLevel level;
    u32                queueFamily;
    std::string        debugName;
    u64                lastUsedFrame;
    bool               inUse;
    bool               recorded;     // Has valid recorded commands
};

struct CommandBufferRecyclerConfig {
    u32 initialSecondaryCount = 32;
    u32 maxSecondaryCount = 512;
    u32 framesBeforeRecycle = 3;  // Wait N frames before reusing
    bool allowGrowth = true;
};

struct CommandBufferRecyclerStats {
    u32 totalBuffers;
    u32 inUseBuffers;
    u32 availableBuffers;
    u32 recordedBuffers;
    u32 growthEvents;
    u32 recycleEvents;
};

class CommandBufferRecycler {
public:
    bool Init(IDevice* device, const CommandBufferRecyclerConfig& config = {});
    void Shutdown();

    // Acquire a secondary command buffer for recording
    u32 AcquireSecondary(u32 queueFamily = 0, const std::string& debugName = "");

    // Mark as recorded (has valid commands that can be re-executed)
    void MarkRecorded(u32 bufferId);

    // Mark as dirty (needs re-recording before next use)
    void MarkDirty(u32 bufferId);

    // Check if a buffer has valid recorded commands
    bool IsRecorded(u32 bufferId) const;

    // Release a command buffer back to the pool
    void Release(u32 bufferId);

    // Get the VkCommandBuffer handle
    u64 GetHandle(u32 bufferId) const;

    // Per-frame: recycle buffers that haven't been used for N frames
    void BeginFrame(u64 frameNumber);

    // Reset all command buffers
    void ResetAll();

    CommandBufferRecyclerStats GetStats() const;

private:
    void GrowPool(u32 count, u32 queueFamily);

    IDevice* m_device = nullptr;
    CommandBufferRecyclerConfig m_config;
    std::vector<RecycledCommandBuffer> m_buffers;
    std::queue<u32> m_available;
    u64 m_currentFrame = 0;
    u32 m_growthEvents = 0;
    u32 m_recycleEvents = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
