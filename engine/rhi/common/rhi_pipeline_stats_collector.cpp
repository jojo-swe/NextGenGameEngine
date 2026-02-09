#include "engine/rhi/common/rhi_pipeline_stats_collector.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool PipelineStatsCollector::Init(const PipelineStatsConfig& config) {
    m_config = config;
    m_nextQueryId = 0;
    m_queriesThisFrame = 0;
    m_lastPixelCount = 0;

    // TODO: Create VkQueryPool with VK_QUERY_TYPE_PIPELINE_STATISTICS
    // VkQueryPoolCreateInfo poolInfo{};
    // poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    // poolInfo.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
    // poolInfo.queryCount = config.maxQueriesPerFrame;
    // poolInfo.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT | ...;

    NGE_LOG_INFO("Pipeline stats collector initialized: maxPasses={}, maxQueries={}, history={}",
                 config.maxPasses, config.maxQueriesPerFrame, config.historyFrames);
    return true;
}

void PipelineStatsCollector::Shutdown() {
    m_activeQueries.clear();
    m_passStats.clear();
}

u32 PipelineStatsCollector::BeginPass(const std::string& passName) {
    std::lock_guard lock(m_mutex);

    if (m_queriesThisFrame >= m_config.maxQueriesPerFrame) {
        NGE_LOG_WARN("Pipeline stats: max queries per frame exceeded ({})", m_config.maxQueriesPerFrame);
        return UINT32_MAX;
    }

    u32 queryId = m_nextQueryId++;
    m_queriesThisFrame++;

    ActiveQuery query;
    query.passName = passName;
    query.hasResults = false;
    query.results = {};
    m_activeQueries[queryId] = std::move(query);

    // TODO: vkCmdBeginQuery(cmdBuffer, queryPool, queryId, 0);

    return queryId;
}

void PipelineStatsCollector::EndPass(u32 queryId) {
    std::lock_guard lock(m_mutex);

    if (queryId == UINT32_MAX) return;

    // TODO: vkCmdEndQuery(cmdBuffer, queryPool, queryId);
}

void PipelineStatsCollector::SubmitResults(u32 queryId, const PipelineStatistics& stats) {
    std::lock_guard lock(m_mutex);

    auto it = m_activeQueries.find(queryId);
    if (it == m_activeQueries.end()) return;

    it->second.hasResults = true;
    it->second.results = stats;
}

void PipelineStatsCollector::EndFrame(u32 screenPixelCount) {
    std::lock_guard lock(m_mutex);

    m_lastPixelCount = screenPixelCount;

    // Process all completed queries
    for (auto& [queryId, query] : m_activeQueries) {
        if (!query.hasResults) continue;

        auto& pass = m_passStats[query.passName];
        pass.passName = query.passName;
        pass.current = query.results;
        pass.sampleCount++;

        // Accumulate
        auto& acc = pass.accumulated;
        acc.inputAssemblyVertices += query.results.inputAssemblyVertices;
        acc.inputAssemblyPrimitives += query.results.inputAssemblyPrimitives;
        acc.vertexShaderInvocations += query.results.vertexShaderInvocations;
        acc.geometryShaderInvocations += query.results.geometryShaderInvocations;
        acc.geometryShaderPrimitives += query.results.geometryShaderPrimitives;
        acc.clippingInvocations += query.results.clippingInvocations;
        acc.clippingPrimitives += query.results.clippingPrimitives;
        acc.fragmentShaderInvocations += query.results.fragmentShaderInvocations;
        acc.tessControlPatches += query.results.tessControlPatches;
        acc.tessEvalInvocations += query.results.tessEvalInvocations;
        acc.computeShaderInvocations += query.results.computeShaderInvocations;
        acc.meshShaderInvocations += query.results.meshShaderInvocations;
        acc.taskShaderInvocations += query.results.taskShaderInvocations;

        // Overdraw ratio
        if (screenPixelCount > 0) {
            pass.overdrawRatio = static_cast<f32>(query.results.fragmentShaderInvocations) /
                                  static_cast<f32>(screenPixelCount);
        }

        // Geometry amplification
        if (query.results.inputAssemblyPrimitives > 0) {
            pass.geometryAmplification = static_cast<f32>(query.results.clippingPrimitives) /
                                          static_cast<f32>(query.results.inputAssemblyPrimitives);
        }
    }

    m_activeQueries.clear();
    m_queriesThisFrame = 0;
}

const PassStatistics* PipelineStatsCollector::GetPassStats(const std::string& passName) const {
    std::lock_guard lock(m_mutex);
    auto it = m_passStats.find(passName);
    if (it != m_passStats.end()) return &it->second;
    return nullptr;
}

std::vector<PassStatistics> PipelineStatsCollector::GetAllPassStats() const {
    std::lock_guard lock(m_mutex);
    std::vector<PassStatistics> result;
    result.reserve(m_passStats.size());
    for (const auto& [name, stats] : m_passStats) {
        result.push_back(stats);
    }
    return result;
}

std::string PipelineStatsCollector::GetWorstOverdrawPass() const {
    std::lock_guard lock(m_mutex);
    std::string worst;
    f32 maxOverdraw = 0.0f;
    for (const auto& [name, stats] : m_passStats) {
        if (stats.overdrawRatio > maxOverdraw) {
            maxOverdraw = stats.overdrawRatio;
            worst = name;
        }
    }
    return worst;
}

void PipelineStatsCollector::ResetAccumulators() {
    std::lock_guard lock(m_mutex);
    for (auto& [name, stats] : m_passStats) {
        stats.accumulated = {};
        stats.sampleCount = 0;
    }
}

PipelineStatsCollectorStats PipelineStatsCollector::GetStats() const {
    std::lock_guard lock(m_mutex);
    PipelineStatsCollectorStats stats{};
    stats.activePasses = static_cast<u32>(m_passStats.size());
    stats.totalQueriesThisFrame = m_queriesThisFrame;

    f32 totalOverdraw = 0.0f;
    f32 maxOD = 0.0f;
    u64 totalFrag = 0;
    u64 totalVert = 0;

    for (const auto& [name, pass] : m_passStats) {
        totalOverdraw += pass.overdrawRatio;
        maxOD = std::max(maxOD, pass.overdrawRatio);
        totalFrag += pass.current.fragmentShaderInvocations;
        totalVert += pass.current.vertexShaderInvocations;
    }

    stats.averageOverdraw = m_passStats.empty() ? 0.0f :
        totalOverdraw / static_cast<f32>(m_passStats.size());
    stats.maxOverdraw = maxOD;
    stats.totalFragmentInvocations = totalFrag;
    stats.totalVertexInvocations = totalVert;

    return stats;
}

} // namespace nge::rhi
