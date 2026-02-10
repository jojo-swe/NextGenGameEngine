#include "engine/rhi/common/rhi_timestamp_query_pool.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <numeric>

namespace nge::rhi {

bool TimestampQueryPool::Init(const TimestampQueryPoolConfig& config) {
    m_config = config;
    m_ringBuffer.resize(config.frameLatency);

    for (u32 i = 0; i < config.frameLatency; ++i) {
        m_ringBuffer[i].frameIndex = 0;
        m_ringBuffer[i].submitted = false;
        m_ringBuffer[i].resolved = false;
        m_ringBuffer[i].totalGpuTimeMs = 0.0;
        m_ringBuffer[i].pairs.reserve(config.queriesPerFrame / 2);
    }

    m_currentFrameRing = 0;
    m_nextQueryIndex = 0;
    m_totalFrames = 0;
    m_totalPairs = 0;
    m_peakGpuTime = 0.0;
    m_lastResolvedRing = -1;

    // TODO: Create VkQueryPool
    // VkQueryPoolCreateInfo poolInfo{};
    // poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    // poolInfo.queryCount = config.queriesPerFrame * config.frameLatency;

    NGE_LOG_INFO("Timestamp query pool initialized: queriesPerFrame={}, latency={}, period={:.2f}ns",
                 config.queriesPerFrame, config.frameLatency, config.timestampPeriodNs);
    return true;
}

void TimestampQueryPool::Shutdown() {
    m_ringBuffer.clear();
    m_movingAvgData.clear();
}

void TimestampQueryPool::BeginFrame(u32 frameIndex) {
    std::lock_guard lock(m_mutex);

    m_currentFrameRing = GetRingIndex(frameIndex);
    auto& frame = m_ringBuffer[m_currentFrameRing];

    frame.frameIndex = frameIndex;
    frame.pairs.clear();
    frame.submitted = false;
    frame.resolved = false;
    frame.totalGpuTimeMs = 0.0;

    m_nextQueryIndex = m_currentFrameRing * m_config.queriesPerFrame;
    m_totalFrames++;

    // TODO: vkCmdResetQueryPool for this frame's range
}

void TimestampQueryPool::EndFrame() {
    std::lock_guard lock(m_mutex);
    m_ringBuffer[m_currentFrameRing].submitted = true;
}

u32 TimestampQueryPool::BeginPass(const std::string& passName) {
    std::lock_guard lock(m_mutex);

    auto& frame = m_ringBuffer[m_currentFrameRing];
    u32 pairId = static_cast<u32>(frame.pairs.size());

    u32 baseQuery = m_currentFrameRing * m_config.queriesPerFrame;
    u32 queryOffset = pairId * 2;

    if (queryOffset + 1 >= m_config.queriesPerFrame) {
        NGE_LOG_WARN("Timestamp query pool: max queries per frame reached");
        return UINT32_MAX;
    }

    TimestampPair pair;
    pair.beginQuery = baseQuery + queryOffset;
    pair.endQuery = baseQuery + queryOffset + 1;
    pair.passName = passName;
    pair.gpuTimeMs = 0.0;
    pair.resolved = false;

    frame.pairs.push_back(std::move(pair));

    // TODO: vkCmdWriteTimestamp(beginQuery)

    return pairId;
}

void TimestampQueryPool::EndPass(u32 pairId) {
    std::lock_guard lock(m_mutex);

    auto& frame = m_ringBuffer[m_currentFrameRing];
    if (pairId >= frame.pairs.size()) return;

    // TODO: vkCmdWriteTimestamp(frame.pairs[pairId].endQuery)
}

void TimestampQueryPool::ResolveFrame(u32 frameIndex, const u64* queryResults, u32 resultCount) {
    std::lock_guard lock(m_mutex);

    u32 ring = GetRingIndex(frameIndex);
    auto& frame = m_ringBuffer[ring];

    if (!frame.submitted || frame.frameIndex != frameIndex) return;

    f64 totalMs = 0.0;

    for (auto& pair : frame.pairs) {
        u32 baseQuery = ring * m_config.queriesPerFrame;
        u32 beginIdx = pair.beginQuery - baseQuery;
        u32 endIdx = pair.endQuery - baseQuery;

        if (endIdx < resultCount) {
            u64 beginTs = queryResults[beginIdx];
            u64 endTs = queryResults[endIdx];

            f64 deltaNs = static_cast<f64>(endTs - beginTs) * m_config.timestampPeriodNs;
            pair.gpuTimeMs = deltaNs / 1000000.0;
            pair.resolved = true;

            totalMs += pair.gpuTimeMs;
            m_totalPairs++;

            // Update moving average
            if (m_config.enableMovingAverage) {
                auto& history = m_movingAvgData[pair.passName];
                history.push_back(pair.gpuTimeMs);
                if (history.size() > m_config.movingAverageWindow) {
                    history.erase(history.begin());
                }
            }
        }
    }

    frame.totalGpuTimeMs = totalMs;
    frame.resolved = true;
    m_lastResolvedRing = static_cast<i32>(ring);

    if (totalMs > m_peakGpuTime) m_peakGpuTime = totalMs;
}

f64 TimestampQueryPool::GetPassTimeMs(const std::string& passName) const {
    std::lock_guard lock(m_mutex);

    if (m_lastResolvedRing < 0) return 0.0;

    const auto& frame = m_ringBuffer[m_lastResolvedRing];
    for (const auto& pair : frame.pairs) {
        if (pair.passName == passName && pair.resolved) {
            return pair.gpuTimeMs;
        }
    }

    return 0.0;
}

std::vector<std::pair<std::string, f64>> TimestampQueryPool::GetAllPassTimings() const {
    std::lock_guard lock(m_mutex);

    std::vector<std::pair<std::string, f64>> result;
    if (m_lastResolvedRing < 0) return result;

    const auto& frame = m_ringBuffer[m_lastResolvedRing];
    for (const auto& pair : frame.pairs) {
        if (pair.resolved) {
            result.emplace_back(pair.passName, pair.gpuTimeMs);
        }
    }

    return result;
}

std::string TimestampQueryPool::GetSlowestPass() const {
    std::lock_guard lock(m_mutex);

    if (m_lastResolvedRing < 0) return "";

    const auto& frame = m_ringBuffer[m_lastResolvedRing];
    f64 maxTime = 0.0;
    std::string slowest;

    for (const auto& pair : frame.pairs) {
        if (pair.resolved && pair.gpuTimeMs > maxTime) {
            maxTime = pair.gpuTimeMs;
            slowest = pair.passName;
        }
    }

    return slowest;
}

f64 TimestampQueryPool::GetPassMovingAvgMs(const std::string& passName) const {
    std::lock_guard lock(m_mutex);

    auto it = m_movingAvgData.find(passName);
    if (it == m_movingAvgData.end() || it->second.empty()) return 0.0;

    const auto& history = it->second;
    f64 sum = std::accumulate(history.begin(), history.end(), 0.0);
    return sum / static_cast<f64>(history.size());
}

f64 TimestampQueryPool::GetLastFrameGpuTimeMs() const {
    std::lock_guard lock(m_mutex);

    if (m_lastResolvedRing < 0) return 0.0;
    return m_ringBuffer[m_lastResolvedRing].totalGpuTimeMs;
}

void TimestampQueryPool::Reset() {
    std::lock_guard lock(m_mutex);

    for (auto& frame : m_ringBuffer) {
        frame.pairs.clear();
        frame.submitted = false;
        frame.resolved = false;
        frame.totalGpuTimeMs = 0.0;
    }

    m_movingAvgData.clear();
    m_nextQueryIndex = 0;
    m_totalFrames = 0;
    m_totalPairs = 0;
    m_peakGpuTime = 0.0;
    m_lastResolvedRing = -1;
}

TimestampQueryPoolStats TimestampQueryPool::GetStats() const {
    std::lock_guard lock(m_mutex);
    TimestampQueryPoolStats stats{};
    stats.totalFramesProfiled = m_totalFrames;
    stats.totalPairsResolved = m_totalPairs;
    stats.activeFrame = m_currentFrameRing;
    stats.queriesUsedThisFrame = static_cast<u32>(m_ringBuffer.empty() ? 0 :
        m_ringBuffer[m_currentFrameRing].pairs.size() * 2);
    stats.peakFrameGpuTimeMs = m_peakGpuTime;

    if (m_lastResolvedRing >= 0) {
        stats.lastFrameGpuTimeMs = m_ringBuffer[m_lastResolvedRing].totalGpuTimeMs;
    }

    // Compute average from all resolved frames in ring
    f64 totalTime = 0.0;
    u32 resolvedFrames = 0;
    for (const auto& frame : m_ringBuffer) {
        if (frame.resolved) {
            totalTime += frame.totalGpuTimeMs;
            resolvedFrames++;
        }
    }
    stats.avgFrameGpuTimeMs = resolvedFrames > 0 ? totalTime / resolvedFrames : 0.0;

    return stats;
}

u32 TimestampQueryPool::GetRingIndex(u32 frameIndex) const {
    return frameIndex % m_config.frameLatency;
}

} // namespace nge::rhi
