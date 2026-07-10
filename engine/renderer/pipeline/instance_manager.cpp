#include "engine/renderer/pipeline/instance_manager.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <cstring>

namespace nge::renderer {

bool InstanceManager::Init(rhi::IDevice* device, u32 maxInstances) {
    m_device = device;
    m_maxInstances = maxInstances;
    m_currentCount = 0;

    m_cpuBuffer.reserve(maxInstances);

    if (!device) {
        NGE_LOG_WARN("InstanceManager::Init: null device — CPU-only mode, no GPU buffers created (tests only)");
    } else {
        // GPU structured buffer for current frame instances
        {
            rhi::BufferDesc desc;
            desc.size = maxInstances * sizeof(GPUInstanceData);
            desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst;
            desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
            desc.debugName = "InstanceBuffer";
            m_gpuBuffer = device->CreateBuffer(desc);
        }

        // Previous frame buffer (for motion vectors)
        {
            rhi::BufferDesc desc;
            desc.size = maxInstances * sizeof(GPUInstanceData);
            desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst;
            desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
            desc.debugName = "PrevInstanceBuffer";
            m_prevGpuBuffer = device->CreateBuffer(desc);
        }

        // CPU-visible staging buffer
        {
            rhi::BufferDesc desc;
            desc.size = maxInstances * sizeof(GPUInstanceData);
            desc.usage = rhi::BufferUsage::TransferSrc;
            desc.memoryUsage = rhi::MemoryUsage::CPU_To_GPU;
            desc.debugName = "InstanceStaging";
            m_stagingBuffer = device->CreateBuffer(desc);
        }
    }

    NGE_LOG_INFO("Instance manager initialized: max {} instances ({} KB)",
                 maxInstances, maxInstances * sizeof(GPUInstanceData) / 1024);
    return true;
}

void InstanceManager::Shutdown() {
    if (!m_device) return;

    auto destroy = [&](rhi::BufferHandle& h) {
        if (h.IsValid()) { m_device->DestroyBuffer(h); h = {}; }
    };

    destroy(m_gpuBuffer);
    destroy(m_prevGpuBuffer);
    destroy(m_stagingBuffer);
    m_cpuBuffer.clear();
}

void InstanceManager::BeginFrame() {
    // Swap current → previous (for motion vectors)
    std::swap(m_gpuBuffer, m_prevGpuBuffer);

    m_cpuBuffer.clear();
    m_currentCount = 0;
    m_dirty = false;
}

void InstanceManager::EndFrame() {
    // Nothing to do — Upload() should have been called
}

u32 InstanceManager::Submit(const GPUInstanceData& instance) {
    if (m_currentCount >= m_maxInstances) {
        NGE_LOG_WARN("Instance manager full ({} max)", m_maxInstances);
        return UINT32_MAX;
    }

    u32 index = m_currentCount++;
    m_cpuBuffer.push_back(instance);
    m_dirty = true;
    return index;
}

void InstanceManager::Submit(const GPUInstanceData* instances, u32 count) {
    u32 available = m_maxInstances - m_currentCount;
    u32 toSubmit = std::min(count, available);

    if (toSubmit < count) {
        NGE_LOG_WARN("Instance manager: only {} of {} instances submitted (full)", toSubmit, count);
    }

    m_cpuBuffer.insert(m_cpuBuffer.end(), instances, instances + toSubmit);
    m_currentCount += toSubmit;
    m_dirty = true;
}

void InstanceManager::Upload(rhi::ICommandList* cmd) {
    if (!m_dirty || m_currentCount == 0) return;

    // Map staging buffer and copy CPU data
    void* mapped = m_device->MapBuffer(m_stagingBuffer);
    if (mapped) {
        std::memcpy(mapped, m_cpuBuffer.data(), m_currentCount * sizeof(GPUInstanceData));
        m_device->UnmapBuffer(m_stagingBuffer);
    }

    // Copy staging → GPU
    u32 copySize = m_currentCount * sizeof(GPUInstanceData);
    cmd->CopyBuffer(m_stagingBuffer, 0, m_gpuBuffer, 0, copySize);

    // Barrier: transfer → shader read
    cmd->BufferBarrier(m_gpuBuffer, rhi::ResourceState::TransferDst, rhi::ResourceState::ShaderRead);

    m_dirty = false;
}

f32 InstanceManager::GetUtilization() const {
    return m_maxInstances > 0 ? static_cast<f32>(m_currentCount) / static_cast<f32>(m_maxInstances) : 0.0f;
}

void InstanceManager::SortByMaterial() {
    std::sort(m_cpuBuffer.begin(), m_cpuBuffer.end(),
              [](const GPUInstanceData& a, const GPUInstanceData& b) {
                  return a.materialId < b.materialId;
              });
    m_dirty = true;
}

} // namespace nge::renderer
