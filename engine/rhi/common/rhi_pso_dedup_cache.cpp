#include "engine/rhi/common/rhi_pso_dedup_cache.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool PSODedupCache::Init(const PSODedupCacheConfig& config) {
    m_config = config;
    m_totalLookups = 0;
    m_cacheHits = 0;
    m_cacheMisses = 0;
    m_evictions = 0;
    m_insertions = 0;
    m_peakEntries = 0;
    m_totalCompileTime = 0;
    m_savedCompileTime = 0;

    NGE_LOG_INFO("PSO dedup cache initialized: maxEntries={}, LRU={}, trackCompile={}",
                 config.maxEntries, config.enableLRU, config.trackCompilationTime);
    return true;
}

void PSODedupCache::Shutdown() {
    m_cache.clear();
    m_lruList.clear();
}

u64 PSODedupCache::Lookup(const PSOKey& key) {
    std::lock_guard lock(m_mutex);
    m_totalLookups++;

    auto it = m_cache.find(key);
    if (it == m_cache.end()) {
        m_cacheMisses++;
        return 0;
    }

    m_cacheHits++;
    it->second.entry.useCount++;

    // Estimate saved compilation time
    if (m_config.trackCompilationTime) {
        m_savedCompileTime += it->second.entry.creationTimeNs;
    }

    // Update LRU
    if (m_config.enableLRU) {
        m_lruList.erase(it->second.lruIt);
        m_lruList.push_front(key);
        it->second.lruIt = m_lruList.begin();
    }

    return it->second.entry.pipelineHandle;
}

void PSODedupCache::Insert(const PSOKey& key, u64 pipelineHandle, u64 layoutHandle,
                            u64 compilationTimeNs, const std::string& debugName) {
    std::lock_guard lock(m_mutex);

    // Already exists? Update handle.
    auto existing = m_cache.find(key);
    if (existing != m_cache.end()) {
        existing->second.entry.pipelineHandle = pipelineHandle;
        existing->second.entry.layoutHandle = layoutHandle;
        return;
    }

    // Evict if at capacity
    if (m_config.enableLRU && m_cache.size() >= m_config.maxEntries) {
        EvictLRU();
    }

    // Insert new entry
    m_lruList.push_front(key);

    CacheNode node;
    node.entry.key = key;
    node.entry.pipelineHandle = pipelineHandle;
    node.entry.layoutHandle = layoutHandle;
    node.entry.type = key.type;
    node.entry.creationTimeNs = compilationTimeNs;
    node.entry.frameLastUsed = 0;
    node.entry.useCount = 1;
    node.entry.debugName = debugName;
    node.lruIt = m_lruList.begin();

    m_cache[key] = std::move(node);
    m_insertions++;

    if (m_config.trackCompilationTime) {
        m_totalCompileTime += compilationTimeNs;
    }

    u32 count = static_cast<u32>(m_cache.size());
    if (count > m_peakEntries) m_peakEntries = count;
}

u64 PSODedupCache::LookupOrReserve(const PSOKey& key, bool& wasFound) {
    u64 handle = Lookup(key);
    wasFound = (handle != 0);
    return handle;
}

bool PSODedupCache::Contains(const PSOKey& key) const {
    std::lock_guard lock(m_mutex);
    return m_cache.find(key) != m_cache.end();
}

bool PSODedupCache::Remove(const PSOKey& key) {
    std::lock_guard lock(m_mutex);

    auto it = m_cache.find(key);
    if (it == m_cache.end()) return false;

    m_lruList.erase(it->second.lruIt);
    m_cache.erase(it);
    return true;
}

void PSODedupCache::Touch(const PSOKey& key, u32 currentFrame) {
    std::lock_guard lock(m_mutex);

    auto it = m_cache.find(key);
    if (it == m_cache.end()) return;

    it->second.entry.frameLastUsed = currentFrame;

    if (m_config.enableLRU) {
        m_lruList.erase(it->second.lruIt);
        m_lruList.push_front(key);
        it->second.lruIt = m_lruList.begin();
    }
}

u32 PSODedupCache::EvictToCount(u32 targetCount) {
    std::lock_guard lock(m_mutex);

    u32 evicted = 0;
    while (m_cache.size() > targetCount && !m_lruList.empty()) {
        PSOKey lruKey = m_lruList.back();
        m_lruList.pop_back();

        auto it = m_cache.find(lruKey);
        if (it != m_cache.end()) {
            if (m_config.logEvictions) {
                NGE_LOG_DEBUG("PSO evicted: hash={:#x} name={}", lruKey.hash, it->second.entry.debugName);
            }
            m_cache.erase(it);
            m_evictions++;
            evicted++;
        }
    }

    return evicted;
}

u32 PSODedupCache::EvictOlderThan(u32 frameThreshold) {
    std::lock_guard lock(m_mutex);

    u32 evicted = 0;
    auto it = m_lruList.end();
    while (it != m_lruList.begin()) {
        --it;
        auto cacheIt = m_cache.find(*it);
        if (cacheIt != m_cache.end() && cacheIt->second.entry.frameLastUsed < frameThreshold) {
            if (m_config.logEvictions) {
                NGE_LOG_DEBUG("PSO evicted (stale): hash={:#x} lastFrame={}",
                              it->hash, cacheIt->second.entry.frameLastUsed);
            }
            m_cache.erase(cacheIt);
            it = m_lruList.erase(it);
            m_evictions++;
            evicted++;
        }
    }

    return evicted;
}

const PSOEntry* PSODedupCache::GetEntry(const PSOKey& key) const {
    std::lock_guard lock(m_mutex);

    auto it = m_cache.find(key);
    if (it == m_cache.end()) return nullptr;
    return &it->second.entry;
}

u32 PSODedupCache::GetCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_cache.size());
}

void PSODedupCache::Clear() {
    std::lock_guard lock(m_mutex);
    m_cache.clear();
    m_lruList.clear();
}

void PSODedupCache::Reset() {
    std::lock_guard lock(m_mutex);
    m_cache.clear();
    m_lruList.clear();
    m_totalLookups = 0;
    m_cacheHits = 0;
    m_cacheMisses = 0;
    m_evictions = 0;
    m_insertions = 0;
    m_peakEntries = 0;
    m_totalCompileTime = 0;
    m_savedCompileTime = 0;
}

PSODedupCacheStats PSODedupCache::GetStats() const {
    std::lock_guard lock(m_mutex);
    PSODedupCacheStats stats{};
    stats.totalEntries = static_cast<u32>(m_cache.size());
    stats.totalLookups = m_totalLookups;
    stats.cacheHits = m_cacheHits;
    stats.cacheMisses = m_cacheMisses;
    stats.evictions = m_evictions;
    stats.insertions = m_insertions;
    stats.hitRate = m_totalLookups > 0 ? static_cast<float>(m_cacheHits) / m_totalLookups : 0.0f;
    stats.totalCompilationTimeNs = m_totalCompileTime;
    stats.savedCompilationTimeNs = m_savedCompileTime;
    stats.peakEntries = m_peakEntries;
    return stats;
}

void PSODedupCache::EvictLRU() {
    if (m_lruList.empty()) return;

    PSOKey lruKey = m_lruList.back();
    m_lruList.pop_back();

    auto it = m_cache.find(lruKey);
    if (it != m_cache.end()) {
        if (m_config.logEvictions) {
            NGE_LOG_DEBUG("PSO LRU evicted: hash={:#x} name={}", lruKey.hash, it->second.entry.debugName);
        }
        m_cache.erase(it);
        m_evictions++;
    }
}

void PSODedupCache::MoveToFront(const PSOKey& key) {
    auto it = m_cache.find(key);
    if (it == m_cache.end()) return;

    m_lruList.erase(it->second.lruIt);
    m_lruList.push_front(key);
    it->second.lruIt = m_lruList.begin();
}

} // namespace nge::rhi
