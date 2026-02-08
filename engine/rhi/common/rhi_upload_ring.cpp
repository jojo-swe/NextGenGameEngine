#include "engine/rhi/common/rhi_upload_ring.h"
#include "engine/core/logging/log.h"
#include <cstring>

namespace nge::rhi {

bool UploadRingBuffer::Init(IDevice* device, const Config& config) {
    m_device = device;
    m_config = config;

    u64 totalSize = config.sizePerFrame * config.framesInFlight;

    BufferDesc desc;
    desc.size = totalSize;
    desc.usage = BufferUsage::Constant | BufferUsage::Storage | BufferUsage::TransferSrc;
    desc.memoryUsage = MemoryUsage::CPU_To_GPU;
    desc.debugName = "UploadRingBuffer";
    m_buffer = device->CreateBuffer(desc);

    // Persistently map the entire buffer
    m_mappedPtr = static_cast<u8*>(device->MapBuffer(m_buffer));
    if (!m_mappedPtr) {
        NGE_LOG_ERROR("Upload ring buffer: failed to map buffer");
        return false;
    }

    NGE_LOG_INFO("Upload ring buffer initialized: {} MB total ({} MB × {} frames)",
                 totalSize / (1024 * 1024), config.sizePerFrame / (1024 * 1024), config.framesInFlight);
    return true;
}

void UploadRingBuffer::Shutdown() {
    if (m_device && m_buffer.IsValid()) {
        m_device->UnmapBuffer(m_buffer);
        m_device->DestroyBuffer(m_buffer);
        m_buffer = {};
    }
    m_mappedPtr = nullptr;
}

void UploadRingBuffer::BeginFrame(u32 frameIndex) {
    std::lock_guard lock(m_mutex);
    m_currentFrame = frameIndex % m_config.framesInFlight;
    m_frameBaseOffset = static_cast<u64>(m_currentFrame) * m_config.sizePerFrame;
    m_frameOffset = 0;
}

UploadAllocation UploadRingBuffer::Allocate(u32 sizeBytes, u32 alignment) {
    std::lock_guard lock(m_mutex);
    UploadAllocation alloc;

    if (alignment == 0) alignment = m_config.alignment;

    // Align the current offset
    u64 aligned = (m_frameOffset + alignment - 1) & ~(static_cast<u64>(alignment) - 1);

    if (aligned + sizeBytes > m_config.sizePerFrame) {
        NGE_LOG_WARN("Upload ring buffer: frame capacity exhausted ({} / {} bytes)",
                     aligned + sizeBytes, m_config.sizePerFrame);
        return alloc;
    }

    alloc.cpuAddress = m_mappedPtr + m_frameBaseOffset + aligned;
    alloc.gpuOffset = m_frameBaseOffset + aligned;
    alloc.size = sizeBytes;
    alloc.valid = true;

    m_frameOffset = aligned + sizeBytes;
    return alloc;
}

UploadAllocation UploadRingBuffer::Upload(const void* data, u32 sizeBytes, u32 alignment) {
    auto alloc = Allocate(sizeBytes, alignment);
    if (alloc.valid && data) {
        std::memcpy(alloc.cpuAddress, data, sizeBytes);
    }
    return alloc;
}

f32 UploadRingBuffer::GetUtilization() const {
    return m_config.sizePerFrame > 0
        ? static_cast<f32>(m_frameOffset) / static_cast<f32>(m_config.sizePerFrame)
        : 0.0f;
}

} // namespace nge::rhi
