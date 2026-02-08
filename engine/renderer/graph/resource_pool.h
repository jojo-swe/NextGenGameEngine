#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/renderer/graph/frame_graph_compiler.h"
#include <vector>
#include <unordered_map>
#include <mutex>

namespace nge::renderer {

// ─── Render Graph Resource Pool ──────────────────────────────────────────
// Typed transient resource allocation for compiled frame graphs.
// Allocates GPU textures and buffers for transient resources, reusing
// them across frames and aliasing compatible resources.
//
// Flow:
//   1. Frame graph compiler determines resource lifetimes and aliasing
//   2. Resource pool allocates physical resources from a reuse pool
//   3. At frame end, resources are returned to the pool
//   4. Pool evicts unused resources after N frames

struct PooledTexture {
    rhi::TextureHandle handle;
    u32                width;
    u32                height;
    rhi::Format        format;
    u32                mipLevels;
    u32                arrayLayers;
    u64                lastUsedFrame;
    bool               inUse;
};

struct PooledBuffer {
    rhi::BufferHandle  handle;
    u64                size;
    rhi::BufferUsage   usage;
    u64                lastUsedFrame;
    bool               inUse;
};

struct ResourcePoolConfig {
    u32 maxPooledTextures = 128;
    u32 maxPooledBuffers = 64;
    u64 evictionAgeFrames = 120;  // Evict after 2 seconds at 60fps
};

struct ResourcePoolStats {
    u32 pooledTextureCount;
    u32 pooledBufferCount;
    u32 activeTextures;
    u32 activeBuffers;
    u32 textureHits;
    u32 textureMisses;
    u32 bufferHits;
    u32 bufferMisses;
    u64 totalTextureMemory;
    u64 totalBufferMemory;
};

class RenderGraphResourcePool {
public:
    bool Init(rhi::IDevice* device, const ResourcePoolConfig& config = {});
    void Shutdown();

    // Acquire a transient texture matching the given description
    rhi::TextureHandle AcquireTexture(u32 width, u32 height, rhi::Format format,
                                       u32 mipLevels = 1, u32 arrayLayers = 1);

    // Acquire a transient buffer matching the given description
    rhi::BufferHandle AcquireBuffer(u64 size, rhi::BufferUsage usage);

    // Release resources back to the pool at frame end
    void ReleaseTexture(rhi::TextureHandle handle);
    void ReleaseBuffer(rhi::BufferHandle handle);

    // Release all active resources (call at end of frame)
    void ReleaseAll();

    // Evict unused resources that haven't been used for evictionAgeFrames
    u32 Evict(u64 currentFrame);

    // Per-frame update
    void BeginFrame(u64 frameNumber);

    ResourcePoolStats GetStats() const;

private:
    rhi::IDevice* m_device = nullptr;
    ResourcePoolConfig m_config;
    u64 m_currentFrame = 0;

    std::vector<PooledTexture> m_textures;
    std::vector<PooledBuffer> m_buffers;

    u32 m_textureHits = 0;
    u32 m_textureMisses = 0;
    u32 m_bufferHits = 0;
    u32 m_bufferMisses = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::renderer
