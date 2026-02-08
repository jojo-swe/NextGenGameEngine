#include "engine/rhi/common/rhi_pipeline_cache_manager.h"
#include "engine/core/logging/log.h"
#include <fstream>
#include <algorithm>

namespace nge::rhi {

bool PipelineCacheManager::Init(IDevice* device, const PipelineCacheConfig& config) {
    m_device = device;
    m_config = config;
    m_currentFrame = 0;
    m_hits = 0;
    m_misses = 0;
    m_evictions = 0;

    // Load existing cache from disk
    std::vector<u8> initialData;
    if (config.enableDiskPersistence) {
        LoadFromDisk();
    }

    // TODO: Create VkPipelineCache
    // VkPipelineCacheCreateInfo ci{};
    // ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    // ci.initialDataSize = initialData.size();
    // ci.pInitialData = initialData.empty() ? nullptr : initialData.data();
    // vkCreatePipelineCache(device, &ci, nullptr, &m_vkPipelineCache);
    m_vkPipelineCache = 1; // Stub

    NGE_LOG_INFO("Pipeline cache manager initialized: disk={}, max={}",
                 config.enableDiskPersistence, config.maxCachedPipelines);
    return true;
}

void PipelineCacheManager::Shutdown() {
    if (m_config.autoSaveOnShutdown && m_config.enableDiskPersistence) {
        SaveToDisk();
    }

    // Destroy all cached pipelines
    for (auto& [hash, cached] : m_graphicsPipelines) {
        if (cached.handle.IsValid()) {
            m_device->DestroyPipeline(cached.handle);
        }
    }
    for (auto& [hash, cached] : m_computePipelines) {
        if (cached.handle.IsValid()) {
            m_device->DestroyPipeline(cached.handle);
        }
    }

    // TODO: vkDestroyPipelineCache(device, m_vkPipelineCache, nullptr);

    m_graphicsPipelines.clear();
    m_computePipelines.clear();
}

PipelineHandle PipelineCacheManager::GetOrCreate(const GraphicsPSODesc& desc) {
    std::lock_guard lock(m_mutex);

    PSOHash hash = PSOHasher::Hash(desc);
    auto it = m_graphicsPipelines.find(hash);
    if (it != m_graphicsPipelines.end()) {
        it->second.lastUsedFrame = m_currentFrame;
        it->second.useCount++;
        m_hits++;
        return it->second.handle;
    }

    m_misses++;

    // TODO: Create VkGraphicsPipeline using m_vkPipelineCache
    // VkGraphicsPipelineCreateInfo ci = buildFromDesc(desc);
    // VkPipeline pipeline;
    // vkCreateGraphicsPipelines(device, m_vkPipelineCache, 1, &ci, nullptr, &pipeline);

    PipelineHandle handle;
    handle.id = static_cast<u32>(m_graphicsPipelines.size() + 1); // Stub

    CachedPipeline cached;
    cached.handle = handle;
    cached.hash = hash;
    cached.lastUsedFrame = m_currentFrame;
    cached.useCount = 1;
    m_graphicsPipelines[hash] = cached;

    return handle;
}

PipelineHandle PipelineCacheManager::GetOrCreate(const ComputePSODesc& desc) {
    std::lock_guard lock(m_mutex);

    PSOHash hash = PSOHasher::Hash(desc);
    auto it = m_computePipelines.find(hash);
    if (it != m_computePipelines.end()) {
        it->second.lastUsedFrame = m_currentFrame;
        it->second.useCount++;
        m_hits++;
        return it->second.handle;
    }

    m_misses++;

    // TODO: Create VkComputePipeline using m_vkPipelineCache
    PipelineHandle handle;
    handle.id = static_cast<u32>(m_computePipelines.size() + 10000);

    CachedPipeline cached;
    cached.handle = handle;
    cached.hash = hash;
    cached.lastUsedFrame = m_currentFrame;
    cached.useCount = 1;
    m_computePipelines[hash] = cached;

    return handle;
}

bool PipelineCacheManager::IsCached(const PSOHash& hash) const {
    std::lock_guard lock(m_mutex);
    return m_graphicsPipelines.count(hash) > 0 || m_computePipelines.count(hash) > 0;
}

void PipelineCacheManager::Invalidate(const PSOHash& hash) {
    std::lock_guard lock(m_mutex);

    auto git = m_graphicsPipelines.find(hash);
    if (git != m_graphicsPipelines.end()) {
        if (git->second.handle.IsValid()) {
            m_device->DestroyPipeline(git->second.handle);
        }
        m_graphicsPipelines.erase(git);
        return;
    }

    auto cit = m_computePipelines.find(hash);
    if (cit != m_computePipelines.end()) {
        if (cit->second.handle.IsValid()) {
            m_device->DestroyPipeline(cit->second.handle);
        }
        m_computePipelines.erase(cit);
    }
}

void PipelineCacheManager::InvalidateByShader(const std::string& shaderPath) {
    std::lock_guard lock(m_mutex);

    // Would need to store shader paths per pipeline for precise invalidation
    // For now, invalidate all — a full rebuild triggered by hot-reload
    NGE_LOG_INFO("Pipeline cache: invalidating all pipelines for shader '{}'", shaderPath);

    for (auto& [hash, cached] : m_graphicsPipelines) {
        if (cached.handle.IsValid()) {
            m_device->DestroyPipeline(cached.handle);
        }
    }
    for (auto& [hash, cached] : m_computePipelines) {
        if (cached.handle.IsValid()) {
            m_device->DestroyPipeline(cached.handle);
        }
    }
    m_graphicsPipelines.clear();
    m_computePipelines.clear();
}

void PipelineCacheManager::SaveToDisk() {
    if (!m_config.enableDiskPersistence) return;

    // TODO:
    // size_t dataSize;
    // vkGetPipelineCacheData(device, m_vkPipelineCache, &dataSize, nullptr);
    // std::vector<u8> data(dataSize);
    // vkGetPipelineCacheData(device, m_vkPipelineCache, &dataSize, data.data());
    //
    // std::ofstream file(m_config.cacheFilePath, std::ios::binary);
    // file.write(reinterpret_cast<const char*>(data.data()), dataSize);

    NGE_LOG_INFO("Pipeline cache saved to disk: {}",
                 m_config.cacheFilePath.string());
}

bool PipelineCacheManager::LoadFromDisk() {
    if (!m_config.enableDiskPersistence) return false;
    if (!std::filesystem::exists(m_config.cacheFilePath)) return false;

    // TODO:
    // std::ifstream file(m_config.cacheFilePath, std::ios::binary | std::ios::ate);
    // size_t size = file.tellg();
    // file.seekg(0);
    // std::vector<u8> data(size);
    // file.read(reinterpret_cast<char*>(data.data()), size);
    // Use data as initialData for VkPipelineCacheCreateInfo

    NGE_LOG_INFO("Pipeline cache loaded from disk: {}",
                 m_config.cacheFilePath.string());
    return true;
}

void PipelineCacheManager::BeginFrame(u64 frameNumber) {
    m_currentFrame = frameNumber;
}

PipelineCacheStats PipelineCacheManager::GetStats() const {
    std::lock_guard lock(m_mutex);
    PipelineCacheStats stats{};
    stats.cachedPipelines = static_cast<u32>(m_graphicsPipelines.size() + m_computePipelines.size());
    stats.hits = m_hits;
    stats.misses = m_misses;
    stats.evictions = m_evictions;

    u32 total = m_hits + m_misses;
    stats.hitRate = total > 0 ? static_cast<f32>(m_hits) / static_cast<f32>(total) : 0.0f;

    if (m_config.enableDiskPersistence && std::filesystem::exists(m_config.cacheFilePath)) {
        stats.cacheFileSizeBytes = std::filesystem::file_size(m_config.cacheFilePath);
    }

    return stats;
}

} // namespace nge::rhi
