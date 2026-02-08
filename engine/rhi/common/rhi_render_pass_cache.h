#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/rhi/common/rhi_attachment.h"
#include <unordered_map>
#include <vector>
#include <mutex>

namespace nge::rhi {

// ─── Render Pass Cache ───────────────────────────────────────────────────
// Caches VkRenderPass objects (or dynamic rendering configurations) to
// avoid redundant creation. Keyed by attachment formats, load/store ops,
// sample count, and layout transitions.
//
// For VK_KHR_dynamic_rendering: caches the VkRenderingInfo configurations.
// For legacy render passes: caches VkRenderPass + VkFramebuffer pairs.

struct RenderPassKey {
    std::vector<Format> colorFormats;
    Format              depthFormat = Format::Undefined;
    u32                 sampleCount = 1;
    std::vector<LoadOp> colorLoadOps;
    std::vector<StoreOp> colorStoreOps;
    LoadOp              depthLoadOp = LoadOp::Clear;
    StoreOp             depthStoreOp = StoreOp::Store;
    bool                hasDepth = false;

    bool operator==(const RenderPassKey& other) const;
};

struct RenderPassKeyHash {
    size_t operator()(const RenderPassKey& key) const;
};

struct CachedRenderPass {
    u64  handle = 0;       // VkRenderPass or configuration ID
    u32  colorAttachmentCount = 0;
    bool hasDepth = false;
    u64  lastUsedFrame = 0;
};

class RenderPassCache {
public:
    bool Init(IDevice* device);
    void Shutdown();

    // Get or create a render pass matching the description
    CachedRenderPass GetOrCreate(const RenderPassDesc& desc, u64 frameNumber);

    // Get or create from explicit key
    CachedRenderPass GetOrCreate(const RenderPassKey& key, u64 frameNumber);

    // Evict unused render passes (call periodically)
    u32 EvictUnused(u64 currentFrame, u64 maxAge = 300);

    // Stats
    u32 GetCachedCount() const;
    u32 GetHitCount() const { return m_hitCount; }
    u32 GetMissCount() const { return m_missCount; }
    f32 GetHitRate() const;

private:
    CachedRenderPass CreateRenderPass(const RenderPassKey& key);
    RenderPassKey BuildKey(const RenderPassDesc& desc) const;

    IDevice* m_device = nullptr;

    std::unordered_map<RenderPassKey, CachedRenderPass, RenderPassKeyHash> m_cache;
    mutable std::mutex m_mutex;

    u32 m_hitCount = 0;
    u32 m_missCount = 0;
};

} // namespace nge::rhi
