#pragma once

#include "engine/core/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <filesystem>

namespace nge::assets {

// ─── Shader Variant Cache ────────────────────────────────────────────────
// Persistent on-disk cache of compiled PSOs keyed by permutation hash.
// Avoids recompiling shader variants that haven't changed between runs.
//
// Cache key = hash(shader source + defines + target profile + entry point)
// Cache value = compiled SPIR-V bytecode + pipeline state blob
//
// On startup, loads the cache index from disk. On shader compile, checks
// cache first. On shutdown (or periodically), flushes new entries to disk.

struct ShaderVariantKey {
    u64         sourceHash;     // Hash of shader source code
    u64         definesHash;    // Hash of preprocessor defines
    std::string entryPoint;
    std::string targetProfile;  // e.g., "cs_6_6", "vs_6_6"

    bool operator==(const ShaderVariantKey& other) const;
};

struct ShaderVariantKeyHash {
    size_t operator()(const ShaderVariantKey& key) const;
};

struct CachedVariant {
    std::vector<u32> spirvBytecode;
    u64              compiledTimestamp;
    u64              sourceTimestamp;
    u32              compilerVersion;
    bool             valid = false;
};

struct VariantCacheConfig {
    std::filesystem::path cacheDirectory = "shader_cache";
    u32 maxCachedVariants = 8192;
    bool enableDiskPersistence = true;
    bool validateOnLoad = true;  // Re-check source timestamps
};

struct VariantCacheStats {
    u32 cachedVariants;
    u32 hits;
    u32 misses;
    u32 evictions;
    u64 totalBytesOnDisk;
    f32 hitRate;
};

class ShaderVariantCache {
public:
    bool Init(const VariantCacheConfig& config = {});
    void Shutdown();

    // Look up a cached variant
    bool Get(const ShaderVariantKey& key, CachedVariant& outVariant) const;

    // Store a compiled variant
    void Put(const ShaderVariantKey& key, const CachedVariant& variant);

    // Invalidate a specific variant (e.g., source changed)
    void Invalidate(const ShaderVariantKey& key);

    // Invalidate all variants for a given source hash
    void InvalidateBySource(u64 sourceHash);

    // Flush new/modified entries to disk
    void FlushToDisk();

    // Load cache index from disk
    bool LoadFromDisk();

    // Clear entire cache (memory + disk)
    void Clear();

    // Stats
    VariantCacheStats GetStats() const;

    // Utility: compute hash for source code + defines
    static u64 HashSource(const std::string& source);
    static u64 HashDefines(const std::vector<std::pair<std::string, std::string>>& defines);

private:
    struct CacheEntry {
        ShaderVariantKey key;
        CachedVariant    variant;
        bool             dirty = false;  // Needs to be flushed to disk
    };

    std::string GetCacheFilePath(const ShaderVariantKey& key) const;

    VariantCacheConfig m_config;
    std::unordered_map<ShaderVariantKey, CacheEntry, ShaderVariantKeyHash> m_cache;
    mutable std::mutex m_mutex;

    mutable u32 m_hits = 0;
    mutable u32 m_misses = 0;
    u32 m_evictions = 0;
};

} // namespace nge::assets
