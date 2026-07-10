#include "engine/renderer/lighting/light_culling.h"
#include "engine/core/logging/log.h"
#include <cstring>
#include <cmath>

namespace nge::renderer {

bool LightCullingSystem::Init(rhi::IDevice* device, const ClusterGridConfig& config) {
    m_device = device;
    m_config = config;

    u32 totalClusters = GetTotalClusters();

    if (!device) {
        NGE_LOG_WARN("LightCullingSystem::Init: null device — CPU-only mode, no GPU buffers created (tests only)");
    }

    if (device) {
        // Light structured buffer
        {
            rhi::BufferDesc desc;
            desc.size = MAX_LIGHTS * sizeof(GPULightData);
            desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst;
            desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
            desc.debugName = "LightBuffer";
            m_lightBuffer = device->CreateBuffer(desc);
        }

        // Cluster AABB buffer (built once or on resize)
        {
            rhi::BufferDesc desc;
            desc.size = totalClusters * 32; // 2x float4 per cluster (min + max)
            desc.usage = rhi::BufferUsage::Storage;
            desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
            desc.debugName = "ClusterAABBs";
            m_clusterBuffer = device->CreateBuffer(desc);
        }

        // Light grid: per-cluster offset + count (2x u32 = 8 bytes per cluster)
        {
            rhi::BufferDesc desc;
            desc.size = totalClusters * 8;
            desc.usage = rhi::BufferUsage::Storage;
            desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
            desc.debugName = "LightGrid";
            m_lightGridBuffer = device->CreateBuffer(desc);
        }

        // Light index list (worst case: every light in every cluster)
        {
            rhi::BufferDesc desc;
            desc.size = totalClusters * config.maxLightsPerCluster * sizeof(u32);
            desc.usage = rhi::BufferUsage::Storage;
            desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
            desc.debugName = "LightIndexList";
            m_lightIndexBuffer = device->CreateBuffer(desc);
        }

        // Staging buffer for light upload
        {
            rhi::BufferDesc desc;
            desc.size = MAX_LIGHTS * sizeof(GPULightData);
            desc.usage = rhi::BufferUsage::TransferSrc;
            desc.memoryUsage = rhi::MemoryUsage::CPU_To_GPU;
            desc.debugName = "LightStaging";
            m_stagingBuffer = device->CreateBuffer(desc);
        }
    }

    m_lights.reserve(MAX_LIGHTS);

    NGE_LOG_INFO("Light culling initialized: {}x{}x{} grid ({} clusters), max {} lights",
                 config.tilesX, config.tilesY, config.slices, totalClusters, MAX_LIGHTS);
    return true;
}

void LightCullingSystem::Shutdown() {
    if (!m_device) return;

    auto destroy = [&](rhi::BufferHandle& h) {
        if (h.IsValid()) { m_device->DestroyBuffer(h); h = {}; }
    };

    destroy(m_lightBuffer);
    destroy(m_clusterBuffer);
    destroy(m_lightGridBuffer);
    destroy(m_lightIndexBuffer);
    destroy(m_stagingBuffer);

    m_lights.clear();
}

void LightCullingSystem::BeginFrame() {
    m_lights.clear();
    m_hasDirectional = false;
    m_pointLightCount = 0;
    m_spotLightCount = 0;
}

u32 LightCullingSystem::AddLight(const LightInfo& light) {
    if (!light.enabled) return UINT32_MAX;

    if (m_lights.size() >= MAX_LIGHTS) {
        NGE_LOG_WARN("Light limit reached ({} max)", MAX_LIGHTS);
        return UINT32_MAX;
    }

    u32 index = static_cast<u32>(m_lights.size());
    m_lights.push_back(ConvertLight(light));

    switch (light.type) {
        case LightType::Point:    m_pointLightCount++; break;
        case LightType::Spot:     m_spotLightCount++; break;
        default: break;
    }

    return index;
}

void LightCullingSystem::SetDirectionalLight(const LightInfo& light) {
    m_directionalLight = ConvertLight(light);
    m_hasDirectional = true;
}

void LightCullingSystem::Upload(rhi::ICommandList* cmd) {
    if (m_lights.empty()) return;

    u32 uploadSize = static_cast<u32>(m_lights.size() * sizeof(GPULightData));

    void* mapped = m_device->MapBuffer(m_stagingBuffer);
    if (mapped) {
        std::memcpy(mapped, m_lights.data(), uploadSize);
        m_device->UnmapBuffer(m_stagingBuffer);
    }

    cmd->CopyBuffer(m_stagingBuffer, 0, m_lightBuffer, 0, uploadSize);
    cmd->BufferBarrier(m_lightBuffer, rhi::ResourceState::TransferDst, rhi::ResourceState::ShaderRead);
}

void LightCullingSystem::AssignClusters(rhi::ICommandList* cmd, rhi::TextureHandle depthBuffer) {
    // Step 1: Build cluster AABBs (only needed on first frame or resize)
    // cmd->BindComputePipeline(m_clusterBuildPipeline);
    // cmd->Dispatch(tilesX, tilesY, slices);

    // Step 2: Assign lights to clusters
    // For each cluster, test each light's bounding volume against the cluster AABB.
    // Write (offset, count) into lightGrid and light indices into lightIndexList.
    // cmd->BindComputePipeline(m_lightAssignPipeline);
    // cmd->BindBuffer(0, m_lightBuffer);
    // cmd->BindBuffer(1, m_clusterBuffer);
    // cmd->BindBuffer(2, m_lightGridBuffer);
    // cmd->BindBuffer(3, m_lightIndexBuffer);
    // cmd->BindTexture(4, depthBuffer);
    // cmd->Dispatch(tilesX, tilesY, slices);

    cmd->BufferBarrier(m_lightGridBuffer, rhi::ResourceState::ShaderWrite, rhi::ResourceState::ShaderRead);
    cmd->BufferBarrier(m_lightIndexBuffer, rhi::ResourceState::ShaderWrite, rhi::ResourceState::ShaderRead);

    (void)depthBuffer;
}

u32 LightCullingSystem::GetTotalClusters() const {
    return m_config.tilesX * m_config.tilesY * m_config.slices;
}

GPULightData LightCullingSystem::ConvertLight(const LightInfo& info) const {
    GPULightData data;
    data.positionAndRange = {info.position.x, info.position.y, info.position.z, info.range};
    data.directionAndAngle = {info.direction.x, info.direction.y, info.direction.z,
                               std::cos(info.outerAngle)};
    data.colorAndIntensity = {info.color.x, info.color.y, info.color.z, info.intensity};
    data.params = {std::cos(info.innerAngle),
                   static_cast<f32>(info.type),
                   static_cast<f32>(info.shadowMapIndex),
                   info.castShadow ? 1.0f : 0.0f};
    return data;
}

} // namespace nge::renderer
