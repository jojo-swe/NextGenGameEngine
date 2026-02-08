#include "engine/rhi/common/rhi_dynamic_buffer.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <cstring>

namespace nge::rhi {

bool DynamicBufferAllocator::Init(IDevice* device, const DynamicBufferConfig& config) {
    m_device = device;
    m_config = config;
    m_peakUsage = 0;
    m_allocationsThisFrame = 0;

    // TODO: Create large persistently mapped buffer
    // BufferDesc desc{};
    // desc.size = config.bufferSizeBytes;
    // desc.usage = BufferUsage::UniformBuffer | BufferUsage::StorageBuffer | BufferUsage::TransferDst;
    // desc.memoryType = MemoryType::HostVisible | MemoryType::HostCoherent;
    // m_buffer = device->CreateBuffer(desc);
    // vkMapMemory(device, bufferMemory, 0, config.bufferSizeBytes, 0, &m_mappedBase);

    // Divide buffer into per-frame regions
    u64 regionSize = config.bufferSizeBytes / config.framesInFlight;
    m_frameRegions.resize(config.framesInFlight);
    for (u32 f = 0; f < config.framesInFlight; ++f) {
        m_frameRegions[f].start = f * regionSize;
        m_frameRegions[f].current = f * regionSize;
        m_frameRegions[f].end = (f + 1) * regionSize;
    }

    NGE_LOG_INFO("Dynamic buffer allocator initialized: {}MB, {} frames, {}B alignment",
                 config.bufferSizeBytes / (1024 * 1024), config.framesInFlight, config.minAlignment);
    return true;
}

void DynamicBufferAllocator::Shutdown() {
    // TODO: vkUnmapMemory(device, bufferMemory);
    // device->DestroyBuffer(m_buffer);
    m_mappedBase = nullptr;
    m_frameRegions.clear();
}

void DynamicBufferAllocator::BeginFrame(u32 frameIndex) {
    std::lock_guard lock(m_mutex);
    m_currentFrame = frameIndex % m_config.framesInFlight;

    // Reset this frame's region
    auto& region = m_frameRegions[m_currentFrame];
    region.current = region.start;
    m_allocationsThisFrame = 0;
}

DynamicAllocation DynamicBufferAllocator::Allocate(u64 size, u64 alignment) {
    std::lock_guard lock(m_mutex);

    DynamicAllocation alloc{};
    if (size == 0) return alloc;

    u64 align = std::max(alignment, static_cast<u64>(m_config.minAlignment));
    auto& region = m_frameRegions[m_currentFrame];

    u64 alignedOffset = AlignUp(region.current, align);
    u64 end = alignedOffset + size;

    if (end > region.end) {
        NGE_LOG_ERROR("Dynamic buffer out of space: need {} bytes, {} remaining",
                      size, region.end - region.current);
        return alloc;
    }

    alloc.buffer = m_buffer;
    alloc.offset = alignedOffset;
    alloc.size = size;
    alloc.valid = true;

    if (m_mappedBase) {
        alloc.mappedPtr = static_cast<u8*>(m_mappedBase) + alignedOffset;
    }

    region.current = end;
    m_allocationsThisFrame++;

    u64 used = region.current - region.start;
    m_peakUsage = std::max(m_peakUsage, used);

    return alloc;
}

DynamicBufferStats DynamicBufferAllocator::GetStats() const {
    std::lock_guard lock(m_mutex);
    DynamicBufferStats stats{};
    stats.totalSize = m_config.bufferSizeBytes;

    if (m_currentFrame < m_frameRegions.size()) {
        const auto& region = m_frameRegions[m_currentFrame];
        stats.usedThisFrame = region.current - region.start;
    }

    stats.peakUsage = m_peakUsage;
    stats.allocationsThisFrame = m_allocationsThisFrame;

    u64 regionSize = m_config.bufferSizeBytes / m_config.framesInFlight;
    stats.utilizationPercent = regionSize > 0
        ? static_cast<f32>(stats.usedThisFrame) / static_cast<f32>(regionSize) * 100.0f
        : 0.0f;

    return stats;
}

} // namespace nge::rhi
