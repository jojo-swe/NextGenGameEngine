#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <unordered_map>
#include <vector>

namespace nge::rhi {

// ─── Resource Barrier Tracker ────────────────────────────────────────────
// Automatically tracks resource states and emits minimal barriers.
// Eliminates redundant transitions and batches barriers for efficiency.
//
// Each texture/buffer has a tracked "current state". When a pass needs
// a resource in a different state, a barrier is recorded. Split barriers
// (release/acquire) are used for cross-queue transitions.

struct PendingBarrier {
    enum class Type : u8 { Buffer, Texture };

    Type           type;
    BufferHandle   buffer;
    TextureHandle  texture;
    ResourceState  oldState;
    ResourceState  newState;
    u32            subresource = UINT32_MAX; // UINT32_MAX = all subresources
    QueueType      srcQueue = QueueType::Graphics;
    QueueType      dstQueue = QueueType::Graphics;
};

class BarrierTracker {
public:
    void Init();
    void Reset();

    // Track initial state of a resource
    void TrackBuffer(BufferHandle buffer, ResourceState initialState);
    void TrackTexture(TextureHandle texture, ResourceState initialState, u32 mipLevels = 1, u32 arrayLayers = 1);

    // Request a resource transition — returns true if a barrier is needed
    bool TransitionBuffer(BufferHandle buffer, ResourceState newState);
    bool TransitionTexture(TextureHandle texture, ResourceState newState, u32 mipLevel = UINT32_MAX);

    // Cross-queue transitions (split barrier: release on src, acquire on dst)
    bool TransitionBufferCrossQueue(BufferHandle buffer, ResourceState newState,
                                      QueueType srcQueue, QueueType dstQueue);
    bool TransitionTextureCrossQueue(TextureHandle texture, ResourceState newState,
                                       QueueType srcQueue, QueueType dstQueue);

    // Get current state of a resource
    ResourceState GetBufferState(BufferHandle buffer) const;
    ResourceState GetTextureState(TextureHandle texture, u32 mipLevel = 0) const;

    // Flush pending barriers into command list
    void FlushBarriers(ICommandList* cmd);

    // Get pending barrier count (for diagnostics)
    u32 GetPendingCount() const { return static_cast<u32>(m_pending.size()); }

    // Untrack resources (on destruction)
    void UntrackBuffer(BufferHandle buffer);
    void UntrackTexture(TextureHandle texture);

private:
    struct TextureState {
        std::vector<ResourceState> subresourceStates; // Per mip×layer
        u32 mipLevels = 1;
        u32 arrayLayers = 1;
    };

    std::unordered_map<u64, ResourceState> m_bufferStates;    // BufferHandle.id → state
    std::unordered_map<u64, TextureState>  m_textureStates;   // TextureHandle.id → state

    std::vector<PendingBarrier> m_pending;
};

} // namespace nge::rhi
