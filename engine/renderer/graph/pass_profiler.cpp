#include "engine/renderer/graph/pass_profiler.h"
#include "engine/core/logging/log.h"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace nge::renderer {

bool PassProfiler::Init(rhi::IDevice* device, u32 maxPasses) {
    GPUTimerProfiler::Config config;
    config.maxTimers = maxPasses * 2; // Begin + End per pass
    config.framesInFlight = 3;

    if (!m_timerProfiler.Init(device, config)) {
        return false;
    }

    m_results.reserve(maxPasses);
    m_currentPasses.reserve(maxPasses);

    NGE_LOG_INFO("Pass profiler initialized: max {} passes", maxPasses);
    return true;
}

void PassProfiler::Shutdown() {
    m_timerProfiler.Shutdown();
    m_results.clear();
    m_currentPasses.clear();
}

void PassProfiler::BeginFrame(rhi::ICommandList* cmd, u32 frameIndex) {
    m_timerProfiler.BeginFrame(cmd, frameIndex);
    m_currentPasses.clear();

    // Collect results from previous frames
    m_results.clear();
    const auto& timerResults = m_timerProfiler.GetResults();
    for (const auto& node : timerResults) {
        // Find matching pass info
        for (const auto& [cp, name] : m_currentPasses) {
            if (name == node.name) {
                PassTimingResult result;
                result.passId = cp.passId;
                result.passName = name;
                result.queueType = cp.queueType;
                result.durationMs = node.durationMs;
                result.executionOrder = cp.executionOrder;
                m_results.push_back(std::move(result));
                break;
            }
        }
    }

    // If no matches from current passes (first frames), use timer nodes directly
    if (m_results.empty()) {
        for (const auto& node : timerResults) {
            PassTimingResult result;
            result.passId = 0;
            result.passName = node.name;
            result.queueType = PassType::Graphics;
            result.durationMs = node.durationMs;
            result.executionOrder = 0;
            m_results.push_back(std::move(result));
        }
    }
}

void PassProfiler::BeginPass(rhi::ICommandList* cmd, const CompiledPass& pass, const std::string& name) {
    m_currentPasses.push_back({pass, name});
    m_timerProfiler.BeginTimer(cmd, name);
}

void PassProfiler::EndPass(rhi::ICommandList* cmd, const CompiledPass& pass) {
    // Find the timer index for this pass
    for (u32 i = 0; i < static_cast<u32>(m_currentPasses.size()); ++i) {
        if (m_currentPasses[i].first.passId == pass.passId) {
            m_timerProfiler.EndTimer(cmd, i);
            break;
        }
    }
}

void PassProfiler::EndFrame(rhi::ICommandList* cmd) {
    m_timerProfiler.EndFrame(cmd);
}

f64 PassProfiler::GetTotalGpuMs() const {
    f64 total = 0;
    for (const auto& r : m_results) {
        total += r.durationMs;
    }
    return total;
}

const PassTimingResult* PassProfiler::GetSlowestPass() const {
    if (m_results.empty()) return nullptr;
    auto it = std::max_element(m_results.begin(), m_results.end(),
        [](const PassTimingResult& a, const PassTimingResult& b) {
            return a.durationMs < b.durationMs;
        });
    return &(*it);
}

f64 PassProfiler::GetPassMs(const std::string& name) const {
    for (const auto& r : m_results) {
        if (r.passName == name) return r.durationMs;
    }
    return -1.0;
}

std::string PassProfiler::GetTimingReport() const {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "GPU Pass Timings (" << GetTotalGpuMs() << " ms total)\n";
    ss << "────────────────────────────────────\n";

    // Sort by execution order for display
    auto sorted = m_results;
    std::sort(sorted.begin(), sorted.end(),
        [](const PassTimingResult& a, const PassTimingResult& b) {
            return a.executionOrder < b.executionOrder;
        });

    f64 total = GetTotalGpuMs();
    for (const auto& r : sorted) {
        f32 pct = total > 0 ? static_cast<f32>(r.durationMs / total * 100.0) : 0.0f;
        const char* queueTag = "";
        switch (r.queueType) {
            case PassType::AsyncCompute: queueTag = " [AC]"; break;
            case PassType::Compute:      queueTag = " [C]"; break;
            case PassType::Transfer:     queueTag = " [T]"; break;
            default: break;
        }
        ss << "  " << r.passName << queueTag << ": "
           << r.durationMs << " ms (" << pct << "%)\n";
    }

    auto* slowest = GetSlowestPass();
    if (slowest) {
        ss << "Slowest: " << slowest->passName << " (" << slowest->durationMs << " ms)\n";
    }

    return ss.str();
}

} // namespace nge::renderer
