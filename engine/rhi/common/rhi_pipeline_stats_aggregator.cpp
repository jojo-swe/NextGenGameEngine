#include "engine/rhi/common/rhi_pipeline_stats_aggregator.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <cstring>

namespace nge::rhi {

bool PipelineStatsAggregator::Init(const PipelineStatsConfig& config) {
    m_config = config;
    m_totalFrames = 0;

    NGE_LOG_INFO("Pipeline stats aggregator initialized: maxPasses={}, history={} frames, minMax={}, bottleneck={}",
                 config.maxPasses, config.historyFrames, config.trackMinMax, config.detectBottlenecks);
    return true;
}

void PipelineStatsAggregator::Shutdown() {
    m_passHistories.clear();
}

void PipelineStatsAggregator::RecordPass(const std::string& passName, const PipelineStatistics& stats, u32 frameIndex) {
    std::lock_guard lock(m_mutex);

    if (m_passHistories.find(passName) == m_passHistories.end()) {
        if (m_passHistories.size() >= m_config.maxPasses) {
            NGE_LOG_WARN("Pipeline stats: max passes reached ({})", m_config.maxPasses);
            return;
        }

        PassHistory history;
        std::memset(&history.minStats, 0xFF, sizeof(PipelineStatistics)); // Max values for min tracking
        std::memset(&history.maxStats, 0, sizeof(PipelineStatistics));
        m_passHistories[passName] = std::move(history);
    }

    auto& history = m_passHistories[passName];

    PassStatEntry entry;
    entry.passName = passName;
    entry.stats = stats;
    entry.frameIndex = frameIndex;

    history.entries.push_back(std::move(entry));

    // Trim to history window
    while (history.entries.size() > m_config.historyFrames) {
        history.entries.erase(history.entries.begin());
    }

    // Update min/max
    if (m_config.trackMinMax) {
        UpdateMinMax(history, stats);
    }

    if (frameIndex >= m_totalFrames) {
        m_totalFrames = frameIndex + 1;
    }
}

AggregatedPassStats PipelineStatsAggregator::GetPassStats(const std::string& passName) const {
    std::lock_guard lock(m_mutex);

    AggregatedPassStats result;
    result.passName = passName;
    std::memset(&result.min, 0, sizeof(PipelineStatistics));
    std::memset(&result.max, 0, sizeof(PipelineStatistics));
    std::memset(&result.avg, 0, sizeof(PipelineStatistics));
    result.sampleCount = 0;

    auto it = m_passHistories.find(passName);
    if (it == m_passHistories.end()) return result;

    const auto& history = it->second;
    result.min = history.minStats;
    result.max = history.maxStats;
    result.avg = ComputeAverage(history);
    result.sampleCount = static_cast<u32>(history.entries.size());

    return result;
}

PipelineStatistics PipelineStatsAggregator::GetLatestPassStats(const std::string& passName) const {
    std::lock_guard lock(m_mutex);

    PipelineStatistics empty;
    std::memset(&empty, 0, sizeof(PipelineStatistics));

    auto it = m_passHistories.find(passName);
    if (it == m_passHistories.end() || it->second.entries.empty()) return empty;

    return it->second.entries.back().stats;
}

std::vector<std::string> PipelineStatsAggregator::GetPassNames() const {
    std::lock_guard lock(m_mutex);

    std::vector<std::string> names;
    names.reserve(m_passHistories.size());
    for (const auto& [name, _] : m_passHistories) {
        names.push_back(name);
    }
    return names;
}

PipelineStatistics PipelineStatsAggregator::GetFrameTotal(u32 frameIndex) const {
    std::lock_guard lock(m_mutex);

    PipelineStatistics total;
    std::memset(&total, 0, sizeof(PipelineStatistics));

    for (const auto& [name, history] : m_passHistories) {
        for (const auto& entry : history.entries) {
            if (entry.frameIndex == frameIndex) {
                total.inputAssemblyVertices += entry.stats.inputAssemblyVertices;
                total.inputAssemblyPrimitives += entry.stats.inputAssemblyPrimitives;
                total.vertexShaderInvocations += entry.stats.vertexShaderInvocations;
                total.geometryShaderInvocations += entry.stats.geometryShaderInvocations;
                total.geometryShaderPrimitives += entry.stats.geometryShaderPrimitives;
                total.clippingInvocations += entry.stats.clippingInvocations;
                total.clippingPrimitives += entry.stats.clippingPrimitives;
                total.fragmentShaderInvocations += entry.stats.fragmentShaderInvocations;
                total.tessControlPatches += entry.stats.tessControlPatches;
                total.tessEvalInvocations += entry.stats.tessEvalInvocations;
                total.computeShaderInvocations += entry.stats.computeShaderInvocations;
            }
        }
    }

    return total;
}

float PipelineStatsAggregator::GetBottleneckRatio(const std::string& passName) const {
    std::lock_guard lock(m_mutex);

    auto it = m_passHistories.find(passName);
    if (it == m_passHistories.end() || it->second.entries.empty()) return 1.0f;

    PipelineStatistics avg = ComputeAverage(it->second);

    if (avg.vertexShaderInvocations == 0) return 1.0f;

    return static_cast<float>(avg.fragmentShaderInvocations) /
           static_cast<float>(avg.vertexShaderInvocations);
}

u32 PipelineStatsAggregator::GetPassCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_passHistories.size());
}

u32 PipelineStatsAggregator::GetFrameCount() const {
    std::lock_guard lock(m_mutex);
    return m_totalFrames;
}

void PipelineStatsAggregator::ClearPassHistory(const std::string& passName) {
    std::lock_guard lock(m_mutex);
    m_passHistories.erase(passName);
}

void PipelineStatsAggregator::Reset() {
    std::lock_guard lock(m_mutex);
    m_passHistories.clear();
    m_totalFrames = 0;
}

PipelineStatsAggregatorStats PipelineStatsAggregator::GetStats() const {
    std::lock_guard lock(m_mutex);

    PipelineStatsAggregatorStats stats{};
    stats.totalPasses = static_cast<u32>(m_passHistories.size());
    stats.totalFramesRecorded = m_totalFrames;

    u64 totalVtx = 0, totalFrag = 0, totalCompute = 0;
    u32 totalEntries = 0;

    for (const auto& [name, history] : m_passHistories) {
        for (const auto& entry : history.entries) {
            totalVtx += entry.stats.vertexShaderInvocations;
            totalFrag += entry.stats.fragmentShaderInvocations;
            totalCompute += entry.stats.computeShaderInvocations;
            totalEntries++;
        }
    }

    stats.totalVertexInvocations = totalVtx;
    stats.totalFragmentInvocations = totalFrag;
    stats.totalComputeInvocations = totalCompute;

    stats.avgVertexPerFrame = m_totalFrames > 0
        ? static_cast<float>(totalVtx) / static_cast<float>(m_totalFrames) : 0.0f;
    stats.avgFragmentPerFrame = m_totalFrames > 0
        ? static_cast<float>(totalFrag) / static_cast<float>(m_totalFrames) : 0.0f;
    stats.vertexToFragmentRatio = totalFrag > 0
        ? static_cast<float>(totalVtx) / static_cast<float>(totalFrag) : 0.0f;

    return stats;
}

void PipelineStatsAggregator::UpdateMinMax(PassHistory& history, const PipelineStatistics& stats) {
    auto& mn = history.minStats;
    auto& mx = history.maxStats;

    mn.inputAssemblyVertices = std::min(mn.inputAssemblyVertices, stats.inputAssemblyVertices);
    mn.inputAssemblyPrimitives = std::min(mn.inputAssemblyPrimitives, stats.inputAssemblyPrimitives);
    mn.vertexShaderInvocations = std::min(mn.vertexShaderInvocations, stats.vertexShaderInvocations);
    mn.geometryShaderInvocations = std::min(mn.geometryShaderInvocations, stats.geometryShaderInvocations);
    mn.geometryShaderPrimitives = std::min(mn.geometryShaderPrimitives, stats.geometryShaderPrimitives);
    mn.clippingInvocations = std::min(mn.clippingInvocations, stats.clippingInvocations);
    mn.clippingPrimitives = std::min(mn.clippingPrimitives, stats.clippingPrimitives);
    mn.fragmentShaderInvocations = std::min(mn.fragmentShaderInvocations, stats.fragmentShaderInvocations);
    mn.tessControlPatches = std::min(mn.tessControlPatches, stats.tessControlPatches);
    mn.tessEvalInvocations = std::min(mn.tessEvalInvocations, stats.tessEvalInvocations);
    mn.computeShaderInvocations = std::min(mn.computeShaderInvocations, stats.computeShaderInvocations);

    mx.inputAssemblyVertices = std::max(mx.inputAssemblyVertices, stats.inputAssemblyVertices);
    mx.inputAssemblyPrimitives = std::max(mx.inputAssemblyPrimitives, stats.inputAssemblyPrimitives);
    mx.vertexShaderInvocations = std::max(mx.vertexShaderInvocations, stats.vertexShaderInvocations);
    mx.geometryShaderInvocations = std::max(mx.geometryShaderInvocations, stats.geometryShaderInvocations);
    mx.geometryShaderPrimitives = std::max(mx.geometryShaderPrimitives, stats.geometryShaderPrimitives);
    mx.clippingInvocations = std::max(mx.clippingInvocations, stats.clippingInvocations);
    mx.clippingPrimitives = std::max(mx.clippingPrimitives, stats.clippingPrimitives);
    mx.fragmentShaderInvocations = std::max(mx.fragmentShaderInvocations, stats.fragmentShaderInvocations);
    mx.tessControlPatches = std::max(mx.tessControlPatches, stats.tessControlPatches);
    mx.tessEvalInvocations = std::max(mx.tessEvalInvocations, stats.tessEvalInvocations);
    mx.computeShaderInvocations = std::max(mx.computeShaderInvocations, stats.computeShaderInvocations);
}

PipelineStatistics PipelineStatsAggregator::ComputeAverage(const PassHistory& history) const {
    PipelineStatistics avg;
    std::memset(&avg, 0, sizeof(PipelineStatistics));

    if (history.entries.empty()) return avg;

    u64 count = history.entries.size();

    for (const auto& entry : history.entries) {
        avg.inputAssemblyVertices += entry.stats.inputAssemblyVertices;
        avg.inputAssemblyPrimitives += entry.stats.inputAssemblyPrimitives;
        avg.vertexShaderInvocations += entry.stats.vertexShaderInvocations;
        avg.geometryShaderInvocations += entry.stats.geometryShaderInvocations;
        avg.geometryShaderPrimitives += entry.stats.geometryShaderPrimitives;
        avg.clippingInvocations += entry.stats.clippingInvocations;
        avg.clippingPrimitives += entry.stats.clippingPrimitives;
        avg.fragmentShaderInvocations += entry.stats.fragmentShaderInvocations;
        avg.tessControlPatches += entry.stats.tessControlPatches;
        avg.tessEvalInvocations += entry.stats.tessEvalInvocations;
        avg.computeShaderInvocations += entry.stats.computeShaderInvocations;
    }

    avg.inputAssemblyVertices /= count;
    avg.inputAssemblyPrimitives /= count;
    avg.vertexShaderInvocations /= count;
    avg.geometryShaderInvocations /= count;
    avg.geometryShaderPrimitives /= count;
    avg.clippingInvocations /= count;
    avg.clippingPrimitives /= count;
    avg.fragmentShaderInvocations /= count;
    avg.tessControlPatches /= count;
    avg.tessEvalInvocations /= count;
    avg.computeShaderInvocations /= count;

    return avg;
}

} // namespace nge::rhi
