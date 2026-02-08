#include "engine/renderer/debug/profiler_overlay.h"
#include <cstdio>

namespace nge::renderer {

bool ProfilerOverlay::Init(DebugTextRenderer* textRenderer) {
    m_textRenderer = textRenderer;
    return textRenderer != nullptr;
}

void ProfilerOverlay::Update(const rhi::GPUProfiler& gpuProfiler, f32 cpuFrameTimeMs,
                               u32 drawCalls, u32 triangles, u32 /*screenWidth*/, u32 /*screenHeight*/) {
    m_stats.gpuFrameMs = gpuProfiler.GetFrameTimeMs();
    m_stats.cpuFrameMs = static_cast<f64>(cpuFrameTimeMs);
    m_stats.drawCalls = drawCalls;
    m_stats.triangles = triangles;
    m_stats.scopes = gpuProfiler.GetResults();

    // Smooth FPS
    f64 currentFps = (cpuFrameTimeMs > 0.001) ? 1000.0 / cpuFrameTimeMs : 0;
    m_fpsSmoothed = m_fpsSmoothed * FPS_SMOOTH_FACTOR + currentFps * (1.0 - FPS_SMOOTH_FACTOR);
}

void ProfilerOverlay::Draw(u32 /*screenWidth*/, u32 /*screenHeight*/) {
    if (!m_enabled || !m_textRenderer) return;

    f32 x = m_posX;
    f32 y = m_posY;
    f32 lineHeight = 10.0f * m_scale;

    math::Vec4 white   = {1.0f, 1.0f, 1.0f, 1.0f};
    math::Vec4 yellow  = {1.0f, 1.0f, 0.3f, 1.0f};
    math::Vec4 green   = {0.3f, 1.0f, 0.3f, 1.0f};
    math::Vec4 red     = {1.0f, 0.3f, 0.3f, 1.0f};
    math::Vec4 cyan    = {0.3f, 1.0f, 1.0f, 1.0f};
    math::Vec4 bgColor = {0.0f, 0.0f, 0.0f, 0.6f};

    // FPS color: green > 60, yellow > 30, red < 30
    math::Vec4 fpsColor = green;
    if (m_fpsSmoothed < 30.0) fpsColor = red;
    else if (m_fpsSmoothed < 60.0) fpsColor = yellow;

    // Header
    char buf[256];
    snprintf(buf, sizeof(buf), "FPS: %.0f", m_fpsSmoothed);
    m_textRenderer->DrawText(x, y, buf, fpsColor, bgColor, m_scale);
    y += lineHeight;

    snprintf(buf, sizeof(buf), "CPU: %.2f ms  GPU: %.2f ms", m_stats.cpuFrameMs, m_stats.gpuFrameMs);
    m_textRenderer->DrawText(x, y, buf, white, bgColor, m_scale);
    y += lineHeight;

    snprintf(buf, sizeof(buf), "Draw calls: %u  Triangles: %uK",
             m_stats.drawCalls, m_stats.triangles / 1000);
    m_textRenderer->DrawText(x, y, buf, white, bgColor, m_scale);
    y += lineHeight;

    // Separator
    m_textRenderer->DrawText(x, y, "--- GPU Passes ---", cyan, bgColor, m_scale);
    y += lineHeight;

    // Per-pass timing
    for (const auto& scope : m_stats.scopes) {
        // Indent based on depth
        f32 indent = x + static_cast<f32>(scope.depth) * 12.0f * m_scale;

        // Color based on timing
        math::Vec4 scopeColor = green;
        if (scope.avgMs > 2.0) scopeColor = yellow;
        if (scope.avgMs > 5.0) scopeColor = red;

        snprintf(buf, sizeof(buf), "%s: %.2f ms (%.2f-%.2f)",
                 scope.name.c_str(), scope.avgMs, scope.minMs, scope.maxMs);
        m_textRenderer->DrawText(indent, y, buf, scopeColor, bgColor, m_scale);
        y += lineHeight;
    }
}

} // namespace nge::renderer
