#include "engine/rhi/common/rhi_queries.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace nge::rhi {

bool GPUProfiler::Init(u32 maxScopes, u32 historyFrames) {
    m_maxScopes = maxScopes;
    m_historyFrames = historyFrames;
    m_currentFrame = 0;
    m_scopeDepth = 0;

    for (auto& frame : m_frames) {
        frame.scopes.reserve(maxScopes);
        frame.queryCount = 0;
    }

    m_history.resize(maxScopes);
    for (auto& h : m_history) {
        h.samples.resize(historyFrames, 0.0);
        h.writeIdx = 0;
    }

    m_initialized = true;
    NGE_LOG_INFO("GPU profiler initialized: max {} scopes, {} history frames", maxScopes, historyFrames);
    return true;
}

void GPUProfiler::Shutdown() {
    m_results.clear();
    m_history.clear();
    m_initialized = false;
}

void GPUProfiler::BeginFrame() {
    if (!m_initialized) return;

    m_currentFrame = (m_currentFrame + 1) % (FRAME_LATENCY + 1);
    auto& frame = m_frames[m_currentFrame];
    frame.scopes.clear();
    frame.queryCount = 0;
    m_scopeDepth = 0;

    // Read back results from FRAME_LATENCY frames ago
    u32 readFrame = (m_currentFrame + 1) % (FRAME_LATENCY + 1);
    auto& readData = m_frames[readFrame];

    m_results.clear();
    m_results.reserve(readData.scopes.size());

    f64 totalFrameTime = 0;

    for (u32 i = 0; i < static_cast<u32>(readData.scopes.size()); ++i) {
        const auto& scope = readData.scopes[i];

        // In production: read timestamp query results from GPU
        // For stub: simulate ~1ms per scope
        f64 elapsed = 0.5 + static_cast<f64>(i) * 0.1;

        // Update rolling history
        if (i < m_maxScopes) {
            auto& hist = m_history[i];
            hist.samples[hist.writeIdx % m_historyFrames] = elapsed;
            hist.writeIdx++;

            // Compute stats
            u32 count = std::min(hist.writeIdx, m_historyFrames);
            f64 sum = 0, minVal = 1e10, maxVal = 0;
            for (u32 j = 0; j < count; ++j) {
                f64 s = hist.samples[j];
                sum += s;
                minVal = std::min(minVal, s);
                maxVal = std::max(maxVal, s);
            }

            ScopeResult result;
            result.name = scope.name;
            result.avgMs = count > 0 ? sum / static_cast<f64>(count) : 0;
            result.minMs = minVal < 1e9 ? minVal : 0;
            result.maxMs = maxVal;
            result.depth = scope.depth;
            m_results.push_back(result);

            if (scope.depth == 0) totalFrameTime += elapsed;
        }
    }

    m_frameTimeMs = totalFrameTime;
}

void GPUProfiler::EndFrame() {
    if (!m_initialized) return;
    // In production: resolve timestamp query pool for this frame
}

void GPUProfiler::BeginScope(const std::string& name) {
    if (!m_initialized) return;

    auto& frame = m_frames[m_currentFrame];
    if (frame.scopes.size() >= m_maxScopes) return;

    ScopeEntry entry;
    entry.name = name;
    entry.beginQueryIdx = frame.queryCount++;
    entry.endQueryIdx = 0;
    entry.depth = m_scopeDepth;

    frame.scopes.push_back(std::move(entry));
    m_scopeDepth++;

    // In production: cmd->WriteTimestamp(queryPool, entry.beginQueryIdx);
}

void GPUProfiler::EndScope() {
    if (!m_initialized || m_scopeDepth == 0) return;

    m_scopeDepth--;
    auto& frame = m_frames[m_currentFrame];

    // Find the matching scope (last unclosed at current depth)
    for (auto it = frame.scopes.rbegin(); it != frame.scopes.rend(); ++it) {
        if (it->depth == m_scopeDepth && it->endQueryIdx == 0) {
            it->endQueryIdx = frame.queryCount++;
            break;
        }
    }

    // In production: cmd->WriteTimestamp(queryPool, endQueryIdx);
}

} // namespace nge::rhi
