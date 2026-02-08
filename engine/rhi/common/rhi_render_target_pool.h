#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <mutex>

namespace nge::rhi {

// ─── Render Target Pool ──────────────────────────────────────────────────
// Manages render target (RT) textures with format/size-aware recycling.
// Different from the generic resource pool — this is specialized for
// render targets with MSAA resolve, mip chain, and clear optimization.
//
// Tracks RT usage patterns to pre-allocate for the next frame.

struct RenderTargetDesc {
    u32    width;
    u32    height;
    Format format;
    u32    sampleCount = 1;
    u32    mipLevels = 1;
    u32    arrayLayers = 1;
    bool   clearOptimized = true; // VK_ATTACHMENT_LOAD_OP_CLEAR optimized
    std::string debugName;
};

struct PooledRenderTarget {
    TextureHandle handle;
    RenderTargetDesc desc;
    u64 lastUsedFrame = 0;
    bool inUse = false;
};

struct RenderTargetPoolConfig {
    u32 maxPooled = 64;
    u64 evictionAgeFrames = 120;
};

struct RenderTargetPoolStats {
    u32 pooledCount;
    u32 activeCount;
    u32 hits;
    u32 misses;
    u64 totalMemoryBytes;
};

class RenderTargetPool {
public:
    bool Init(IDevice* device, const RenderTargetPoolConfig& config = {});
    void Shutdown();

    // Acquire a render target matching the description
    TextureHandle Acquire(const RenderTargetDesc& desc);

    // Release back to pool
    void Release(TextureHandle handle);

    // Release all active render targets
    void ReleaseAll();

    // Per-frame maintenance
    void BeginFrame(u64 frameNumber);

    // Evict unused render targets
    u32 Evict(u64 currentFrame);

    RenderTargetPoolStats GetStats() const;

private:
    bool IsCompatible(const PooledRenderTarget& rt, const RenderTargetDesc& desc) const;
    u64 EstimateMemory(const RenderTargetDesc& desc) const;

    IDevice* m_device = nullptr;
    RenderTargetPoolConfig m_config;
    std::vector<PooledRenderTarget> m_pool;
    u64 m_currentFrame = 0;
    u32 m_hits = 0;
    u32 m_misses = 0;
    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
