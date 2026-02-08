#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/rhi/common/rhi_pso_builder.h"
#include "engine/rhi/common/rhi_pso_hash.h"
#include <unordered_map>
#include <vector>
#include <mutex>
#include <filesystem>

namespace nge::rhi {

// ─── GPU Pipeline Cache Manager ──────────────────────────────────────────
// Manages VkPipelineCache with disk persistence and PSO deduplication.
// Caches compiled pipelines across frames and sessions. Merges pipeline
// cache data on shutdown for faster startup on next run.
//
// Combines PSO hashing (fast lookup) with Vulkan's built-in pipeline
// cache (driver-level caching of compiled shader microcode).

struct PipelineCacheConfig {
    std::filesystem::path cacheFilePath = "pipeline_cache.bin";
    bool enableDiskPersistence = true;
    u32 maxCachedPipelines = 4096;
    bool autoSaveOnShutdown = true;
};

struct CachedPipeline {
    PipelineHandle handle;
    PSOHash        hash;
    u64            lastUsedFrame;
    u32            useCount;
};

struct PipelineCacheStats {
    u32 cachedPipelines;
    u32 hits;
    u32 misses;
    u32 evictions;
    u64 cacheFileSizeBytes;
    f32 hitRate;
};

class PipelineCacheManager {
public:
    bool Init(IDevice* device, const PipelineCacheConfig& config = {});
    void Shutdown();

    // Look up or create a graphics pipeline
    PipelineHandle GetOrCreate(const GraphicsPSODesc& desc);

    // Look up or create a compute pipeline
    PipelineHandle GetOrCreate(const ComputePSODesc& desc);

    // Check if a pipeline is cached
    bool IsCached(const PSOHash& hash) const;

    // Invalidate a specific pipeline (e.g., shader recompiled)
    void Invalidate(const PSOHash& hash);

    // Invalidate all pipelines using a specific shader
    void InvalidateByShader(const std::string& shaderPath);

    // Save pipeline cache to disk
    void SaveToDisk();

    // Load pipeline cache from disk
    bool LoadFromDisk();

    // Per-frame update
    void BeginFrame(u64 frameNumber);

    PipelineCacheStats GetStats() const;

    // Get the Vulkan pipeline cache handle (for vkCreateGraphicsPipelines)
    u64 GetVkPipelineCache() const { return m_vkPipelineCache; }

private:
    IDevice* m_device = nullptr;
    PipelineCacheConfig m_config;

    u64 m_vkPipelineCache = 0; // VkPipelineCache handle

    std::unordered_map<PSOHash, CachedPipeline> m_graphicsPipelines;
    std::unordered_map<PSOHash, CachedPipeline> m_computePipelines;

    u64 m_currentFrame = 0;
    mutable u32 m_hits = 0;
    mutable u32 m_misses = 0;
    u32 m_evictions = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
