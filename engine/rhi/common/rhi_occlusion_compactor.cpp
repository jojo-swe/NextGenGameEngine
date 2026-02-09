#include "engine/rhi/common/rhi_occlusion_compactor.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool OcclusionCompactor::Init(IDevice* device, const OcclusionCompactorConfig& config) {
    m_device = device;
    m_config = config;

    m_instances.reserve(config.maxInstances);
    m_visibleIds.reserve(config.maxInstances);

    NGE_LOG_INFO("Occlusion compactor initialized: maxInstances={}, threshold={}, conservative={}",
                 config.maxInstances, config.occlusionThreshold, config.conservativeMode);
    return true;
}

void OcclusionCompactor::Shutdown() {
    m_instances.clear();
    m_visibleIds.clear();
}

void OcclusionCompactor::SubmitResults(const OcclusionInstance* instances, u32 count) {
    std::lock_guard lock(m_mutex);

    u32 submitCount = std::min(count, m_config.maxInstances);
    m_instances.resize(submitCount);

    for (u32 i = 0; i < submitCount; ++i) {
        m_instances[i] = instances[i];
    }
}

CompactedResult OcclusionCompactor::Compact() {
    std::lock_guard lock(m_mutex);

    CompactedResult result{};
    m_visibleIds.clear();
    m_queryNotReady = 0;

    for (const auto& inst : m_instances) {
        bool isVisible;

        if (inst.queryResult == UINT64_MAX) {
            // Query not ready yet
            m_queryNotReady++;
            isVisible = m_config.conservativeMode; // Assume visible if conservative
        } else {
            isVisible = inst.queryResult > m_config.occlusionThreshold;
        }

        if (isVisible) {
            m_visibleIds.push_back(inst.instanceId);
            result.visibleCount++;
        } else {
            result.occludedCount++;
        }
    }

    result.visibleInstanceIds = m_visibleIds;

    return result;
}

void OcclusionCompactor::BuildIndirectArgs(CompactedResult& result, u32 indexCountPerInstance,
                                            u32 firstIndex, i32 vertexOffset) {
    result.indirectIndexCount = indexCountPerInstance;
    result.indirectInstanceCount = result.visibleCount;
    result.indirectFirstIndex = firstIndex;
    result.indirectVertexOffset = vertexOffset;
    result.indirectFirstInstance = 0;
}

const std::vector<u32>& OcclusionCompactor::GetVisibleIds() const {
    return m_visibleIds;
}

void OcclusionCompactor::Reset() {
    std::lock_guard lock(m_mutex);
    m_instances.clear();
    m_visibleIds.clear();
    m_queryNotReady = 0;
}

OcclusionCompactorStats OcclusionCompactor::GetStats() const {
    std::lock_guard lock(m_mutex);
    OcclusionCompactorStats stats{};
    stats.totalInstances = static_cast<u32>(m_instances.size());
    stats.visibleInstances = static_cast<u32>(m_visibleIds.size());
    stats.occludedInstances = stats.totalInstances - stats.visibleInstances;
    stats.occlusionRate = stats.totalInstances > 0 ?
        static_cast<f32>(stats.occludedInstances) / static_cast<f32>(stats.totalInstances) * 100.0f : 0.0f;
    stats.queryNotReadyCount = m_queryNotReady;
    return stats;
}

} // namespace nge::rhi
