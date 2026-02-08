#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>

namespace nge::renderer {

// ─── Debug Visualization System ──────────────────────────────────────────
// Overlays debug information on the rendered scene. Activated per-frame
// via flags. Each mode renders as a fullscreen post-process or geometry
// overlay pass in the render graph.

enum class DebugMode : u32 {
    None            = 0,
    Wireframe       = 1 << 0,   // Wireframe overlay on solid geometry
    Normals         = 1 << 1,   // World-space normals as RGB
    Albedo          = 1 << 2,   // Base color only (no lighting)
    Metallic        = 1 << 3,   // Metallic channel grayscale
    Roughness       = 1 << 4,   // Roughness channel grayscale
    AO              = 1 << 5,   // Ambient occlusion channel
    MotionVectors   = 1 << 6,   // Motion vectors as RG color
    Depth           = 1 << 7,   // Linearized depth (near=white, far=black)
    Overdraw        = 1 << 8,   // Overdraw heat map (red = high overdraw)
    MeshletBounds   = 1 << 9,   // Meshlet AABB wireframes
    LightHeatmap    = 1 << 10,  // Lights per cluster heat map
    CascadeSplits   = 1 << 11,  // Shadow cascade split visualization
    MipLevel        = 1 << 12,  // Texture mip level as color gradient
    NanInfCheck     = 1 << 13,  // Highlight NaN/Inf pixels in magenta
};

inline DebugMode operator|(DebugMode a, DebugMode b) {
    return static_cast<DebugMode>(static_cast<u32>(a) | static_cast<u32>(b));
}
inline DebugMode operator&(DebugMode a, DebugMode b) {
    return static_cast<DebugMode>(static_cast<u32>(a) & static_cast<u32>(b));
}
inline bool HasFlag(DebugMode mode, DebugMode flag) {
    return (static_cast<u32>(mode) & static_cast<u32>(flag)) != 0;
}

struct DebugVisualizationConfig {
    DebugMode activeMode = DebugMode::None;
    f32       wireframeLineWidth = 1.0f;
    f32       normalScale = 1.0f;         // Length of normal vectors
    f32       depthNear = 0.1f;
    f32       depthFar = 500.0f;
    f32       overdrawScale = 0.1f;       // Color intensity per overdraw layer
    u32       maxOverdrawLayers = 10;
    math::Vec3 nanColor = {1.0f, 0.0f, 1.0f}; // Magenta
};

struct DebugDrawConstants {
    math::Mat4 viewProj;
    math::Vec4 cameraPos;
    f32        depthNear;
    f32        depthFar;
    f32        overdrawScale;
    u32        debugMode;
    f32        wireframeLineWidth;
    f32        normalScale;
    f32        pad[2];
};

class DebugVisualization {
public:
    bool Init(rhi::IDevice* device, const DebugVisualizationConfig& config = {});
    void Shutdown();

    // Set active debug mode(s)
    void SetMode(DebugMode mode) { m_config.activeMode = mode; }
    void ToggleMode(DebugMode mode);
    DebugMode GetMode() const { return m_config.activeMode; }
    bool IsActive() const { return m_config.activeMode != DebugMode::None; }

    // Render debug overlay pass
    void Render(rhi::ICommandList* cmd,
                rhi::TextureHandle colorTarget,
                rhi::TextureHandle depthBuffer,
                rhi::TextureHandle normalBuffer,
                rhi::TextureHandle motionBuffer,
                const math::Mat4& viewProj,
                const math::Vec3& cameraPos);

    // Render wireframe geometry overlay
    void RenderWireframe(rhi::ICommandList* cmd, const math::Mat4& viewProj);

    // Render normal vectors as lines
    void RenderNormals(rhi::ICommandList* cmd, const math::Mat4& viewProj, f32 scale);

    DebugVisualizationConfig& GetConfig() { return m_config; }

private:
    rhi::IDevice* m_device = nullptr;
    DebugVisualizationConfig m_config;

    // Compute pipelines for fullscreen debug passes
    rhi::PipelineHandle m_debugOverlayPipeline;
    rhi::PipelineHandle m_overdrawPipeline;
    rhi::PipelineHandle m_nanCheckPipeline;

    // Graphics pipelines
    rhi::PipelineHandle m_wireframePipeline;
    rhi::PipelineHandle m_normalsPipeline;

    // Per-frame constant buffer
    rhi::BufferHandle m_constantBuffer;
};

} // namespace nge::renderer
