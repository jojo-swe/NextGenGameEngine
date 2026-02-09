#include "engine/rhi/common/rhi_resource_version_tracker.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool ResourceVersionTracker::Init(const ResourceVersionConfig& config) {
    m_config = config;
    m_entries.reserve(config.maxResources);
    m_totalCreated = 0;
    m_totalDestroyed = 0;
    m_staleAccesses = 0;
    m_maxGeneration = 0;

    NGE_LOG_INFO("Resource version tracker initialized: maxResources={}, historySize={}",
                 config.maxResources, config.destroyedHistorySize);
    return true;
}

void ResourceVersionTracker::Shutdown() {
    m_entries.clear();
    m_destroyedHistory.clear();
}

VersionedHandle ResourceVersionTracker::Register(u64 handle, const std::string& debugName) {
    std::lock_guard lock(m_mutex);

    ResourceVersionEntry entry;
    entry.handle = handle;
    entry.currentGeneration = 1;
    entry.debugName = debugName;
    entry.alive = true;

    m_entries[handle] = std::move(entry);
    m_totalCreated++;

    if (1 > m_maxGeneration) m_maxGeneration = 1;

    return {handle, 1};
}

void ResourceVersionTracker::Destroy(u64 handle) {
    std::lock_guard lock(m_mutex);

    auto it = m_entries.find(handle);
    if (it == m_entries.end()) return;

    it->second.alive = false;
    it->second.currentGeneration++;
    m_totalDestroyed++;

    if (it->second.currentGeneration > m_maxGeneration) {
        m_maxGeneration = it->second.currentGeneration;
    }

    if (m_config.trackDestroyedHistory) {
        m_destroyedHistory.push_back(it->second.debugName + " (gen " +
                                      std::to_string(it->second.currentGeneration - 1) + ")");
        if (m_destroyedHistory.size() > m_config.destroyedHistorySize) {
            m_destroyedHistory.erase(m_destroyedHistory.begin());
        }
    }
}

VersionedHandle ResourceVersionTracker::Reregister(u64 handle, const std::string& debugName) {
    std::lock_guard lock(m_mutex);

    auto it = m_entries.find(handle);
    if (it != m_entries.end()) {
        it->second.currentGeneration++;
        it->second.debugName = debugName;
        it->second.alive = true;
        m_totalCreated++;

        if (it->second.currentGeneration > m_maxGeneration) {
            m_maxGeneration = it->second.currentGeneration;
        }

        return {handle, it->second.currentGeneration};
    }

    // Handle not previously tracked — register fresh
    ResourceVersionEntry entry;
    entry.handle = handle;
    entry.currentGeneration = 1;
    entry.debugName = debugName;
    entry.alive = true;

    m_entries[handle] = std::move(entry);
    m_totalCreated++;

    return {handle, 1};
}

bool ResourceVersionTracker::IsValid(const VersionedHandle& vh) const {
    std::lock_guard lock(m_mutex);

    auto it = m_entries.find(vh.handle);
    if (it == m_entries.end()) return false;

    return it->second.alive && it->second.currentGeneration == vh.generation;
}

u32 ResourceVersionTracker::GetGeneration(u64 handle) const {
    std::lock_guard lock(m_mutex);

    auto it = m_entries.find(handle);
    if (it != m_entries.end()) return it->second.currentGeneration;
    return 0;
}

bool ResourceVersionTracker::IsAlive(u64 handle) const {
    std::lock_guard lock(m_mutex);

    auto it = m_entries.find(handle);
    if (it != m_entries.end()) return it->second.alive;
    return false;
}

std::string ResourceVersionTracker::GetDebugName(u64 handle) const {
    std::lock_guard lock(m_mutex);

    auto it = m_entries.find(handle);
    if (it != m_entries.end()) return it->second.debugName;
    return "";
}

void ResourceVersionTracker::RecordStaleAccess(const VersionedHandle& vh) {
    std::lock_guard lock(m_mutex);
    m_staleAccesses++;

    auto it = m_entries.find(vh.handle);
    std::string name = (it != m_entries.end()) ? it->second.debugName : "unknown";
    u32 currentGen = (it != m_entries.end()) ? it->second.currentGeneration : 0;

    NGE_LOG_WARN("Stale resource access: handle={}, held_gen={}, current_gen={}, name='{}'",
                 vh.handle, vh.generation, currentGen, name);
}

std::vector<std::string> ResourceVersionTracker::GetDestroyedHistory() const {
    std::lock_guard lock(m_mutex);
    return m_destroyedHistory;
}

void ResourceVersionTracker::Reset() {
    std::lock_guard lock(m_mutex);
    m_entries.clear();
    m_destroyedHistory.clear();
    m_totalCreated = 0;
    m_totalDestroyed = 0;
    m_staleAccesses = 0;
    m_maxGeneration = 0;
}

ResourceVersionStats ResourceVersionTracker::GetStats() const {
    std::lock_guard lock(m_mutex);
    ResourceVersionStats stats{};

    u32 alive = 0;
    for (const auto& [handle, entry] : m_entries) {
        if (entry.alive) alive++;
    }

    stats.aliveResources = alive;
    stats.totalCreated = m_totalCreated;
    stats.totalDestroyed = m_totalDestroyed;
    stats.staleAccessesDetected = m_staleAccesses;
    stats.maxGeneration = m_maxGeneration;

    return stats;
}

} // namespace nge::rhi
