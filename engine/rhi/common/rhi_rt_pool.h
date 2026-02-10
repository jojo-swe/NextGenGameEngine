#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Render Target Pool Manager ──────────────────────────────────────
// Manages a pool of reusable render targets to minimize allocation churn.
// Tracks usage per frame, recycles unused targets, and matches requests
// by format/resolution/flags.
//
// Use cases:
//   - Reuse transient render targets across passes
//   - Match requested format/resolution to pooled resources
//   - Track per-frame usage and recycle after N unused frames
//   - Budget tracking (total VRAM in pool)
//   - Debug naming for GPU capture tools

enum class RTFormat : u8 {
    RGBA8_Unorm,
    RGBA8_SRGB,
    RGBA16_Float,
    RGBA32_Float,
    R11G11B10_Float,
    RG16_Float,
    R16_Float,
    R32_Float,
    D24_S8,
    D32_Float,
    D32_S8,
};

enum class RTFlags : u8 {
    None             = 0,
    ColorAttachment  = 1 << 0,
    DepthStencil     = 1 << 1,
    ShaderResource   = 1 << 2,
    StorageImage     = 1 << 3,
    TransferSrc      = 1 << 4,
    TransferDst      = 1 << 5,
};

struct RenderTargetDesc {
    u32      width;
    u32      height;
    RTFormat format;
    u8       flags;         // RTFlags bitmask
    u32      mipLevels;     // 1 for no mips
    u32      arrayLayers;   // 1 for non-array
    u32      sampleCount;   // 1 for no MSAA
    std::string debugName;

    bool IsCompatible(const RenderTargetDesc& other) const {
        return width == other.width && height == other.height &&
               format == other.format && flags == other.flags &&
               mipLevels == other.mipLevels && arrayLayers == other.arrayLayers &&
               sampleCount == other.sampleCount;
    }
};

struct PooledRenderTarget {
    u32              rtId;
    RenderTargetDesc desc;
    u64              gpuHandle;     // Opaque GPU resource handle
    u32              lastUsedFrame;
    bool             inUse;
    u64              sizeBytes;
};

struct RTPoolConfig {
    u32  maxTargets = 128;
    u32  recycleAfterFrames = 8;    // Free targets unused for N frames
    u64  vramBudget = 256 * 1024 * 1024ULL;  // 256 MB
    bool enableRecycling = true;
};

struct RTPoolStats {
    u32 totalTargets;
    u32 targetsInUse;
    u32 targetsFree;
    u32 allocationsThisFrame;
    u32 reusesThisFrame;
    u32 totalAllocations;
    u32 totalReuses;
    u32 totalRecycled;
    u64 totalVRAMUsed;
    u64 vramBudget;
    float budgetUtilization;
};

class RenderTargetPool {
public:
    bool Init(const RTPoolConfig& config = {});
    void Shutdown();

    // Acquire a render target matching the description.
    // Returns rtId. Reuses a pooled target if compatible, otherwise allocates.
    u32 Acquire(const RenderTargetDesc& desc, u64 gpuHandle = 0);

    // Release a render target back to the pool
    void Release(u32 rtId);

    // Process frame: recycle unused targets, update stats
    void ProcessFrame(u32 currentFrame);

    // Get render target info
    const PooledRenderTarget* GetTarget(u32 rtId) const;

    // Check if a compatible target is available in the pool
    bool HasAvailable(const RenderTargetDesc& desc) const;

    // Get number of free targets matching a description
    u32 CountAvailable(const RenderTargetDesc& desc) const;

    // Force release all targets
    void ReleaseAll();

    u32 GetTotalCount() const;
    u32 GetInUseCount() const;
    u32 GetFreeCount() const;
    u64 GetEstimatedVRAM() const;

    void Reset();

    RTPoolStats GetStats() const;

private:
    u64 EstimateSize(const RenderTargetDesc& desc) const;
    u32 FindCompatible(const RenderTargetDesc& desc) const;

    RTPoolConfig m_config;
    std::unordered_map<u32, PooledRenderTarget> m_targets;

    u32 m_nextId = 0;
    u32 m_allocsThisFrame = 0;
    u32 m_reusesThisFrame = 0;
    u32 m_totalAllocs = 0;
    u32 m_totalReuses = 0;
    u32 m_totalRecycled = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
