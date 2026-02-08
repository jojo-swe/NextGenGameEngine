#include "engine/rhi/common/rhi_shader_module_cache.h"
#include "engine/core/logging/log.h"
#include <fstream>
#include <algorithm>

namespace nge::rhi {

bool ShaderModuleCache::Init(IDevice* device, const ShaderModuleCacheConfig& config) {
    m_device = device;
    m_config = config;
    m_hits = 0;
    m_misses = 0;
    m_evictions = 0;

    NGE_LOG_INFO("Shader module cache initialized: max={}, evict after {} frames",
                 config.maxCachedModules, config.evictionAgeFrames);
    return true;
}

void ShaderModuleCache::Shutdown() {
    std::lock_guard lock(m_mutex);
    for (auto& [hash, mod] : m_modules) {
        // TODO: vkDestroyShaderModule(device, mod.handle, nullptr);
    }
    m_modules.clear();
    m_pathToHash.clear();
}

u64 ShaderModuleCache::GetOrLoad(const ShaderModuleDesc& desc, u64 frameNumber) {
    std::lock_guard lock(m_mutex);

    // Check path cache first
    auto pathStr = desc.filePath.string();
    auto pathIt = m_pathToHash.find(pathStr);
    if (pathIt != m_pathToHash.end()) {
        auto modIt = m_modules.find(pathIt->second);
        if (modIt != m_modules.end()) {
            modIt->second.lastUsedFrame = frameNumber;
            modIt->second.refCount++;
            m_hits++;
            return modIt->second.handle;
        }
    }

    m_misses++;

    // Load from disk
    auto spirv = LoadFile(desc.filePath);
    if (spirv.empty()) {
        NGE_LOG_ERROR("Failed to load shader: {}", pathStr);
        return 0;
    }

    u64 hash = desc.contentHash != 0 ? desc.contentHash : HashSpirv(spirv);

    // Check if this hash already exists (different path, same content)
    auto hashIt = m_modules.find(hash);
    if (hashIt != m_modules.end()) {
        hashIt->second.lastUsedFrame = frameNumber;
        hashIt->second.refCount++;
        m_pathToHash[pathStr] = hash;
        return hashIt->second.handle;
    }

    // Create new module
    // TODO:
    // VkShaderModuleCreateInfo ci{};
    // ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    // ci.codeSize = spirv.size();
    // ci.pCode = reinterpret_cast<const u32*>(spirv.data());
    // VkShaderModule shaderModule;
    // vkCreateShaderModule(device, &ci, nullptr, &shaderModule);

    CachedShaderModule cached;
    cached.handle = hash; // Stub: use hash as handle
    cached.contentHash = hash;
    cached.filePath = desc.filePath;
    cached.entryPoint = desc.entryPoint;
    cached.lastUsedFrame = frameNumber;
    cached.refCount = 1;
    if (m_config.keepBytecodeInMemory) {
        cached.spirvBytes = std::move(spirv);
    }

    m_modules[hash] = std::move(cached);
    m_pathToHash[pathStr] = hash;

    return hash;
}

u64 ShaderModuleCache::GetOrCreate(const std::vector<u8>& spirv, const std::string& debugName, u64 frameNumber) {
    std::lock_guard lock(m_mutex);

    u64 hash = HashSpirv(spirv);
    auto it = m_modules.find(hash);
    if (it != m_modules.end()) {
        it->second.lastUsedFrame = frameNumber;
        it->second.refCount++;
        m_hits++;
        return it->second.handle;
    }

    m_misses++;

    // TODO: vkCreateShaderModule from spirv data

    CachedShaderModule cached;
    cached.handle = hash;
    cached.contentHash = hash;
    cached.entryPoint = "main";
    cached.lastUsedFrame = frameNumber;
    cached.refCount = 1;
    if (m_config.keepBytecodeInMemory) {
        cached.spirvBytes = spirv;
    }

    m_modules[hash] = std::move(cached);
    (void)debugName;
    return hash;
}

void ShaderModuleCache::AddRef(u64 handle) {
    std::lock_guard lock(m_mutex);
    for (auto& [hash, mod] : m_modules) {
        if (mod.handle == handle) {
            mod.refCount++;
            return;
        }
    }
}

void ShaderModuleCache::Release(u64 handle) {
    std::lock_guard lock(m_mutex);
    for (auto& [hash, mod] : m_modules) {
        if (mod.handle == handle && mod.refCount > 0) {
            mod.refCount--;
            return;
        }
    }
}

void ShaderModuleCache::Invalidate(const std::filesystem::path& filePath) {
    std::lock_guard lock(m_mutex);
    auto pathStr = filePath.string();
    auto pathIt = m_pathToHash.find(pathStr);
    if (pathIt != m_pathToHash.end()) {
        auto modIt = m_modules.find(pathIt->second);
        if (modIt != m_modules.end()) {
            // TODO: vkDestroyShaderModule
            m_modules.erase(modIt);
        }
        m_pathToHash.erase(pathIt);
        NGE_LOG_INFO("Shader module invalidated: {}", pathStr);
    }
}

void ShaderModuleCache::InvalidateAll() {
    std::lock_guard lock(m_mutex);
    for (auto& [hash, mod] : m_modules) {
        // TODO: vkDestroyShaderModule
    }
    m_modules.clear();
    m_pathToHash.clear();
    NGE_LOG_INFO("All shader modules invalidated");
}

u32 ShaderModuleCache::EvictUnused(u64 currentFrame) {
    std::lock_guard lock(m_mutex);
    u32 evicted = 0;

    for (auto it = m_modules.begin(); it != m_modules.end(); ) {
        if (it->second.refCount == 0 &&
            currentFrame - it->second.lastUsedFrame > m_config.evictionAgeFrames) {
            // TODO: vkDestroyShaderModule
            // Remove path mapping
            for (auto pathIt = m_pathToHash.begin(); pathIt != m_pathToHash.end(); ) {
                if (pathIt->second == it->first) {
                    pathIt = m_pathToHash.erase(pathIt);
                } else {
                    ++pathIt;
                }
            }
            it = m_modules.erase(it);
            evicted++;
            m_evictions++;
        } else {
            ++it;
        }
    }

    return evicted;
}

bool ShaderModuleCache::HasModule(u64 contentHash) const {
    std::lock_guard lock(m_mutex);
    return m_modules.count(contentHash) > 0;
}

ShaderModuleCacheStats ShaderModuleCache::GetStats() const {
    std::lock_guard lock(m_mutex);
    ShaderModuleCacheStats stats{};
    stats.cachedModules = static_cast<u32>(m_modules.size());
    stats.hits = m_hits;
    stats.misses = m_misses;
    stats.evictions = m_evictions;

    for (const auto& [hash, mod] : m_modules) {
        stats.totalBytecodeBytes += mod.spirvBytes.size();
    }
    return stats;
}

u64 ShaderModuleCache::HashSpirv(const std::vector<u8>& spirv) {
    // FNV-1a 64-bit hash
    u64 hash = 14695981039346656037ULL;
    for (u8 byte : spirv) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::vector<u8> ShaderModuleCache::LoadFile(const std::filesystem::path& path) const {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};

    auto size = file.tellg();
    if (size <= 0) return {};

    std::vector<u8> data(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

} // namespace nge::rhi
