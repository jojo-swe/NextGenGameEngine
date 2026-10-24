#include "engine/renderer/visibility/visibility_renderer.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"

namespace nge::renderer {

bool VisibilityRenderer::Init(rhi::IDevice* device, const VisibilityRendererDesc& desc) {
    m_device = device;
    m_desc = desc;

    CreateBuffers(desc);
    CreatePipelines();

    NGE_LOG_INFO("Visibility renderer initialized: maxInstances={}, maxMeshlets={}, meshShaders={}",
                 desc.maxInstances, desc.maxMeshlets, desc.enableMeshShaders);
    return true;
}

void VisibilityRenderer::Shutdown() {
    if (!m_device) return;

    auto destroyBuf = [&](rhi::BufferHandle& h) {
        if (h.IsValid()) { m_device->DestroyBuffer(h); h = rhi::BufferHandle{}; }
    };

    destroyBuf(m_instanceBuffer);
    destroyBuf(m_visibleInstanceBuffer);
    destroyBuf(m_visibleCountBuffer);
    destroyBuf(m_drawCommandBuffer);
    destroyBuf(m_meshletBuffer);

    m_device = nullptr;
}

void VisibilityRenderer::CreateBuffers(const VisibilityRendererDesc& desc) {
    auto createBuf = [&](usize size, rhi::BufferUsage usage, const char* name) -> rhi::BufferHandle {
        rhi::BufferDesc bd;
        bd.size = size;
        bd.usage = usage;
        bd.memoryUsage = rhi::MemoryUsage::GPU_Only;
        bd.debugName = name;
        return m_device->CreateBuffer(bd);
    };

    auto storageXfer = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst;

    m_instanceBuffer = createBuf(
        sizeof(InstanceGPUData) * desc.maxInstances, storageXfer, "VB_InstanceBuffer");

    m_visibleInstanceBuffer = createBuf(
        sizeof(u32) * desc.maxInstances,
        rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect,
        "VB_VisibleInstances");

    m_visibleCountBuffer = createBuf(
        sizeof(u32) * 4, // counters for instances, meshlets, draws
        rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst | rhi::BufferUsage::TransferSrc,
        "VB_VisibleCount");

    m_drawCommandBuffer = createBuf(
        sizeof(u32) * 4 * desc.maxDrawCommands, // VkDrawIndirectCommand per entry
        rhi::BufferUsage::Storage | rhi::BufferUsage::Indirect,
        "VB_DrawCommands");

    // Meshlet descriptors are uploaded from the virtual geometry system
    m_meshletBuffer = createBuf(
        64 * desc.maxMeshlets, // ~64 bytes per meshlet descriptor
        storageXfer, "VB_MeshletBuffer");
}

void VisibilityRenderer::CreatePipelines() {
    // TODO: Create compute and graphics pipelines when shader compilation is wired up
    // For now, pipelines will be created in the render pipeline init after shaders are compiled
    NGE_LOG_DEBUG("Visibility renderer pipelines (deferred until shader compilation)");
}

void VisibilityRenderer::SetInstances([[maybe_unused]] const InstanceGPUData* instances, u32 count) {
    m_instanceCount = count;
    // TODO: Upload instance data to GPU via staging buffer
    // m_device->UploadBufferData(m_instanceBuffer, instances, count * sizeof(InstanceGPUData));
}

void VisibilityRenderer::Render(rhi::ICommandList* cmd,
                                 rhi::TextureHandle visBuffer,
                                 rhi::TextureHandle depthBuffer,
                                 rhi::TextureHandle hzbTexture,
                                 const math::Mat4& viewProj,
                                 const math::Vec3& cameraPos,
                                 const math::Vec4 frustumPlanes[6],
                                 u32 width, u32 height) {
    cmd->BeginDebugLabel("Visibility Pipeline", 0.2f, 0.4f, 0.9f);

    // Reset counters
    // TODO: cmd->FillBuffer(m_visibleCountBuffer, 0, sizeof(u32) * 4, 0);

    // Phase A: Cull against previous frame's HZB
    PassInstanceCulling(cmd, viewProj, frustumPlanes, hzbTexture, width / 2, height / 2);

    // Phase A: Cull meshlets of surviving instances
    PassMeshletCulling(cmd, viewProj, cameraPos, frustumPlanes);

    // Phase A: Rasterize visible meshlets to visibility buffer
    PassVisibilityRaster(cmd, visBuffer, depthBuffer, viewProj, width, height);

    // Rebuild HZB from new depth
    PassHZBBuild(cmd, depthBuffer, hzbTexture);

    if (m_desc.enableTwoPhaseOcclusion) {
        // Phase B: Re-test rejected instances against new HZB
        // TODO: separate rejected list, re-cull, re-raster
    }

    // Material resolve: shade visible pixels using vis buffer data
    PassMaterialResolve(cmd, visBuffer, depthBuffer, width, height);

    cmd->EndDebugLabel();
}

void VisibilityRenderer::PassInstanceCulling(rhi::ICommandList* cmd,
                                              const math::Mat4& /*viewProj*/,
                                              const math::Vec4 /*frustumPlanes*/[6],
                                              rhi::TextureHandle /*hzb*/,
                                              u32 /*hzbWidth*/, u32 /*hzbHeight*/) {
    cmd->BeginDebugLabel("Instance Culling", 0.3f, 0.8f, 0.3f);

    if (m_instanceCullPipeline.IsValid()) {
        cmd->BindComputePipeline(m_instanceCullPipeline);
        // TODO: Bind push constants with viewProj, frustum planes, HZB info
        // TODO: Dispatch ceil(instanceCount / 64) groups
        u32 groupCount = (m_instanceCount + 63) / 64;
        cmd->Dispatch(groupCount, 1, 1);
    }

    // Barrier: compute write → compute read
    cmd->BufferBarrier(m_visibleInstanceBuffer,
                        rhi::ResourceState::ShaderWrite,
                        rhi::ResourceState::ShaderRead);

    cmd->EndDebugLabel();
}

void VisibilityRenderer::PassMeshletCulling(rhi::ICommandList* cmd,
                                              const math::Mat4& /*viewProj*/,
                                              const math::Vec3& /*cameraPos*/,
                                              const math::Vec4 /*frustumPlanes*/[6]) {
    cmd->BeginDebugLabel("Meshlet Culling", 0.3f, 0.6f, 0.8f);

    if (m_meshletCullPipeline.IsValid()) {
        cmd->BindComputePipeline(m_meshletCullPipeline);
        // TODO: Dispatch based on visible instance count (indirect dispatch)
    }

    cmd->EndDebugLabel();
}

void VisibilityRenderer::PassVisibilityRaster(rhi::ICommandList* cmd,
                                                rhi::TextureHandle visBuffer,
                                                rhi::TextureHandle depthBuffer,
                                                const math::Mat4& /*viewProj*/,
                                                u32 width, u32 height) {
    cmd->BeginDebugLabel("Visibility Raster", 0.2f, 0.2f, 0.9f);

    rhi::Viewport viewport{0, 0, static_cast<f32>(width), static_cast<f32>(height), 0, 1};
    rhi::Scissor scissor{0, 0, width, height};
    rhi::ClearValue clearVis = rhi::ClearValue::Color(0.0f, 0.0f, 0.0f, 0.0f);
    rhi::LoadOp loadOp = rhi::LoadOp::Clear;

    // Transition vis buffer and depth to render targets
    cmd->TextureBarrier(visBuffer, rhi::ResourceState::ShaderRead, rhi::ResourceState::RenderTarget);
    cmd->TextureBarrier(depthBuffer, rhi::ResourceState::ShaderRead, rhi::ResourceState::DepthWrite);

    cmd->BeginRendering(&visBuffer, 1, depthBuffer, &clearVis, viewport, scissor, &loadOp);

    if (m_visBufferPipeline.IsValid()) {
        cmd->BindGraphicsPipeline(m_visBufferPipeline);
        // Mesh shader path: DispatchMesh from draw command buffer (indirect)
        // Vertex path: DrawIndexedIndirect from draw command buffer
        // TODO: cmd->DrawIndirect(m_drawCommandBuffer, 0, drawCount);
    }

    cmd->EndRendering();

    // Transition back
    cmd->TextureBarrier(visBuffer, rhi::ResourceState::RenderTarget, rhi::ResourceState::ShaderRead);
    cmd->TextureBarrier(depthBuffer, rhi::ResourceState::DepthWrite, rhi::ResourceState::ShaderRead);

    cmd->EndDebugLabel();
}

void VisibilityRenderer::PassHZBBuild(rhi::ICommandList* cmd,
                                        rhi::TextureHandle /*depthBuffer*/,
                                        rhi::TextureHandle /*hzbTexture*/) {
    cmd->BeginDebugLabel("HZB Build", 0.5f, 0.5f, 0.2f);

    // Downsample depth buffer into HZB mip chain
    // Each mip level takes the MAX of 2x2 region from previous level
    // This is done via a compute shader dispatched per mip level
    // TODO: Bind HZB compute pipeline, dispatch per mip

    cmd->EndDebugLabel();
}

void VisibilityRenderer::PassMaterialResolve(rhi::ICommandList* cmd,
                                               rhi::TextureHandle /*visBuffer*/,
                                               rhi::TextureHandle /*depthBuffer*/,
                                               u32 width, u32 height) {
    cmd->BeginDebugLabel("Material Resolve", 0.9f, 0.4f, 0.2f);

    if (m_materialResolvePipeline.IsValid()) {
        cmd->BindComputePipeline(m_materialResolvePipeline);
        // Full-screen compute: one thread per pixel
        // Reads visibility buffer → reconstructs position/normal/UVs from mesh data
        // Evaluates material → writes to G-buffer outputs or direct lighting
        u32 groupsX = (width + 7) / 8;
        u32 groupsY = (height + 7) / 8;
        cmd->Dispatch(groupsX, groupsY, 1);
    }

    cmd->EndDebugLabel();
}

} // namespace nge::renderer
