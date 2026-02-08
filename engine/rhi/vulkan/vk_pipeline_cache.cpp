#include "engine/rhi/vulkan/vk_pipeline_cache.h"
#include "engine/core/logging/log.h"
#include <fstream>
#include <cstring>
#include <functional>

namespace nge::rhi::vulkan {

bool GraphicsPipelineKey::operator==(const GraphicsPipelineKey& o) const {
    return vertexShaderHash == o.vertexShaderHash &&
           fragmentShaderHash == o.fragmentShaderHash &&
           meshShaderHash == o.meshShaderHash &&
           taskShaderHash == o.taskShaderHash &&
           renderStateHash == o.renderStateHash &&
           vertexLayoutHash == o.vertexLayoutHash &&
           colorAttachmentCount == o.colorAttachmentCount &&
           depthFormat == o.depthFormat &&
           sampleCount == o.sampleCount &&
           std::memcmp(colorFormats, o.colorFormats, sizeof(colorFormats)) == 0;
}

usize GraphicsPipelineKeyHash::operator()(const GraphicsPipelineKey& key) const {
    usize h = 0;
    auto combine = [&](usize val) { h ^= val + 0x9e3779b9 + (h << 6) + (h >> 2); };
    combine(std::hash<u64>{}(key.vertexShaderHash));
    combine(std::hash<u64>{}(key.fragmentShaderHash));
    combine(std::hash<u64>{}(key.meshShaderHash));
    combine(std::hash<u64>{}(key.taskShaderHash));
    combine(std::hash<u64>{}(key.renderStateHash));
    combine(std::hash<u64>{}(key.vertexLayoutHash));
    combine(std::hash<u32>{}(key.colorAttachmentCount));
    combine(std::hash<u32>{}(key.sampleCount));
    combine(std::hash<u8>{}(static_cast<u8>(key.depthFormat)));
    for (u32 i = 0; i < key.colorAttachmentCount; ++i) {
        combine(std::hash<u8>{}(static_cast<u8>(key.colorFormats[i])));
    }
    return h;
}

usize ComputePipelineKeyHash::operator()(const ComputePipelineKey& key) const {
    usize h = std::hash<u64>{}(key.shaderHash);
    h ^= std::hash<u64>{}(key.layoutHash) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

bool PipelineCache::Init(void* vkDevice, const std::string& cacheFilePath) {
    m_device = vkDevice;
    m_cacheFilePath = cacheFilePath;
    m_cacheHits = 0;
    m_cacheMisses = 0;

    // Try loading existing cache from disk
    LoadFromDisk();

    // TODO: Create VkPipelineCache
    // VkPipelineCacheCreateInfo ci{};
    // ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    // ci.initialDataSize = cacheData.size();
    // ci.pInitialData = cacheData.data();
    // vkCreatePipelineCache(device, &ci, nullptr, &cache);
    // m_vkCache = reinterpret_cast<u64>(cache);

    m_vkCache = 1; // Stub

    NGE_LOG_INFO("Pipeline cache initialized (file: '{}')", cacheFilePath);
    return true;
}

void PipelineCache::Shutdown() {
    SaveToDisk();

    // TODO: vkDestroyPipelineCache
    m_graphicsPipelines.clear();
    m_computePipelines.clear();
    m_vkCache = 0;

    NGE_LOG_INFO("Pipeline cache shutdown: {} graphics, {} compute, {} hits, {} misses",
                 m_graphicsPipelines.size(), m_computePipelines.size(),
                 m_cacheHits, m_cacheMisses);
}

bool PipelineCache::SaveToDisk() {
    if (m_cacheFilePath.empty()) return false;

    // TODO: Get cache data from Vulkan
    // usize dataSize = 0;
    // vkGetPipelineCacheData(device, cache, &dataSize, nullptr);
    // std::vector<u8> data(dataSize);
    // vkGetPipelineCacheData(device, cache, &dataSize, data.data());

    std::vector<u8> data; // Stub

    std::ofstream file(m_cacheFilePath, std::ios::binary);
    if (!file.is_open()) {
        NGE_LOG_WARN("Failed to save pipeline cache to '{}'", m_cacheFilePath);
        return false;
    }

    // Write header
    u32 magic = 0x50434348; // "PCCH"
    u32 version = 1;
    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));

    u32 dataSize = static_cast<u32>(data.size());
    file.write(reinterpret_cast<const char*>(&dataSize), sizeof(dataSize));
    if (!data.empty()) {
        file.write(reinterpret_cast<const char*>(data.data()), data.size());
    }

    NGE_LOG_INFO("Saved pipeline cache to '{}' ({} bytes)", m_cacheFilePath, dataSize);
    return true;
}

bool PipelineCache::LoadFromDisk() {
    if (m_cacheFilePath.empty()) return false;

    std::ifstream file(m_cacheFilePath, std::ios::binary);
    if (!file.is_open()) {
        NGE_LOG_DEBUG("No existing pipeline cache at '{}'", m_cacheFilePath);
        return false;
    }

    // Read header
    u32 magic = 0, version = 0;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&version), sizeof(version));

    if (magic != 0x50434348 || version != 1) {
        NGE_LOG_WARN("Invalid pipeline cache file (magic=0x{:08X}, version={})", magic, version);
        return false;
    }

    u32 dataSize = 0;
    file.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));

    if (dataSize > 0) {
        std::vector<u8> data(dataSize);
        file.read(reinterpret_cast<char*>(data.data()), dataSize);
        // TODO: Use data to initialize VkPipelineCache
    }

    NGE_LOG_INFO("Loaded pipeline cache from '{}' ({} bytes)", m_cacheFilePath, dataSize);
    return true;
}

void PipelineCache::RegisterGraphicsPipeline(const GraphicsPipelineKey& key, u64 pipelineHandle) {
    std::lock_guard lock(m_mutex);
    m_graphicsPipelines[key] = pipelineHandle;
}

void PipelineCache::RegisterComputePipeline(const ComputePipelineKey& key, u64 pipelineHandle) {
    std::lock_guard lock(m_mutex);
    m_computePipelines[key] = pipelineHandle;
}

u64 PipelineCache::FindGraphicsPipeline(const GraphicsPipelineKey& key) const {
    std::lock_guard lock(m_mutex);
    auto it = m_graphicsPipelines.find(key);
    if (it != m_graphicsPipelines.end()) {
        m_cacheHits++;
        return it->second;
    }
    m_cacheMisses++;
    return 0;
}

u64 PipelineCache::FindComputePipeline(const ComputePipelineKey& key) const {
    std::lock_guard lock(m_mutex);
    auto it = m_computePipelines.find(key);
    if (it != m_computePipelines.end()) {
        m_cacheHits++;
        return it->second;
    }
    m_cacheMisses++;
    return 0;
}

} // namespace nge::rhi::vulkan
