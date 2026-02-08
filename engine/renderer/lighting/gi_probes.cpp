#include "engine/renderer/lighting/gi_probes.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"
#include <algorithm>

namespace nge::renderer {

bool GIProbeSystem::Init(rhi::IDevice* device, const ProbeGridConfig& config) {
    m_device = device;
    m_config = config;

    u32 totalProbes = GetTotalProbes();
    m_probeData.resize(totalProbes);
    for (auto& p : m_probeData) {
        p.shR = SH9::Zero();
        p.shG = SH9::Zero();
        p.shB = SH9::Zero();
    }

    // Probe SH storage buffer
    rhi::BufferDesc probeDesc;
    probeDesc.size        = sizeof(GPUProbeData) * totalProbes;
    probeDesc.usage       = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst;
    probeDesc.memoryUsage = rhi::MemoryUsage::GPU_Only;
    probeDesc.debugName   = "GI_ProbeBuffer";
    m_probeBuffer = device->CreateBuffer(probeDesc);

    // Grid info uniform buffer
    rhi::BufferDesc gridDesc;
    gridDesc.size        = sizeof(GPUProbeGridInfo);
    gridDesc.usage       = rhi::BufferUsage::Uniform | rhi::BufferUsage::TransferDst;
    gridDesc.memoryUsage = rhi::MemoryUsage::CPU_To_GPU;
    gridDesc.debugName   = "GI_GridInfo";
    m_gridInfoBuffer = device->CreateBuffer(gridDesc);

    // Ray result staging buffer (for probe tracing output)
    u32 maxRaysPerFrame = config.probesPerFrame * config.raysPerProbe;
    rhi::BufferDesc rayDesc;
    rayDesc.size        = sizeof(f32) * 4 * maxRaysPerFrame; // float4 per ray (rgb radiance + distance)
    rayDesc.usage       = rhi::BufferUsage::Storage;
    rayDesc.memoryUsage = rhi::MemoryUsage::GPU_Only;
    rayDesc.debugName   = "GI_RayResults";
    m_rayResultBuffer = device->CreateBuffer(rayDesc);

    // Upload initial grid info
    GPUProbeGridInfo gridInfo{};
    gridInfo.origin = {config.origin.x, config.origin.y, config.origin.z, 0};
    gridInfo.spacing = {config.spacing.x, config.spacing.y, config.spacing.z, 0};
    gridInfo.countX = config.countX;
    gridInfo.countY = config.countY;
    gridInfo.countZ = config.countZ;
    gridInfo.totalProbes = totalProbes;
    math::Vec3 gridMax = config.origin + math::Vec3(
        static_cast<f32>(config.countX - 1) * config.spacing.x,
        static_cast<f32>(config.countY - 1) * config.spacing.y,
        static_cast<f32>(config.countZ - 1) * config.spacing.z);
    gridInfo.gridMin = {config.origin.x, config.origin.y, config.origin.z, 0};
    gridInfo.gridMax = {gridMax.x, gridMax.y, gridMax.z, 0};

    device->UpdateBuffer(m_gridInfoBuffer, &gridInfo, sizeof(gridInfo));

    NGE_LOG_INFO("GI probe system initialized: {}x{}x{} = {} probes, {} rays/probe, {} probes/frame",
                 config.countX, config.countY, config.countZ, totalProbes,
                 config.raysPerProbe, config.probesPerFrame);
    return true;
}

void GIProbeSystem::Shutdown() {
    if (!m_device) return;

    auto destroyBuf = [&](rhi::BufferHandle& h) {
        if (h.IsValid()) { m_device->DestroyBuffer(h); h = rhi::BufferHandle{}; }
    };

    destroyBuf(m_probeBuffer);
    destroyBuf(m_gridInfoBuffer);
    destroyBuf(m_rayResultBuffer);
    m_probeData.clear();
}

void GIProbeSystem::SetGridConfig(const ProbeGridConfig& config) {
    Shutdown();
    Init(m_device, config);
}

math::Vec3 GIProbeSystem::ProbeWorldPos(u32 ix, u32 iy, u32 iz) const {
    return m_config.origin + math::Vec3(
        static_cast<f32>(ix) * m_config.spacing.x,
        static_cast<f32>(iy) * m_config.spacing.y,
        static_cast<f32>(iz) * m_config.spacing.z);
}

void GIProbeSystem::SelectProbesToUpdate(const math::Vec3& cameraPos) {
    // Distance-biased round-robin: probes closer to camera update more frequently
    // Simple approach: round-robin with wraparound
    u32 total = GetTotalProbes();
    m_probesUpdatedThisFrame = math::Min(m_config.probesPerFrame, total);

    // Advance offset
    m_updateOffset = (m_updateOffset + m_probesUpdatedThisFrame) % total;

    (void)cameraPos; // TODO: weight by distance for priority
}

void GIProbeSystem::Update(rhi::ICommandList* cmd,
                            const math::Vec3& cameraPos,
                            rhi::AccelStructHandle /*tlas*/) {
    SelectProbesToUpdate(cameraPos);

    cmd->BeginDebugLabel("GI Probe Update", 0.4f, 0.9f, 0.4f);

    // Pass 1: Trace rays from selected probes
    if (m_traceProbesPipeline.IsValid()) {
        cmd->BindComputePipeline(m_traceProbesPipeline);
        // Push constants: probe offset, probe count, rays per probe, frame seed
        u32 totalRays = m_probesUpdatedThisFrame * m_config.raysPerProbe;
        cmd->Dispatch((totalRays + 63) / 64, 1, 1);

        cmd->BufferBarrier(m_rayResultBuffer,
                            rhi::ResourceState::ShaderWrite,
                            rhi::ResourceState::ShaderRead);
    }

    // Pass 2: Integrate ray results into SH coefficients
    if (m_updateProbesPipeline.IsValid()) {
        cmd->BindComputePipeline(m_updateProbesPipeline);
        // One thread per updated probe
        cmd->Dispatch((m_probesUpdatedThisFrame + 63) / 64, 1, 1);

        cmd->BufferBarrier(m_probeBuffer,
                            rhi::ResourceState::ShaderWrite,
                            rhi::ResourceState::ShaderRead);
    }

    cmd->EndDebugLabel();
}

} // namespace nge::renderer
