#include "engine/renderer/debug/debug_visualization.h"
#include "engine/core/logging/log.h"
#include <cstring>

namespace nge::renderer {

bool DebugVisualization::Init(rhi::IDevice* device, const DebugVisualizationConfig& config) {
    m_device = device;
    m_config = config;

    // Per-frame constant buffer
    {
        rhi::BufferDesc desc;
        desc.size = sizeof(DebugDrawConstants);
        desc.usage = rhi::BufferUsage::Constant | rhi::BufferUsage::TransferDst;
        desc.memoryUsage = rhi::MemoryUsage::CPU_To_GPU;
        desc.debugName = "DebugVisConstants";
        m_constantBuffer = device->CreateBuffer(desc);
    }

    // TODO: Create compute pipelines
    // m_debugOverlayPipeline = CreateComputePipeline("debug_overlay.hlsl", "CSMain");
    // m_overdrawPipeline = CreateComputePipeline("debug_overlay.hlsl", "CSOverdraw");
    // m_nanCheckPipeline = CreateComputePipeline("debug_overlay.hlsl", "CSNanCheck");

    // TODO: Create graphics pipelines
    // m_wireframePipeline = CreateGraphicsPipeline("debug_wireframe.hlsl", wireframeRasterState);
    // m_normalsPipeline = CreateGraphicsPipeline("debug_normals.hlsl", lineRasterState);

    NGE_LOG_INFO("Debug visualization initialized");
    return true;
}

void DebugVisualization::Shutdown() {
    if (!m_device) return;
    if (m_constantBuffer.IsValid()) {
        m_device->DestroyBuffer(m_constantBuffer);
        m_constantBuffer = {};
    }
}

void DebugVisualization::ToggleMode(DebugMode mode) {
    if (HasFlag(m_config.activeMode, mode)) {
        // Remove flag
        m_config.activeMode = static_cast<DebugMode>(
            static_cast<u32>(m_config.activeMode) & ~static_cast<u32>(mode));
    } else {
        m_config.activeMode = m_config.activeMode | mode;
    }
}

void DebugVisualization::Render(rhi::ICommandList* cmd,
                                  rhi::TextureHandle colorTarget,
                                  rhi::TextureHandle depthBuffer,
                                  rhi::TextureHandle normalBuffer,
                                  rhi::TextureHandle motionBuffer,
                                  const math::Mat4& viewProj,
                                  const math::Vec3& cameraPos) {
    if (!IsActive()) return;

    cmd->BeginDebugLabel("DebugVisualization", 1.0f, 0.5f, 0.0f);

    // Upload constants
    DebugDrawConstants constants;
    constants.viewProj = viewProj;
    constants.cameraPos = {cameraPos.x, cameraPos.y, cameraPos.z, 0.0f};
    constants.depthNear = m_config.depthNear;
    constants.depthFar = m_config.depthFar;
    constants.overdrawScale = m_config.overdrawScale;
    constants.debugMode = static_cast<u32>(m_config.activeMode);
    constants.wireframeLineWidth = m_config.wireframeLineWidth;
    constants.normalScale = m_config.normalScale;

    void* mapped = m_device->MapBuffer(m_constantBuffer);
    if (mapped) {
        std::memcpy(mapped, &constants, sizeof(constants));
        m_device->UnmapBuffer(m_constantBuffer);
    }

    // Fullscreen debug overlay pass (GBuffer visualization modes)
    if (HasFlag(m_config.activeMode, DebugMode::Normals) ||
        HasFlag(m_config.activeMode, DebugMode::Albedo) ||
        HasFlag(m_config.activeMode, DebugMode::Metallic) ||
        HasFlag(m_config.activeMode, DebugMode::Roughness) ||
        HasFlag(m_config.activeMode, DebugMode::AO) ||
        HasFlag(m_config.activeMode, DebugMode::Depth) ||
        HasFlag(m_config.activeMode, DebugMode::MotionVectors) ||
        HasFlag(m_config.activeMode, DebugMode::MipLevel) ||
        HasFlag(m_config.activeMode, DebugMode::CascadeSplits)) {

        // cmd->BindComputePipeline(m_debugOverlayPipeline);
        // cmd->BindBuffer(0, m_constantBuffer);
        // cmd->BindTexture(1, colorTarget);   // UAV output
        // cmd->BindTexture(2, depthBuffer);   // SRV
        // cmd->BindTexture(3, normalBuffer);  // SRV
        // cmd->BindTexture(4, motionBuffer);  // SRV
        // u32 dispatchX = (width + 7) / 8;
        // u32 dispatchY = (height + 7) / 8;
        // cmd->Dispatch(dispatchX, dispatchY, 1);
    }

    // Overdraw heat map
    if (HasFlag(m_config.activeMode, DebugMode::Overdraw)) {
        // Requires an overdraw counter texture (incremented per fragment)
        // cmd->BindComputePipeline(m_overdrawPipeline);
        // cmd->Dispatch(dispatchX, dispatchY, 1);
    }

    // NaN/Inf check
    if (HasFlag(m_config.activeMode, DebugMode::NanInfCheck)) {
        // Scans color target for NaN/Inf and highlights in magenta
        // cmd->BindComputePipeline(m_nanCheckPipeline);
        // cmd->Dispatch(dispatchX, dispatchY, 1);
    }

    // Wireframe overlay
    if (HasFlag(m_config.activeMode, DebugMode::Wireframe)) {
        RenderWireframe(cmd, viewProj);
    }

    // Normal vectors
    if (HasFlag(m_config.activeMode, DebugMode::Normals)) {
        RenderNormals(cmd, viewProj, m_config.normalScale);
    }

    cmd->EndDebugLabel();

    (void)colorTarget; (void)depthBuffer; (void)normalBuffer; (void)motionBuffer;
}

void DebugVisualization::RenderWireframe(rhi::ICommandList* cmd, const math::Mat4& viewProj) {
    // Redraw scene geometry with wireframe rasterizer state
    // cmd->BindGraphicsPipeline(m_wireframePipeline);
    // cmd->SetLineWidth(m_config.wireframeLineWidth);
    // ... bind vertex/index buffers, draw
    (void)cmd; (void)viewProj;
}

void DebugVisualization::RenderNormals(rhi::ICommandList* cmd, const math::Mat4& viewProj, f32 scale) {
    // Generate line segments from vertex positions + normals via geometry/compute shader
    // cmd->BindGraphicsPipeline(m_normalsPipeline);
    // ... draw normal lines
    (void)cmd; (void)viewProj; (void)scale;
}

} // namespace nge::renderer
