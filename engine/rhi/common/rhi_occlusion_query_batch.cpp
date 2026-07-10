#include "engine/rhi/common/rhi_occlusion_query_batch.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool OcclusionQueryBatchManager::Init(const OcclusionQueryBatchConfig& config) {
    m_config = config;
    m_slots.resize(config.poolSize);

    for (u32 i = 0; i < config.poolSize; ++i) {
        m_slots[i].slotId = i;
        m_slots[i].objectId = UINT32_MAX;
        m_slots[i].state = QueryState::Free;
        m_slots[i].samplesPassed = 0;
        m_slots[i].issuedFrame = 0;
        m_slots[i].resultFrame = 0;
        m_slots[i].visible = config.conservativeDefault;
    }

    m_queriesThisFrame = 0;
    m_resultsThisFrame = 0;
    m_totalIssued = 0;
    m_totalResults = 0;
    m_visibleCount = 0;
    m_occludedCount = 0;

    NGE_LOG_INFO("Occlusion query batch manager initialized: poolSize={}, maxPerFrame={}, latency={} frames",
                 config.poolSize, config.maxQueriesPerFrame, config.latencyFrames);
    return true;
}

void OcclusionQueryBatchManager::Shutdown() {
    m_slots.clear();
    m_objectToSlot.clear();
}

u32 OcclusionQueryBatchManager::AllocateQuery(u32 objectId) {
    std::lock_guard lock(m_mutex);

    // Check if object already has a slot
    auto it = m_objectToSlot.find(objectId);
    if (it != m_objectToSlot.end()) {
        return it->second;
    }

    // Find a free slot
    for (u32 i = 0; i < static_cast<u32>(m_slots.size()); ++i) {
        if (m_slots[i].state == QueryState::Free) {
            m_slots[i].objectId = objectId;
            m_slots[i].state = QueryState::Free; // Will become Pending on MarkIssued
            m_slots[i].samplesPassed = 0;
            m_slots[i].visible = m_config.conservativeDefault;
            m_objectToSlot[objectId] = i;
            return i;
        }
    }

    NGE_LOG_WARN("Occlusion query batch: no free slots available");
    return UINT32_MAX;
}

void OcclusionQueryBatchManager::MarkIssued(u32 slotId, u32 currentFrame) {
    std::lock_guard lock(m_mutex);

    if (slotId >= m_slots.size()) return;

    auto& slot = m_slots[slotId];
    slot.state = QueryState::Pending;
    slot.issuedFrame = currentFrame;

    m_queriesThisFrame++;
    m_totalIssued++;
}

void OcclusionQueryBatchManager::SubmitResult(u32 slotId, u64 samplesPassed, u32 currentFrame) {
    std::lock_guard lock(m_mutex);

    if (slotId >= m_slots.size()) return;

    auto& slot = m_slots[slotId];
    slot.state = QueryState::ResultReady;
    slot.samplesPassed = samplesPassed;
    slot.resultFrame = currentFrame;
    slot.visible = (samplesPassed > m_config.visibilityThreshold);

    if (slot.visible) m_visibleCount++;
    else m_occludedCount++;

    m_resultsThisFrame++;
    m_totalResults++;
}

bool OcclusionQueryBatchManager::IsVisible(u32 objectId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_objectToSlot.find(objectId);
    if (it == m_objectToSlot.end()) return m_config.conservativeDefault;

    u32 slotId = it->second;
    if (slotId >= m_slots.size()) return m_config.conservativeDefault;

    const auto& slot = m_slots[slotId];

    if (slot.state == QueryState::ResultReady) {
        return slot.visible;
    }

    // No result yet: use conservative default
    return m_config.conservativeDefault;
}

u64 OcclusionQueryBatchManager::GetSamplesPassed(u32 objectId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_objectToSlot.find(objectId);
    if (it == m_objectToSlot.end()) return 0;

    u32 slotId = it->second;
    if (slotId >= m_slots.size()) return 0;

    return m_slots[slotId].samplesPassed;
}

void OcclusionQueryBatchManager::FreeQuery(u32 slotId) {
    std::lock_guard lock(m_mutex);

    if (slotId >= m_slots.size()) return;

    auto& slot = m_slots[slotId];
    u32 objectId = slot.objectId;

    slot.state = QueryState::Free;
    slot.objectId = UINT32_MAX;
    slot.samplesPassed = 0;
    slot.visible = m_config.conservativeDefault;

    m_objectToSlot.erase(objectId);
}

void OcclusionQueryBatchManager::FreeObjectQueries(u32 objectId) {
    std::lock_guard lock(m_mutex);

    auto it = m_objectToSlot.find(objectId);
    if (it == m_objectToSlot.end()) return;

    u32 slotId = it->second;
    if (slotId < m_slots.size()) {
        m_slots[slotId].state = QueryState::Free;
        m_slots[slotId].objectId = UINT32_MAX;
        m_slots[slotId].samplesPassed = 0;
        m_slots[slotId].visible = m_config.conservativeDefault;
    }

    m_objectToSlot.erase(it);
}

void OcclusionQueryBatchManager::ProcessFrame([[maybe_unused]] u32 currentFrame) {
    std::lock_guard lock(m_mutex);

    // Reset per-frame counters
    m_queriesThisFrame = 0;
    m_resultsThisFrame = 0;
}

const OcclusionQuerySlot* OcclusionQueryBatchManager::GetSlot(u32 slotId) const {
    std::lock_guard lock(m_mutex);

    if (slotId >= m_slots.size()) return nullptr;
    return &m_slots[slotId];
}

u32 OcclusionQueryBatchManager::FindSlotForObject(u32 objectId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_objectToSlot.find(objectId);
    if (it == m_objectToSlot.end()) return UINT32_MAX;
    return it->second;
}

u32 OcclusionQueryBatchManager::GetAllocatedCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_objectToSlot.size());
}

u32 OcclusionQueryBatchManager::GetFreeCount() const {
    std::lock_guard lock(m_mutex);

    u32 free = 0;
    for (const auto& slot : m_slots) {
        if (slot.state == QueryState::Free && slot.objectId == UINT32_MAX) free++;
    }
    return free;
}

void OcclusionQueryBatchManager::Reset() {
    std::lock_guard lock(m_mutex);

    for (auto& slot : m_slots) {
        slot.objectId = UINT32_MAX;
        slot.state = QueryState::Free;
        slot.samplesPassed = 0;
        slot.visible = m_config.conservativeDefault;
    }

    m_objectToSlot.clear();
    m_queriesThisFrame = 0;
    m_resultsThisFrame = 0;
    m_totalIssued = 0;
    m_totalResults = 0;
    m_visibleCount = 0;
    m_occludedCount = 0;
}

OcclusionQueryBatchStats OcclusionQueryBatchManager::GetStats() const {
    std::lock_guard lock(m_mutex);

    OcclusionQueryBatchStats stats{};
    stats.totalSlots = static_cast<u32>(m_slots.size());
    stats.slotsInUse = static_cast<u32>(m_objectToSlot.size());
    stats.slotsFree = stats.totalSlots - stats.slotsInUse;
    stats.queriesIssuedThisFrame = m_queriesThisFrame;
    stats.resultsReadThisFrame = m_resultsThisFrame;
    stats.visibleObjects = m_visibleCount;
    stats.occludedObjects = m_occludedCount;
    stats.totalQueriesIssued = m_totalIssued;
    stats.totalResultsRead = m_totalResults;

    u32 totalDecided = m_visibleCount + m_occludedCount;
    stats.occlusionRatio = totalDecided > 0
        ? static_cast<float>(m_occludedCount) / static_cast<float>(totalDecided)
        : 0.0f;

    return stats;
}

} // namespace nge::rhi
