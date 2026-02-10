#include "engine/rhi/common/rhi_desc_set_version_tracker.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool DescriptorSetVersionTracker::Init(const DescSetVersionConfig& config) {
    m_config = config;
    m_nextId = 0;
    m_totalUpdates = 0;
    m_updatesAvoided = 0;
    m_staleDetections = 0;
    m_totalBindingUpdates = 0;
    m_bindingUpdatesAvoided = 0;

    NGE_LOG_INFO("Descriptor set version tracker initialized: maxSets={}, maxBindings={}, perBinding={}",
                 config.maxSets, config.maxBindingsPerSet, config.trackPerBinding);
    return true;
}

void DescriptorSetVersionTracker::Shutdown() {
    m_sets.clear();
}

u32 DescriptorSetVersionTracker::RegisterSet(u32 bindingCount, const std::string& name) {
    std::lock_guard lock(m_mutex);

    if (m_sets.size() >= m_config.maxSets) {
        NGE_LOG_ERROR("Desc set version tracker: max sets reached ({})", m_config.maxSets);
        return UINT32_MAX;
    }

    u32 id = m_nextId++;

    TrackedSet ts;
    ts.info.setId = id;
    ts.info.currentVersion = 0;
    ts.info.lastUpdateFrame = 0;
    ts.info.bindingCount = bindingCount;
    ts.info.debugName = name;

    if (m_config.trackPerBinding) {
        ts.bindings.resize(bindingCount, {0, 0});
    }

    m_sets[id] = std::move(ts);
    return id;
}

void DescriptorSetVersionTracker::MarkUpdated(u32 setId, u32 frameIndex) {
    std::lock_guard lock(m_mutex);

    auto it = m_sets.find(setId);
    if (it == m_sets.end()) return;

    it->second.info.currentVersion++;
    it->second.info.lastUpdateFrame = frameIndex;
    m_totalUpdates++;
}

void DescriptorSetVersionTracker::MarkBindingUpdated(u32 setId, u32 bindingIndex, u64 contentHash) {
    std::lock_guard lock(m_mutex);

    auto it = m_sets.find(setId);
    if (it == m_sets.end()) return;

    auto& ts = it->second;

    if (!m_config.trackPerBinding || bindingIndex >= ts.bindings.size()) return;

    // Check if content actually changed
    if (ts.bindings[bindingIndex].contentHash == contentHash && contentHash != 0) {
        m_bindingUpdatesAvoided++;
        return;
    }

    ts.bindings[bindingIndex].version++;
    ts.bindings[bindingIndex].contentHash = contentHash;
    ts.info.currentVersion++; // Set-level version also bumps
    m_totalBindingUpdates++;
}

bool DescriptorSetVersionTracker::NeedsRebind(u32 setId, u64 consumerVersion) const {
    std::lock_guard lock(m_mutex);

    auto it = m_sets.find(setId);
    if (it == m_sets.end()) return true; // Unknown set -> rebind

    return consumerVersion < it->second.info.currentVersion;
}

bool DescriptorSetVersionTracker::BindingChanged(u32 setId, u32 bindingIndex, u64 consumerVersion) const {
    std::lock_guard lock(m_mutex);

    auto it = m_sets.find(setId);
    if (it == m_sets.end()) return true;

    const auto& ts = it->second;
    if (!m_config.trackPerBinding || bindingIndex >= ts.bindings.size()) return true;

    return consumerVersion < ts.bindings[bindingIndex].version;
}

u64 DescriptorSetVersionTracker::GetVersion(u32 setId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_sets.find(setId);
    if (it == m_sets.end()) return 0;

    return it->second.info.currentVersion;
}

u64 DescriptorSetVersionTracker::GetBindingVersion(u32 setId, u32 bindingIndex) const {
    std::lock_guard lock(m_mutex);

    auto it = m_sets.find(setId);
    if (it == m_sets.end()) return 0;

    const auto& ts = it->second;
    if (!m_config.trackPerBinding || bindingIndex >= ts.bindings.size()) return 0;

    return ts.bindings[bindingIndex].version;
}

u64 DescriptorSetVersionTracker::GetBindingContentHash(u32 setId, u32 bindingIndex) const {
    std::lock_guard lock(m_mutex);

    auto it = m_sets.find(setId);
    if (it == m_sets.end()) return 0;

    const auto& ts = it->second;
    if (!m_config.trackPerBinding || bindingIndex >= ts.bindings.size()) return 0;

    return ts.bindings[bindingIndex].contentHash;
}

void DescriptorSetVersionTracker::RecordConsume(u32 setId, u64 consumerVersion) {
    std::lock_guard lock(m_mutex);

    auto it = m_sets.find(setId);
    if (it == m_sets.end()) return;

    if (consumerVersion < it->second.info.currentVersion) {
        m_staleDetections++;
    } else {
        m_updatesAvoided++;
    }
}

const DescSetVersionInfo* DescriptorSetVersionTracker::GetSetInfo(u32 setId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_sets.find(setId);
    if (it == m_sets.end()) return nullptr;

    return &it->second.info;
}

void DescriptorSetVersionTracker::Unregister(u32 setId) {
    std::lock_guard lock(m_mutex);
    m_sets.erase(setId);
}

u32 DescriptorSetVersionTracker::GetSetCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_sets.size());
}

void DescriptorSetVersionTracker::Reset() {
    std::lock_guard lock(m_mutex);
    m_sets.clear();
    m_nextId = 0;
    m_totalUpdates = 0;
    m_updatesAvoided = 0;
    m_staleDetections = 0;
    m_totalBindingUpdates = 0;
    m_bindingUpdatesAvoided = 0;
}

DescSetVersionStats DescriptorSetVersionTracker::GetStats() const {
    std::lock_guard lock(m_mutex);

    DescSetVersionStats stats{};
    stats.totalSets = static_cast<u32>(m_sets.size());
    stats.totalUpdates = m_totalUpdates;
    stats.updatesAvoided = m_updatesAvoided;
    stats.staleDetections = m_staleDetections;
    stats.totalBindingUpdates = m_totalBindingUpdates;
    stats.bindingUpdatesAvoided = m_bindingUpdatesAvoided;

    u32 total = m_totalUpdates + m_updatesAvoided;
    stats.avoidanceRatio = total > 0
        ? static_cast<float>(m_updatesAvoided) / static_cast<float>(total)
        : 0.0f;

    return stats;
}

} // namespace nge::rhi
