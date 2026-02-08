#include "engine/assets/shader_variant_cache.h"
#include "engine/core/logging/log.h"
#include <fstream>
#include <algorithm>
#include <functional>

namespace nge::assets {

bool ShaderVariantKey::operator==(const ShaderVariantKey& other) const {
    return sourceHash == other.sourceHash &&
           definesHash == other.definesHash &&
           entryPoint == other.entryPoint &&
           targetProfile == other.targetProfile;
}

size_t ShaderVariantKeyHash::operator()(const ShaderVariantKey& key) const {
    size_t h = std::hash<u64>{}(key.sourceHash);
    h ^= std::hash<u64>{}(key.definesHash) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<std::string>{}(key.entryPoint) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<std::string>{}(key.targetProfile) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

bool ShaderVariantCache::Init(const VariantCacheConfig& config) {
    m_config = config;
    m_hits = 0;
    m_misses = 0;
    m_evictions = 0;

    if (config.enableDiskPersistence) {
        std::filesystem::create_directories(config.cacheDirectory);
        LoadFromDisk();
    }

    NGE_LOG_INFO("Shader variant cache initialized: {} cached variants, dir={}",
                 m_cache.size(), config.cacheDirectory.string());
    return true;
}

void ShaderVariantCache::Shutdown() {
    if (m_config.enableDiskPersistence) {
        FlushToDisk();
    }
    m_cache.clear();
}

bool ShaderVariantCache::Get(const ShaderVariantKey& key, CachedVariant& outVariant) const {
    std::lock_guard lock(m_mutex);

    auto it = m_cache.find(key);
    if (it != m_cache.end() && it->second.variant.valid) {
        outVariant = it->second.variant;
        m_hits++;
        return true;
    }

    m_misses++;
    return false;
}

void ShaderVariantCache::Put(const ShaderVariantKey& key, const CachedVariant& variant) {
    std::lock_guard lock(m_mutex);

    // Evict if at capacity
    if (m_cache.size() >= m_config.maxCachedVariants) {
        // Simple eviction: remove oldest entry
        u64 oldestTime = UINT64_MAX;
        ShaderVariantKey oldestKey{};
        for (const auto& [k, entry] : m_cache) {
            if (entry.variant.compiledTimestamp < oldestTime) {
                oldestTime = entry.variant.compiledTimestamp;
                oldestKey = k;
            }
        }
        m_cache.erase(oldestKey);
        m_evictions++;
    }

    CacheEntry entry;
    entry.key = key;
    entry.variant = variant;
    entry.dirty = true;
    m_cache[key] = std::move(entry);
}

void ShaderVariantCache::Invalidate(const ShaderVariantKey& key) {
    std::lock_guard lock(m_mutex);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        // Remove disk file
        if (m_config.enableDiskPersistence) {
            auto path = GetCacheFilePath(key);
            std::filesystem::remove(path);
        }
        m_cache.erase(it);
    }
}

void ShaderVariantCache::InvalidateBySource(u64 sourceHash) {
    std::lock_guard lock(m_mutex);

    for (auto it = m_cache.begin(); it != m_cache.end(); ) {
        if (it->first.sourceHash == sourceHash) {
            if (m_config.enableDiskPersistence) {
                auto path = GetCacheFilePath(it->first);
                std::filesystem::remove(path);
            }
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
}

void ShaderVariantCache::FlushToDisk() {
    std::lock_guard lock(m_mutex);
    if (!m_config.enableDiskPersistence) return;

    u32 flushed = 0;
    for (auto& [key, entry] : m_cache) {
        if (!entry.dirty) continue;

        auto path = GetCacheFilePath(key);
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) continue;

        // Write header
        u32 magic = 0x53564341; // "SVCA"
        u32 version = 1;
        file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        file.write(reinterpret_cast<const char*>(&version), sizeof(version));

        // Write key info
        file.write(reinterpret_cast<const char*>(&key.sourceHash), sizeof(key.sourceHash));
        file.write(reinterpret_cast<const char*>(&key.definesHash), sizeof(key.definesHash));

        u32 entryLen = static_cast<u32>(key.entryPoint.size());
        file.write(reinterpret_cast<const char*>(&entryLen), sizeof(entryLen));
        file.write(key.entryPoint.data(), entryLen);

        u32 profileLen = static_cast<u32>(key.targetProfile.size());
        file.write(reinterpret_cast<const char*>(&profileLen), sizeof(profileLen));
        file.write(key.targetProfile.data(), profileLen);

        // Write SPIR-V bytecode
        const auto& variant = entry.variant;
        u32 wordCount = static_cast<u32>(variant.spirvBytecode.size());
        file.write(reinterpret_cast<const char*>(&wordCount), sizeof(wordCount));
        file.write(reinterpret_cast<const char*>(variant.spirvBytecode.data()),
                   wordCount * sizeof(u32));

        // Write metadata
        file.write(reinterpret_cast<const char*>(&variant.compiledTimestamp), sizeof(variant.compiledTimestamp));
        file.write(reinterpret_cast<const char*>(&variant.sourceTimestamp), sizeof(variant.sourceTimestamp));
        file.write(reinterpret_cast<const char*>(&variant.compilerVersion), sizeof(variant.compilerVersion));

        entry.dirty = false;
        flushed++;
    }

    if (flushed > 0) {
        NGE_LOG_DEBUG("Shader variant cache: flushed {} entries to disk", flushed);
    }
}

bool ShaderVariantCache::LoadFromDisk() {
    if (!m_config.enableDiskPersistence) return false;
    if (!std::filesystem::exists(m_config.cacheDirectory)) return false;

    u32 loaded = 0;
    for (const auto& dirEntry : std::filesystem::directory_iterator(m_config.cacheDirectory)) {
        if (!dirEntry.is_regular_file()) continue;
        if (dirEntry.path().extension() != ".svc") continue;

        std::ifstream file(dirEntry.path(), std::ios::binary);
        if (!file.is_open()) continue;

        // Read header
        u32 magic, version;
        file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (magic != 0x53564341 || version != 1) continue;

        // Read key
        ShaderVariantKey key;
        file.read(reinterpret_cast<char*>(&key.sourceHash), sizeof(key.sourceHash));
        file.read(reinterpret_cast<char*>(&key.definesHash), sizeof(key.definesHash));

        u32 entryLen;
        file.read(reinterpret_cast<char*>(&entryLen), sizeof(entryLen));
        key.entryPoint.resize(entryLen);
        file.read(key.entryPoint.data(), entryLen);

        u32 profileLen;
        file.read(reinterpret_cast<char*>(&profileLen), sizeof(profileLen));
        key.targetProfile.resize(profileLen);
        file.read(key.targetProfile.data(), profileLen);

        // Read SPIR-V
        CachedVariant variant;
        u32 wordCount;
        file.read(reinterpret_cast<char*>(&wordCount), sizeof(wordCount));
        variant.spirvBytecode.resize(wordCount);
        file.read(reinterpret_cast<char*>(variant.spirvBytecode.data()), wordCount * sizeof(u32));

        // Read metadata
        file.read(reinterpret_cast<char*>(&variant.compiledTimestamp), sizeof(variant.compiledTimestamp));
        file.read(reinterpret_cast<char*>(&variant.sourceTimestamp), sizeof(variant.sourceTimestamp));
        file.read(reinterpret_cast<char*>(&variant.compilerVersion), sizeof(variant.compilerVersion));
        variant.valid = true;

        CacheEntry entry;
        entry.key = key;
        entry.variant = std::move(variant);
        entry.dirty = false;
        m_cache[key] = std::move(entry);
        loaded++;
    }

    NGE_LOG_INFO("Shader variant cache: loaded {} entries from disk", loaded);
    return loaded > 0;
}

void ShaderVariantCache::Clear() {
    std::lock_guard lock(m_mutex);

    if (m_config.enableDiskPersistence && std::filesystem::exists(m_config.cacheDirectory)) {
        std::filesystem::remove_all(m_config.cacheDirectory);
        std::filesystem::create_directories(m_config.cacheDirectory);
    }

    m_cache.clear();
    m_hits = 0;
    m_misses = 0;
    m_evictions = 0;
}

VariantCacheStats ShaderVariantCache::GetStats() const {
    std::lock_guard lock(m_mutex);
    VariantCacheStats stats{};
    stats.cachedVariants = static_cast<u32>(m_cache.size());
    stats.hits = m_hits;
    stats.misses = m_misses;
    stats.evictions = m_evictions;

    u32 total = m_hits + m_misses;
    stats.hitRate = total > 0 ? static_cast<f32>(m_hits) / static_cast<f32>(total) : 0.0f;

    // Calculate disk usage
    if (m_config.enableDiskPersistence && std::filesystem::exists(m_config.cacheDirectory)) {
        for (const auto& entry : std::filesystem::directory_iterator(m_config.cacheDirectory)) {
            if (entry.is_regular_file()) {
                stats.totalBytesOnDisk += entry.file_size();
            }
        }
    }

    return stats;
}

u64 ShaderVariantCache::HashSource(const std::string& source) {
    // FNV-1a 64-bit
    u64 hash = 0xcbf29ce484222325ULL;
    for (char c : source) {
        hash ^= static_cast<u64>(c);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

u64 ShaderVariantCache::HashDefines(const std::vector<std::pair<std::string, std::string>>& defines) {
    u64 hash = 0xcbf29ce484222325ULL;
    for (const auto& [name, value] : defines) {
        for (char c : name) {
            hash ^= static_cast<u64>(c);
            hash *= 0x100000001b3ULL;
        }
        hash ^= 0xFF;
        hash *= 0x100000001b3ULL;
        for (char c : value) {
            hash ^= static_cast<u64>(c);
            hash *= 0x100000001b3ULL;
        }
    }
    return hash;
}

std::string ShaderVariantCache::GetCacheFilePath(const ShaderVariantKey& key) const {
    // Build filename from combined hash
    size_t combinedHash = ShaderVariantKeyHash{}(key);
    return (m_config.cacheDirectory / (std::to_string(combinedHash) + ".svc")).string();
}

} // namespace nge::assets
