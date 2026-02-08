#include "engine/rhi/common/rhi_ping_pong_buffer.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool PingPongBufferManager::Init(IDevice* device) {
    m_device = device;
    m_currentIndex = 0;
    m_frameNumber = 0;
    NGE_LOG_INFO("Ping-pong buffer manager initialized");
    return true;
}

void PingPongBufferManager::Shutdown() {
    for (auto& set : m_sets) {
        if (set.alive) {
            // TODO: Destroy all buffers/textures in the set
            // for (auto& buf : set.buffers) device->DestroyBuffer(buf);
            // for (auto& tex : set.textures) device->DestroyTexture(tex);
            set.alive = false;
        }
    }
    m_sets.clear();
}

u32 PingPongBufferManager::CreateBufferSet(const PingPongBufferDesc& desc) {
    std::lock_guard lock(m_mutex);

    BufferSet set;
    set.mode = desc.mode;
    set.sizeBytes = desc.sizeBytes;
    set.debugName = desc.debugName;
    set.isTexture = false;
    set.alive = true;

    u32 count = static_cast<u32>(desc.mode);
    set.buffers.resize(count);
    set.mappedPtrs.resize(count, nullptr);

    for (u32 i = 0; i < count; ++i) {
        // TODO: Create buffer
        // BufferDesc bufDesc{};
        // bufDesc.size = desc.sizeBytes;
        // bufDesc.usage = BufferUsage::UniformBuffer | BufferUsage::TransferDst;
        // if (desc.storageBuffer) bufDesc.usage |= BufferUsage::StorageBuffer;
        // if (desc.hostVisible) {
        //     bufDesc.memoryType = MemoryType::HostVisible | MemoryType::HostCoherent;
        // } else {
        //     bufDesc.memoryType = MemoryType::DeviceLocal;
        // }
        // set.buffers[i] = device->CreateBuffer(bufDesc);
        // if (desc.hostVisible) {
        //     vkMapMemory(device, memory, 0, desc.sizeBytes, 0, &set.mappedPtrs[i]);
        // }
    }

    u32 id = static_cast<u32>(m_sets.size());
    m_sets.push_back(std::move(set));

    NGE_LOG_INFO("Ping-pong buffer '{}' created: {}x {}KB, mode={}",
                 desc.debugName, count, desc.sizeBytes / 1024,
                 count == 2 ? "double" : "triple");
    return id;
}

u32 PingPongBufferManager::CreateTextureSet(const PingPongTextureDesc& desc) {
    std::lock_guard lock(m_mutex);

    BufferSet set;
    set.mode = desc.mode;
    set.sizeBytes = 0;
    set.debugName = desc.debugName;
    set.isTexture = true;
    set.alive = true;

    u32 count = static_cast<u32>(desc.mode);
    set.textures.resize(count);

    for (u32 i = 0; i < count; ++i) {
        // TODO: Create texture
        // TextureDesc texDesc{};
        // texDesc.width = desc.width;
        // texDesc.height = desc.height;
        // texDesc.format = desc.format;
        // texDesc.usage = TextureUsage::Storage | TextureUsage::Sampled;
        // set.textures[i] = device->CreateTexture(texDesc);
    }

    u32 id = static_cast<u32>(m_sets.size());
    m_sets.push_back(std::move(set));

    NGE_LOG_INFO("Ping-pong texture '{}' created: {}x {}x{}, mode={}",
                 desc.debugName, count, desc.width, desc.height,
                 count == 2 ? "double" : "triple");
    return id;
}

void PingPongBufferManager::DestroySet(u32 setId) {
    std::lock_guard lock(m_mutex);
    if (setId >= m_sets.size()) return;
    auto& set = m_sets[setId];
    if (!set.alive) return;

    // TODO: Destroy buffers/textures
    set.alive = false;
}

BufferHandle PingPongBufferManager::GetWriteBuffer(u32 setId) const {
    std::lock_guard lock(m_mutex);
    if (setId >= m_sets.size() || !m_sets[setId].alive || m_sets[setId].isTexture) return {};
    u32 count = static_cast<u32>(m_sets[setId].mode);
    return m_sets[setId].buffers[m_currentIndex % count];
}

BufferHandle PingPongBufferManager::GetReadBuffer(u32 setId) const {
    std::lock_guard lock(m_mutex);
    if (setId >= m_sets.size() || !m_sets[setId].alive || m_sets[setId].isTexture) return {};
    u32 count = static_cast<u32>(m_sets[setId].mode);
    u32 readIndex = (m_currentIndex + count - 1) % count;
    return m_sets[setId].buffers[readIndex];
}

TextureHandle PingPongBufferManager::GetWriteTexture(u32 setId) const {
    std::lock_guard lock(m_mutex);
    if (setId >= m_sets.size() || !m_sets[setId].alive || !m_sets[setId].isTexture) return {};
    u32 count = static_cast<u32>(m_sets[setId].mode);
    return m_sets[setId].textures[m_currentIndex % count];
}

TextureHandle PingPongBufferManager::GetReadTexture(u32 setId) const {
    std::lock_guard lock(m_mutex);
    if (setId >= m_sets.size() || !m_sets[setId].alive || !m_sets[setId].isTexture) return {};
    u32 count = static_cast<u32>(m_sets[setId].mode);
    u32 readIndex = (m_currentIndex + count - 1) % count;
    return m_sets[setId].textures[readIndex];
}

void* PingPongBufferManager::GetWritePtr(u32 setId) const {
    std::lock_guard lock(m_mutex);
    if (setId >= m_sets.size() || !m_sets[setId].alive || m_sets[setId].isTexture) return nullptr;
    u32 count = static_cast<u32>(m_sets[setId].mode);
    return m_sets[setId].mappedPtrs[m_currentIndex % count];
}

void PingPongBufferManager::Advance(u64 frameNumber) {
    std::lock_guard lock(m_mutex);
    m_frameNumber = frameNumber;
    // Find the max buffer count across all sets for proper rotation
    // In practice we just increment — each set uses modulo its own count
    m_currentIndex++;
}

u32 PingPongBufferManager::GetPreviousIndex(u32 setId) const {
    std::lock_guard lock(m_mutex);
    if (setId >= m_sets.size()) return 0;
    u32 count = static_cast<u32>(m_sets[setId].mode);
    return (m_currentIndex + count - 1) % count;
}

PingPongStats PingPongBufferManager::GetStats() const {
    std::lock_guard lock(m_mutex);
    PingPongStats stats{};
    stats.currentFrameIndex = m_currentIndex;

    for (const auto& set : m_sets) {
        if (set.alive) {
            stats.totalPingPongSets++;
            u32 count = static_cast<u32>(set.mode);
            stats.totalBuffers += count;
            stats.totalMemoryBytes += set.sizeBytes * count;
        }
    }
    return stats;
}

} // namespace nge::rhi
