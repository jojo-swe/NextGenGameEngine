#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace nge::rhi::vulkan {

// ─── Pipeline Cache ──────────────────────────────────────────────────────
// Caches compiled pipeline state objects (PSOs) to disk and memory.
// Avoids recompilation on subsequent engine launches.
//
// Wraps VkPipelineCache with:
//   - Automatic save/load from disk
//   - Per-pipeline hash-based lookup
//   - Warm-up support (pre-create known pipelines)

using VkPipelineCacheHandle = u64; // VkPipelineCache cast

struct GraphicsPipelineKey {
    u64 vertexShaderHash;
    u64 fragmentShaderHash;
    u64 meshShaderHash;       // 0 if not using mesh shaders
    u64 taskShaderHash;       // 0 if not using task shaders
    u64 renderStateHash;      // Blend, depth, raster state combined hash
    u64 vertexLayoutHash;
    u32 colorAttachmentCount;
    Format colorFormats[8];
    Format depthFormat;
    u32 sampleCount;

    bool operator==(const GraphicsPipelineKey& o) const;
};

struct ComputePipelineKey {
    u64 shaderHash;
    u64 layoutHash;

    bool operator==(const ComputePipelineKey& o) const {
        return shaderHash == o.shaderHash && layoutHash == o.layoutHash;
    }
};

struct GraphicsPipelineKeyHash {
    usize operator()(const GraphicsPipelineKey& key) const;
};

struct ComputePipelineKeyHash {
    usize operator()(const ComputePipelineKey& key) const;
};

class PipelineCache {
public:
    bool Init(void* vkDevice, const std::string& cacheFilePath = "pipeline_cache.bin");
    void Shutdown();

    // Save cache to disk (call on shutdown or periodically)
    bool SaveToDisk();

    // Load cache from disk (call on init)
    bool LoadFromDisk();

    // Get the Vulkan pipeline cache handle for pipeline creation
    VkPipelineCacheHandle GetVkCache() const { return m_vkCache; }

    // Track created pipelines
    void RegisterGraphicsPipeline(const GraphicsPipelineKey& key, u64 pipelineHandle);
    void RegisterComputePipeline(const ComputePipelineKey& key, u64 pipelineHandle);

    u64 FindGraphicsPipeline(const GraphicsPipelineKey& key) const;
    u64 FindComputePipeline(const ComputePipelineKey& key) const;

    // Stats
    u32 GetGraphicsPipelineCount() const { return static_cast<u32>(m_graphicsPipelines.size()); }
    u32 GetComputePipelineCount() const { return static_cast<u32>(m_computePipelines.size()); }
    u32 GetCacheHits() const { return m_cacheHits; }
    u32 GetCacheMisses() const { return m_cacheMisses; }

private:
    void* m_device = nullptr;
    VkPipelineCacheHandle m_vkCache = 0;
    std::string m_cacheFilePath;

    std::unordered_map<GraphicsPipelineKey, u64, GraphicsPipelineKeyHash> m_graphicsPipelines;
    std::unordered_map<ComputePipelineKey, u64, ComputePipelineKeyHash> m_computePipelines;

    mutable u32 m_cacheHits = 0;
    mutable u32 m_cacheMisses = 0;
    mutable std::mutex m_mutex;
};

} // namespace nge::rhi::vulkan
