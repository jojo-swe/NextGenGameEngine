#include "engine/rhi/common/rhi_query_heap.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool QueryHeapManager::Init(IDevice* device, const Config& config) {
    m_device = device;
    m_config = config;

    m_pools.resize(config.framesInFlight);
    m_frameQueries.resize(config.framesInFlight);

    for (u32 i = 0; i < config.framesInFlight; ++i) {
        // TODO: Create VkQueryPool for each type
        // Timestamp pool (2 queries per measurement: begin + end)
        // VkQueryPoolCreateInfo ci{};
        // ci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        // ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
        // ci.queryCount = config.maxTimestampQueries * 2;
        // vkCreateQueryPool(device, &ci, nullptr, &pool);

        m_pools[i].timestampPool = static_cast<u64>(i * 3 + 1);
        m_pools[i].occlusionPool = static_cast<u64>(i * 3 + 2);
        m_pools[i].pipelineStatPool = static_cast<u64>(i * 3 + 3);

        m_frameQueries[i].timestampEntries.reserve(config.maxTimestampQueries);
        m_frameQueries[i].occlusionEntries.reserve(config.maxOcclusionQueries);
    }

    NGE_LOG_INFO("Query heap initialized: {} timestamp, {} occlusion, {} pipeline stat queries, {} frames",
                 config.maxTimestampQueries, config.maxOcclusionQueries,
                 config.maxPipelineStatQueries, config.framesInFlight);
    return true;
}

void QueryHeapManager::Shutdown() {
    // TODO: vkDestroyQueryPool for all pools
    m_pools.clear();
    m_frameQueries.clear();
    m_timestampResults.clear();
    m_occlusionResults.clear();
}

void QueryHeapManager::BeginFrame(u32 frameIndex) {
    std::lock_guard lock(m_mutex);
    m_currentFrame = frameIndex % m_config.framesInFlight;

    // Read back results from the oldest frame (N-2)
    u32 readbackFrame = (m_currentFrame + 1) % m_config.framesInFlight;
    ReadbackResults(readbackFrame);

    // Reset current frame's queries
    auto& fq = m_frameQueries[m_currentFrame];
    fq.timestampCount = 0;
    fq.occlusionCount = 0;
    fq.pipelineStatCount = 0;
    fq.timestampEntries.clear();
    fq.occlusionEntries.clear();

    // TODO: vkCmdResetQueryPool for current frame pools
}

void QueryHeapManager::EndFrame(ICommandList* cmd) {
    (void)cmd;
    // Queries are already written during the frame via Begin/End calls
}

u32 QueryHeapManager::BeginTimestamp(ICommandList* cmd, const std::string& name) {
    std::lock_guard lock(m_mutex);
    auto& fq = m_frameQueries[m_currentFrame];

    if (fq.timestampCount >= m_config.maxTimestampQueries) {
        NGE_LOG_WARN("Timestamp query limit reached");
        return UINT32_MAX;
    }

    u32 queryId = fq.timestampCount++;
    u32 beginIndex = queryId * 2;

    // TODO: vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool, beginIndex);

    FrameQueries::TimestampEntry entry;
    entry.name = name;
    entry.beginIndex = beginIndex;
    entry.endIndex = beginIndex + 1;
    fq.timestampEntries.push_back(entry);

    (void)cmd;
    return queryId;
}

void QueryHeapManager::EndTimestamp(ICommandList* cmd, u32 queryId) {
    std::lock_guard lock(m_mutex);
    auto& fq = m_frameQueries[m_currentFrame];

    if (queryId >= fq.timestampCount) return;

    u32 endIndex = fq.timestampEntries[queryId].endIndex;
    // TODO: vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool, endIndex);

    (void)cmd;
    (void)endIndex;
}

u32 QueryHeapManager::BeginOcclusion(ICommandList* cmd, const std::string& name) {
    std::lock_guard lock(m_mutex);
    auto& fq = m_frameQueries[m_currentFrame];

    if (fq.occlusionCount >= m_config.maxOcclusionQueries) return UINT32_MAX;

    u32 queryId = fq.occlusionCount++;

    // TODO: vkCmdBeginQuery(cmd, occlusionPool, queryId, VK_QUERY_CONTROL_PRECISE_BIT);

    FrameQueries::OcclusionEntry entry;
    entry.name = name;
    entry.queryIndex = queryId;
    fq.occlusionEntries.push_back(entry);

    (void)cmd;
    return queryId;
}

void QueryHeapManager::EndOcclusion(ICommandList* cmd, u32 queryId) {
    // TODO: vkCmdEndQuery(cmd, occlusionPool, queryId);
    (void)cmd;
    (void)queryId;
}

u32 QueryHeapManager::BeginPipelineStats(ICommandList* cmd) {
    std::lock_guard lock(m_mutex);
    auto& fq = m_frameQueries[m_currentFrame];

    if (fq.pipelineStatCount >= m_config.maxPipelineStatQueries) return UINT32_MAX;

    u32 queryId = fq.pipelineStatCount++;
    // TODO: vkCmdBeginQuery(cmd, pipelineStatPool, queryId, 0);

    (void)cmd;
    return queryId;
}

void QueryHeapManager::EndPipelineStats(ICommandList* cmd, u32 queryId) {
    // TODO: vkCmdEndQuery(cmd, pipelineStatPool, queryId);
    (void)cmd;
    (void)queryId;
}

void QueryHeapManager::ReadbackResults(u32 frameIndex) {
    auto& fq = m_frameQueries[frameIndex];

    // Timestamp results
    m_timestampResults.clear();
    if (fq.timestampCount > 0) {
        // TODO: vkGetQueryPoolResults for timestamp pool
        // u64 timestamps[maxTimestampQueries * 2];
        // vkGetQueryPoolResults(device, pool, 0, count*2, sizeof(timestamps),
        //                       timestamps, sizeof(u64), VK_QUERY_RESULT_64_BIT);

        for (const auto& entry : fq.timestampEntries) {
            TimestampResult result;
            result.name = entry.name;
            result.beginTick = 0; // Would come from readback
            result.endTick = 0;
            result.durationMs = 0.0; // (endTick - beginTick) * m_timestampPeriod / 1e6
            m_timestampResults.push_back(result);
        }
    }

    // Occlusion results
    m_occlusionResults.clear();
    if (fq.occlusionCount > 0) {
        for (const auto& entry : fq.occlusionEntries) {
            OcclusionResult result;
            result.name = entry.name;
            result.samplesPassed = 0; // Would come from readback
            m_occlusionResults.push_back(result);
        }
    }

    // Pipeline stats
    m_pipelineStats = {};
    if (fq.pipelineStatCount > 0) {
        // TODO: vkGetQueryPoolResults for pipeline stats pool
    }
}

f64 QueryHeapManager::GetTimestampMs(const std::string& name) const {
    for (const auto& result : m_timestampResults) {
        if (result.name == name) return result.durationMs;
    }
    return -1.0;
}

} // namespace nge::rhi
