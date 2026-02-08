#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_queries.h"
#include "engine/renderer/debug/debug_text.h"
#include <string>
#include <vector>

namespace nge::renderer {

// ─── Profiler Overlay ────────────────────────────────────────────────────
// Renders a real-time GPU/CPU performance overlay using DebugTextRenderer.
// Shows per-pass GPU timing, frame stats, and memory usage.

class ProfilerOverlay {
public:
    bool Init(DebugTextRenderer* textRenderer);

    // Per-frame update: reads GPU profiler results and frame stats
    void Update(const rhi::GPUProfiler& gpuProfiler, f32 cpuFrameTimeMs,
                 u32 drawCalls, u32 triangles, u32 screenWidth, u32 screenHeight);

    // Draw the overlay (call before DebugTextRenderer::Flush)
    void Draw(u32 screenWidth, u32 screenHeight);

    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    void SetPosition(f32 x, f32 y) { m_posX = x; m_posY = y; }
    void SetScale(f32 scale) { m_scale = scale; }

private:
    struct FrameStats {
        f64 gpuFrameMs = 0;
        f64 cpuFrameMs = 0;
        u32 drawCalls = 0;
        u32 triangles = 0;
        std::vector<rhi::GPUProfiler::ScopeResult> scopes;
    };

    DebugTextRenderer* m_textRenderer = nullptr;
    FrameStats m_stats;

    f32 m_posX = 10.0f;
    f32 m_posY = 10.0f;
    f32 m_scale = 1.0f;
    bool m_enabled = true;

    // FPS smoothing
    f64 m_fpsSmoothed = 0;
    static constexpr f64 FPS_SMOOTH_FACTOR = 0.95;
};

} // namespace nge::renderer
