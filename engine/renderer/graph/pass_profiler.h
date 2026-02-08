#pragma once

#include "engine/core/types.h"
#include "engine/renderer/debug/gpu_timer.h"
#include "engine/renderer/graph/frame_graph_compiler.h"
#include <vector>
#include <string>

namespace nge::renderer {

// ─── Render Graph Pass Profiler ──────────────────────────────────────────
// Automatically injects GPU timers around each compiled render graph pass.
// Collects per-pass GPU timing for the profiler overlay without requiring
// manual instrumentation of every pass.

struct PassTimingResult {
    u32         passId;
    std::string passName;
    PassType    queueType;
    f64         durationMs;
    u32         executionOrder;
};

class PassProfiler {
public:
    bool Init(rhi::IDevice* device, u32 maxPasses = 64);
    void Shutdown();

    // Begin profiling a compiled graph's passes
    void BeginFrame(rhi::ICommandList* cmd, u32 frameIndex);

    // Inject timer around a pass (call before/after each pass execute)
    void BeginPass(rhi::ICommandList* cmd, const CompiledPass& pass, const std::string& name);
    void EndPass(rhi::ICommandList* cmd, const CompiledPass& pass);

    // Finalize frame
    void EndFrame(rhi::ICommandList* cmd);

    // Results from N-2 frames
    const std::vector<PassTimingResult>& GetResults() const { return m_results; }

    // Total GPU frame time (sum of all passes)
    f64 GetTotalGpuMs() const;

    // Find slowest pass
    const PassTimingResult* GetSlowestPass() const;

    // Get pass timing by name
    f64 GetPassMs(const std::string& name) const;

    // Get timing breakdown as string (for overlay display)
    std::string GetTimingReport() const;

private:
    GPUTimerProfiler m_timerProfiler;
    std::vector<PassTimingResult> m_results;
    std::vector<std::pair<CompiledPass, std::string>> m_currentPasses;
};

} // namespace nge::renderer
