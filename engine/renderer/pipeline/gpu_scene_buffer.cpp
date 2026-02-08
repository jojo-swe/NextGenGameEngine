#include "engine/renderer/pipeline/gpu_scene_buffer.h"
#include "engine/core/logging/log.h"
#include <cstring>

namespace nge::renderer {

bool GPUSceneBuffer::Init(rhi::IDevice* device, const GPUSceneConfig& config) {
    m_device = device;
    m_config = config;

    // Instance buffer (current frame transforms)
    {
        rhi::BufferDesc desc;
        desc.size = config.maxInstances * sizeof(GPUSceneInstance);
        desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst;
        desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        desc.debugName = "GPUSceneInstances";
        m_instanceBuffer = device->CreateBuffer(desc);
    }

    // Previous frame instance buffer (for motion vectors)
    {
        rhi::BufferDesc desc;
        desc.size = config.maxInstances * sizeof(GPUSceneInstance);
        desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst;
        desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        desc.debugName = "GPUScenePrevInstances";
        m_prevInstanceBuffer = device->CreateBuffer(desc);
    }

    // Material buffer
    {
        rhi::BufferDesc desc;
        desc.size = config.maxMaterials * sizeof(GPUSceneMaterial);
        desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst;
        desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        desc.debugName = "GPUSceneMaterials";
        m_materialBuffer = device->CreateBuffer(desc);
    }

    // Staging buffer (large enough for instances + materials)
    {
        u64 instanceSize = config.maxInstances * sizeof(GPUSceneInstance);
        u64 materialSize = config.maxMaterials * sizeof(GPUSceneMaterial);
        rhi::BufferDesc desc;
        desc.size = instanceSize + materialSize;
        desc.usage = rhi::BufferUsage::TransferSrc;
        desc.memoryUsage = rhi::MemoryUsage::CPU_To_GPU;
        desc.debugName = "GPUSceneStaging";
        m_stagingBuffer = device->CreateBuffer(desc);
    }

    m_cpuInstances.reserve(config.maxInstances);
    m_cpuMaterials.reserve(config.maxMaterials);

    NGE_LOG_INFO("GPU scene buffer initialized: {} instances, {} materials",
                 config.maxInstances, config.maxMaterials);
    return true;
}

void GPUSceneBuffer::Shutdown() {
    if (!m_device) return;

    auto destroy = [&](rhi::BufferHandle& h) {
        if (h.IsValid()) { m_device->DestroyBuffer(h); h = {}; }
    };

    destroy(m_instanceBuffer);
    destroy(m_prevInstanceBuffer);
    destroy(m_materialBuffer);
    destroy(m_stagingBuffer);

    m_cpuInstances.clear();
    m_cpuMaterials.clear();
}

void GPUSceneBuffer::BeginFrame() {
    std::lock_guard lock(m_mutex);

    // Swap current → previous (for motion vectors)
    std::swap(m_instanceBuffer, m_prevInstanceBuffer);

    m_cpuInstances.clear();
    m_cpuMaterials.clear();
    m_instanceCount = 0;
    m_materialCount = 0;
    m_instancesDirty = false;
    m_materialsDirty = false;
}

void GPUSceneBuffer::SetInstances(const GPUSceneInstance* data, u32 count) {
    std::lock_guard lock(m_mutex);
    count = std::min(count, m_config.maxInstances);
    m_cpuInstances.assign(data, data + count);
    m_instanceCount = count;
    m_instancesDirty = true;
}

void GPUSceneBuffer::SetMaterials(const GPUSceneMaterial* data, u32 count) {
    std::lock_guard lock(m_mutex);
    count = std::min(count, m_config.maxMaterials);
    m_cpuMaterials.assign(data, data + count);
    m_materialCount = count;
    m_materialsDirty = true;
}

void GPUSceneBuffer::Upload(rhi::ICommandList* cmd) {
    std::lock_guard lock(m_mutex);

    void* mapped = m_device->MapBuffer(m_stagingBuffer);
    if (!mapped) return;

    u64 offset = 0;

    // Upload instances
    if (m_instancesDirty && m_instanceCount > 0) {
        u64 size = m_instanceCount * sizeof(GPUSceneInstance);
        std::memcpy(static_cast<u8*>(mapped) + offset, m_cpuInstances.data(), size);
        cmd->CopyBuffer(m_stagingBuffer, offset, m_instanceBuffer, 0, size);
        offset += size;
    }

    // Upload materials
    if (m_materialsDirty && m_materialCount > 0) {
        u64 size = m_materialCount * sizeof(GPUSceneMaterial);
        std::memcpy(static_cast<u8*>(mapped) + offset, m_cpuMaterials.data(), size);
        cmd->CopyBuffer(m_stagingBuffer, offset, m_materialBuffer, 0, size);
        offset += size;
    }

    m_device->UnmapBuffer(m_stagingBuffer);

    // Barriers: transfer → shader read
    if (m_instancesDirty) {
        cmd->BufferBarrier(m_instanceBuffer, rhi::ResourceState::TransferDst, rhi::ResourceState::ShaderRead);
    }
    if (m_materialsDirty) {
        cmd->BufferBarrier(m_materialBuffer, rhi::ResourceState::TransferDst, rhi::ResourceState::ShaderRead);
    }

    m_instancesDirty = false;
    m_materialsDirty = false;
}

void GPUSceneBuffer::Bind(rhi::ICommandList* cmd) const {
    // TODO: Bind as SRVs at well-known binding slots
    // cmd->BindBuffer(SCENE_INSTANCE_SLOT, m_instanceBuffer);
    // cmd->BindBuffer(SCENE_PREV_INSTANCE_SLOT, m_prevInstanceBuffer);
    // cmd->BindBuffer(SCENE_MATERIAL_SLOT, m_materialBuffer);
    (void)cmd;
}

} // namespace nge::renderer
