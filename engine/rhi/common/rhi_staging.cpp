#include "engine/rhi/common/rhi_staging.h"
#include "engine/core/logging/log.h"
#include <cstring>
#include <algorithm>

namespace nge::rhi {

bool StagingManager::Init(IDevice* device, const Config& config) {
    m_device = device;
    m_config = config;
    m_currentOffset = 0;

    // Create initial staging buffer
    BufferDesc desc;
    desc.size = config.initialSize;
    desc.usage = BufferUsage::TransferSrc;
    desc.memoryUsage = MemoryUsage::CPU_To_GPU;
    desc.debugName = "StagingBuffer";
    m_stagingBuffer = device->CreateBuffer(desc);
    m_mappedPtr = device->MapBuffer(m_stagingBuffer);
    m_currentSize = config.initialSize;

    NGE_LOG_INFO("Staging manager initialized: {} MB initial", config.initialSize / (1024 * 1024));
    return true;
}

void StagingManager::Shutdown() {
    if (!m_device) return;

    if (m_mappedPtr) {
        m_device->UnmapBuffer(m_stagingBuffer);
        m_mappedPtr = nullptr;
    }
    if (m_stagingBuffer.IsValid()) {
        m_device->DestroyBuffer(m_stagingBuffer);
        m_stagingBuffer = {};
    }

    m_bufferCopies.clear();
    m_textureCopies.clear();
}

void StagingManager::BeginFrame() {
    m_currentOffset = 0;
    m_bufferCopies.clear();
    m_textureCopies.clear();
}

bool StagingManager::StageBuffer(BufferHandle dst, u32 dstOffset, const void* data, u32 size) {
    std::lock_guard lock(m_mutex);

    if (!EnsureCapacity(size)) return false;

    // Copy data into staging buffer
    u8* dest = static_cast<u8*>(m_mappedPtr) + m_currentOffset;
    std::memcpy(dest, data, size);

    StagingBufferCopy copy;
    copy.dstBuffer = dst;
    copy.dstOffset = dstOffset;
    copy.srcOffset = m_currentOffset;
    copy.size = size;
    m_bufferCopies.push_back(copy);

    m_currentOffset += size;
    // Align to 16 bytes for next allocation
    m_currentOffset = (m_currentOffset + 15) & ~15u;

    return true;
}

bool StagingManager::StageTexture(TextureHandle dst, u32 mipLevel, u32 arrayLayer,
                                    u32 width, u32 height, const void* data, u32 size) {
    std::lock_guard lock(m_mutex);

    if (!EnsureCapacity(size)) return false;

    u8* dest = static_cast<u8*>(m_mappedPtr) + m_currentOffset;
    std::memcpy(dest, data, size);

    StagingTextureCopy copy;
    copy.dstTexture = dst;
    copy.mipLevel = mipLevel;
    copy.arrayLayer = arrayLayer;
    copy.width = width;
    copy.height = height;
    copy.srcOffset = m_currentOffset;
    copy.size = size;
    m_textureCopies.push_back(copy);

    m_currentOffset += size;
    m_currentOffset = (m_currentOffset + 15) & ~15u;

    return true;
}

void StagingManager::Flush(ICommandList* cmd) {
    if (m_bufferCopies.empty() && m_textureCopies.empty()) return;

    // Buffer copies
    for (const auto& copy : m_bufferCopies) {
        cmd->CopyBuffer(m_stagingBuffer, copy.srcOffset, copy.dstBuffer, copy.dstOffset, copy.size);
    }

    // Texture copies
    for (const auto& copy : m_textureCopies) {
        cmd->CopyBufferToTexture(m_stagingBuffer, copy.srcOffset,
                                  copy.dstTexture, copy.mipLevel, copy.arrayLayer,
                                  copy.width, copy.height);
    }

    NGE_LOG_DEBUG("Staging flush: {} buffer copies, {} texture copies, {} KB used",
                  m_bufferCopies.size(), m_textureCopies.size(), m_currentOffset / 1024);

    m_bufferCopies.clear();
    m_textureCopies.clear();
}

bool StagingManager::EnsureCapacity(u32 additionalBytes) {
    u32 needed = m_currentOffset + additionalBytes;
    if (needed <= m_currentSize) return true;

    // Grow
    u32 newSize = m_currentSize;
    while (newSize < needed) {
        newSize *= m_config.growFactor;
    }
    newSize = std::min(newSize, m_config.maxSize);

    if (needed > newSize) {
        NGE_LOG_ERROR("Staging buffer max size exceeded ({} MB)", m_config.maxSize / (1024 * 1024));
        return false;
    }

    GrowStagingBuffer(newSize);
    return true;
}

void StagingManager::GrowStagingBuffer(u32 newSize) {
    NGE_LOG_INFO("Growing staging buffer: {} MB -> {} MB",
                 m_currentSize / (1024 * 1024), newSize / (1024 * 1024));

    // Unmap old buffer
    if (m_mappedPtr) {
        m_device->UnmapBuffer(m_stagingBuffer);
    }

    // Create new, larger buffer
    BufferDesc desc;
    desc.size = newSize;
    desc.usage = BufferUsage::TransferSrc;
    desc.memoryUsage = MemoryUsage::CPU_To_GPU;
    desc.debugName = "StagingBuffer";
    BufferHandle newBuffer = m_device->CreateBuffer(desc);
    void* newMapped = m_device->MapBuffer(newBuffer);

    // Copy existing data
    if (m_currentOffset > 0 && m_mappedPtr && newMapped) {
        std::memcpy(newMapped, m_mappedPtr, m_currentOffset);
    }

    // Destroy old
    m_device->DestroyBuffer(m_stagingBuffer);

    m_stagingBuffer = newBuffer;
    m_mappedPtr = newMapped;
    m_currentSize = newSize;
}

} // namespace nge::rhi
