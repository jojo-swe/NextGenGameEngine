#include "engine/rhi/common/rhi_query_readback.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <numeric>

namespace nge::rhi {

bool QueryReadbackManager::Init(IDevice* device, const QueryReadbackConfig& config) {
    m_device = device;
    m_config = config;
    m_currentFrame = 0;
    m_readbacksCompleted = 0;
    m_frameNumber = 0;

    m_frames.resize(config.framesInFlight);

    // TODO: Create query heap and readback buffer
    // VkQueryPoolCreateInfo ci{};
    // ci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    // ci.queryCount = maxQueryGroups * maxQueriesPerGroup;
    // vkCreateQueryPool(device, &ci, nullptr, &queryPool);

    NGE_LOG_INFO("Query readback manager initialized: {} frames, {} max groups, window={}",
                 config.framesInFlight, config.maxQueryGroups, config.aggregationWindowFrames);
    return true;
}

void QueryReadbackManager::Shutdown() {
    // TODO: vkDestroyQueryPool
    m_groups.clear();
    m_frames.clear();
    m_callbacks.clear();
}

u32 QueryReadbackManager::RegisterGroup(const QueryGroupDesc& desc) {
    std::lock_guard lock(m_mutex);

    u32 id = static_cast<u32>(m_groups.size());
    QueryGroup group;
    group.desc = desc;

    // Calculate heap offset
    u32 offset = 0;
    for (const auto& g : m_groups) {
        offset += g.desc.queryCount;
    }
    group.heapOffset = offset;
    group.rawResults.resize(desc.queryCount, 0);
    group.aggregationWindow.resize(desc.queryCount);

    m_groups.push_back(std::move(group));

    NGE_LOG_INFO("Query group '{}' registered: type={}, count={}, heapOffset={}",
                 desc.name, static_cast<u32>(desc.type), desc.queryCount, offset);
    return id;
}

void QueryReadbackManager::BeginQuery(ICommandList* cmd, u32 groupId, u32 queryIndex) {
    if (groupId >= m_groups.size()) return;
    const auto& group = m_groups[groupId];
    if (queryIndex >= group.desc.queryCount) return;

    // TODO: vkCmdBeginQuery(cmd, queryPool, group.heapOffset + queryIndex, 0);
    (void)cmd;
}

void QueryReadbackManager::EndQuery(ICommandList* cmd, u32 groupId, u32 queryIndex) {
    if (groupId >= m_groups.size()) return;
    const auto& group = m_groups[groupId];
    if (queryIndex >= group.desc.queryCount) return;

    // TODO: vkCmdEndQuery(cmd, queryPool, group.heapOffset + queryIndex);
    (void)cmd;
}

void QueryReadbackManager::WriteTimestamp(ICommandList* cmd, u32 groupId, u32 queryIndex) {
    if (groupId >= m_groups.size()) return;
    const auto& group = m_groups[groupId];
    if (queryIndex >= group.desc.queryCount) return;

    // TODO: vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    //                            queryPool, group.heapOffset + queryIndex);
    (void)cmd;
}

void QueryReadbackManager::ResolveQueries(ICommandList* cmd) {
    std::lock_guard lock(m_mutex);

    for (const auto& group : m_groups) {
        // TODO: vkCmdCopyQueryPoolResults(cmd, queryPool,
        //     group.heapOffset, group.desc.queryCount,
        //     readbackBuffer, bufferOffset, stride,
        //     VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    }

    if (m_currentFrame < m_frames.size()) {
        m_frames[m_currentFrame].resolved = true;
    }
    (void)cmd;
}

void QueryReadbackManager::ReadbackResults(u64 frameNumber) {
    std::lock_guard lock(m_mutex);
    m_frameNumber = frameNumber;

    // Read from the frame that was resolved N frames ago
    u32 readbackFrame = (m_currentFrame + 1) % m_config.framesInFlight;
    if (readbackFrame >= m_frames.size()) return;
    if (!m_frames[readbackFrame].resolved) return;

    // TODO: Map readback buffer and copy results
    // void* mappedData;
    // vkMapMemory(device, readbackMemory, offset, size, 0, &mappedData);
    // memcpy results to m_groups[*].rawResults
    // vkUnmapMemory(device, readbackMemory);

    // Process results and update aggregation windows
    for (auto& group : m_groups) {
        for (u32 q = 0; q < group.desc.queryCount; ++q) {
            if (group.desc.type == QueryType::Timestamp && q % 2 == 0 && q + 1 < group.desc.queryCount) {
                u64 start = group.rawResults[q];
                u64 end = group.rawResults[q + 1];
                f64 durationMs = static_cast<f64>(end - start) * m_config.timestampPeriodNs / 1000000.0;

                // Update aggregation window
                auto& window = group.aggregationWindow[q];
                window.push_back(durationMs);
                if (window.size() > m_config.aggregationWindowFrames) {
                    window.erase(window.begin());
                }

                // Fire callbacks
                for (const auto& cb : m_callbacks) {
                    cb(group.desc.name, q, durationMs);
                }
            }
        }
    }

    m_frames[readbackFrame].resolved = false;
    m_readbacksCompleted++;
}

f64 QueryReadbackManager::GetTimestampMs(u32 groupId, u32 startQuery, u32 endQuery) const {
    std::lock_guard lock(m_mutex);
    if (groupId >= m_groups.size()) return 0.0;
    const auto& group = m_groups[groupId];
    if (startQuery >= group.desc.queryCount || endQuery >= group.desc.queryCount) return 0.0;

    u64 start = group.rawResults[startQuery];
    u64 end = group.rawResults[endQuery];
    return static_cast<f64>(end - start) * m_config.timestampPeriodNs / 1000000.0;
}

OcclusionResult QueryReadbackManager::GetOcclusionResult(u32 groupId, u32 queryIndex) const {
    std::lock_guard lock(m_mutex);
    OcclusionResult result{};
    if (groupId >= m_groups.size()) return result;
    const auto& group = m_groups[groupId];
    if (queryIndex >= group.desc.queryCount) return result;

    result.samplesPassed = group.rawResults[queryIndex];
    result.visible = result.samplesPassed > 0;
    return result;
}

PipelineStatsResult QueryReadbackManager::GetPipelineStats(u32 groupId, u32 queryIndex) const {
    std::lock_guard lock(m_mutex);
    PipelineStatsResult result{};
    if (groupId >= m_groups.size()) return result;
    // Pipeline stats occupy multiple u64 values per query
    // Would need to parse from raw results based on pipeline statistics flags
    (void)queryIndex;
    return result;
}

QueryAggregation QueryReadbackManager::GetAggregation(u32 groupId, u32 queryIndex) const {
    std::lock_guard lock(m_mutex);
    QueryAggregation agg{};
    if (groupId >= m_groups.size()) return agg;
    const auto& group = m_groups[groupId];
    if (queryIndex >= group.aggregationWindow.size()) return agg;

    const auto& window = group.aggregationWindow[queryIndex];
    if (window.empty()) return agg;

    agg.sampleCount = static_cast<u32>(window.size());
    agg.minMs = *std::min_element(window.begin(), window.end());
    agg.maxMs = *std::max_element(window.begin(), window.end());
    agg.avgMs = std::accumulate(window.begin(), window.end(), 0.0) / window.size();
    return agg;
}

void QueryReadbackManager::OnResult(QueryResultCallback callback) {
    std::lock_guard lock(m_mutex);
    m_callbacks.push_back(std::move(callback));
}

void QueryReadbackManager::BeginFrame(u64 frameNumber) {
    m_frameNumber = frameNumber;
    m_currentFrame = static_cast<u32>(frameNumber % m_config.framesInFlight);

    // Reset queries for this frame
    // TODO: vkCmdResetQueryPool(cmd, queryPool, frameOffset, frameQueryCount);
}

void QueryReadbackManager::EndFrame() {
    // Nothing needed — resolve is called explicitly
}

QueryReadbackStats QueryReadbackManager::GetStats() const {
    std::lock_guard lock(m_mutex);
    QueryReadbackStats stats{};
    stats.activeGroups = static_cast<u32>(m_groups.size());
    u32 totalQueries = 0;
    u32 pending = 0;
    for (const auto& g : m_groups) {
        totalQueries += g.desc.queryCount;
    }
    for (const auto& f : m_frames) {
        if (f.resolved) pending++;
    }
    stats.totalQueries = totalQueries;
    stats.readbacksPending = pending;
    stats.readbacksCompleted = m_readbacksCompleted;
    return stats;
}

} // namespace nge::rhi
