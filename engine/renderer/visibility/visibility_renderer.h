#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/renderer/geometry/virtual_geometry.h"
#include "engine/renderer/materials/material.h"

namespace nge::renderer {

// ─── Visibility Buffer Renderer ──────────────────────────────────────────
// Implements the GPU-driven visibility buffer pipeline:
//   1. GPU Instance Culling (frustum + HZB occlusion)
//   2. GPU Meshlet Culling (frustum + backface cone)
//   3. Visibility Buffer Rasterization (mesh shaders → R32G32_UINT)
//   4. Material Resolve (full-screen compute → G-buffers + lighting)
//
// Two-phase occlusion culling:
//   Phase A: Cull against previous frame HZB → render survivors → rebuild HZB
//   Phase B: Re-test rejected instances against new HZB → render newly visible

struct VisibilityRendererDesc {
    u32 maxInstances     = 1024 * 1024;  // 1M instances
    u32 maxMeshlets      = 16 * 1024 * 1024; // 16M meshlets
    u32 maxDrawCommands  = 1024 * 1024;
    bool enableTwoPhaseOcclusion = true;
    bool enableMeshShaders = true; // Fallback to vertex path if false
};

struct InstanceGPUData {
    math::Mat4 worldMatrix;
    math::Mat4 worldMatrixInvTranspose;
    math::Vec4 boundingSphere;     // xyz = center, w = radius
    u32        meshletOffset;
    u32        meshletCount;
    u32        materialId;
    u32        flags;
};

class VisibilityRenderer {
public:
    bool Init(rhi::IDevice* device, const VisibilityRendererDesc& desc = {});
    void Shutdown();

    // Register instance data for rendering
    void SetInstances(const InstanceGPUData* instances, u32 count);

    // Execute the full visibility pipeline
    void Render(rhi::ICommandList* cmd,
                rhi::TextureHandle visBuffer,
                rhi::TextureHandle depthBuffer,
                rhi::TextureHandle hzbTexture,
                const math::Mat4& viewProj,
                const math::Vec3& cameraPos,
                const math::Vec4 frustumPlanes[6],
                u32 width, u32 height);

    // Individual passes (for custom pipeline composition)
    void PassInstanceCulling(rhi::ICommandList* cmd, const math::Mat4& viewProj,
                              const math::Vec4 frustumPlanes[6],
                              rhi::TextureHandle hzb, u32 hzbWidth, u32 hzbHeight);
    void PassMeshletCulling(rhi::ICommandList* cmd, const math::Mat4& viewProj,
                             const math::Vec3& cameraPos,
                             const math::Vec4 frustumPlanes[6]);
    void PassVisibilityRaster(rhi::ICommandList* cmd,
                               rhi::TextureHandle visBuffer,
                               rhi::TextureHandle depthBuffer,
                               const math::Mat4& viewProj,
                               u32 width, u32 height);
    void PassHZBBuild(rhi::ICommandList* cmd,
                       rhi::TextureHandle depthBuffer,
                       rhi::TextureHandle hzbTexture);
    void PassMaterialResolve(rhi::ICommandList* cmd,
                              rhi::TextureHandle visBuffer,
                              rhi::TextureHandle depthBuffer,
                              u32 width, u32 height);

    // Stats
    u32 GetVisibleInstances() const { return m_visibleInstanceCount; }
    u32 GetVisibleMeshlets() const { return m_visibleMeshletCount; }
    u32 GetRenderedTriangles() const { return m_renderedTriangles; }

private:
    void CreateBuffers(const VisibilityRendererDesc& desc);
    void CreatePipelines();

    rhi::IDevice* m_device = nullptr;
    VisibilityRendererDesc m_desc;

    // GPU Buffers
    rhi::BufferHandle m_instanceBuffer;       // All instances
    rhi::BufferHandle m_visibleInstanceBuffer; // Surviving instances after culling
    rhi::BufferHandle m_visibleCountBuffer;    // Atomic counter for visible count
    rhi::BufferHandle m_drawCommandBuffer;     // Indirect draw/dispatch commands
    rhi::BufferHandle m_meshletBuffer;         // All meshlet descriptors

    // Pipelines
    rhi::PipelineHandle m_instanceCullPipeline;
    rhi::PipelineHandle m_meshletCullPipeline;
    rhi::PipelineHandle m_visBufferPipeline;    // Mesh shader or vertex path
    rhi::PipelineHandle m_hzbBuildPipeline;
    rhi::PipelineHandle m_materialResolvePipeline;

    // State
    u32 m_instanceCount = 0;
    u32 m_visibleInstanceCount = 0;
    u32 m_visibleMeshletCount = 0;
    u32 m_renderedTriangles = 0;
};

} // namespace nge::renderer
