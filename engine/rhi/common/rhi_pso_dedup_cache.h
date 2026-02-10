#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <list>
#include <mutex>
#include <functional>

namespace nge::rhi {

// ─── GPU PSO Deduplication Cache with LRU Eviction ───────────────────────
// Caches pipeline state objects (PSOs) keyed by a hash of their creation
// parameters. Eliminates redundant pipeline compilations and provides LRU
// eviction when the cache exceeds capacity.
//
// Use cases:
//   - Deduplicate identical PSO requests across materials/passes
//   - Avoid redundant vkCreateGraphicsPipelines calls
//   - Bound cache memory with LRU eviction policy
//   - Warm-up: pre-populate cache from disk serialized data
//   - Hit/miss statistics for pipeline compilation profiling

enum class PSOType : u8 {
    Graphics,
    Compute,
    RayTracing,
    MeshShader,
};

struct PSOKey {
    u64 hash;          // FNV-1a or xxHash of full PSO description
    PSOType type;

    bool operator==(const PSOKey& other) const {
        return hash == other.hash && type == other.type;
    }
};

struct PSOKeyHasher {
    size_t operator()(const PSOKey& key) const {
        return std::hash<u64>()(key.hash) ^ (static_cast<size_t>(key.type) << 48);
    }
};

struct PSOEntry {
    PSOKey   key;
    u64      pipelineHandle;    // VkPipeline or equivalent
    u64      layoutHandle;      // VkPipelineLayout
    PSOType  type;
    u64      creationTimeNs;    // Time to compile (nanoseconds)
    u32      frameLastUsed;
    u32      useCount;
    std::string debugName;
};

struct PSODedupCacheConfig {
    u32  maxEntries = 4096;
    bool enableLRU = true;
    bool trackCompilationTime = true;
    bool logEvictions = false;
    u32  warmUpBatchSize = 64;   // Max PSOs to compile per frame during warm-up
};

struct PSODedupCacheStats {
    u32 totalEntries;
    u32 totalLookups;
    u32 cacheHits;
    u32 cacheMisses;
    u32 evictions;
    u32 insertions;
    float hitRate;               // cacheHits / totalLookups
    u64 totalCompilationTimeNs;
    u64 savedCompilationTimeNs;  // Estimated time saved by cache hits
    u32 peakEntries;
};

class PSODedupCache {
public:
    bool Init(const PSODedupCacheConfig& config = {});
    void Shutdown();

    // Look up a PSO by key. Returns pipeline handle or 0 if not found.
    u64 Lookup(const PSOKey& key);

    // Insert a new PSO into the cache.
    void Insert(const PSOKey& key, u64 pipelineHandle, u64 layoutHandle,
                u64 compilationTimeNs = 0, const std::string& debugName = "");

    // Look up or create: returns existing handle or 0 (caller must compile & insert).
    u64 LookupOrReserve(const PSOKey& key, bool& wasFound);

    // Check if a PSO exists without updating LRU order.
    bool Contains(const PSOKey& key) const;

    // Remove a specific PSO.
    bool Remove(const PSOKey& key);

    // Mark a PSO as used this frame (updates LRU).
    void Touch(const PSOKey& key, u32 currentFrame);

    // Evict least recently used entries to reach target count.
    u32 EvictToCount(u32 targetCount);

    // Evict entries not used since given frame.
    u32 EvictOlderThan(u32 frameThreshold);

    // Get info about a cached PSO.
    const PSOEntry* GetEntry(const PSOKey& key) const;

    // Get number of cached PSOs.
    u32 GetCount() const;

    void Clear();
    void Reset();

    PSODedupCacheStats GetStats() const;

private:
    void EvictLRU();
    void MoveToFront(const PSOKey& key);

    PSODedupCacheConfig m_config;

    // LRU list: front = most recently used, back = least recently used
    std::list<PSOKey> m_lruList;
    using LRUIterator = std::list<PSOKey>::iterator;

    struct CacheNode {
        PSOEntry entry;
        LRUIterator lruIt;
    };

    std::unordered_map<PSOKey, CacheNode, PSOKeyHasher> m_cache;

    u32 m_totalLookups = 0;
    u32 m_cacheHits = 0;
    u32 m_cacheMisses = 0;
    u32 m_evictions = 0;
    u32 m_insertions = 0;
    u32 m_peakEntries = 0;
    u64 m_totalCompileTime = 0;
    u64 m_savedCompileTime = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
