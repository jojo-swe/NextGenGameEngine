#include "engine/rhi/common/rhi_readback.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool ReadbackRingBuffer::Init(IDevice* device, const Config& config) {
    m_device = device;
    m_config = config;
    m_nextSlot = 0;
    m_completedCount = 0;

    m_slots.resize(config.ringSize);

    for (auto& slot : m_slots) {
        BufferDesc desc;
        desc.size = config.maxReadbackSize;
        desc.usage = BufferUsage::TransferDst;
        desc.memoryUsage = MemoryUsage::GPU_To_CPU;
        desc.debugName = "ReadbackSlot";
        slot.stagingBuffer = device->CreateBuffer(desc);
        slot.active = false;
    }

    NGE_LOG_INFO("Readback ring buffer initialized: {} slots, {} KB max each",
                 config.ringSize, config.maxReadbackSize / 1024);
    return true;
}

void ReadbackRingBuffer::Shutdown() {
    if (!m_device) return;
    for (auto& slot : m_slots) {
        if (slot.stagingBuffer.IsValid()) {
            m_device->DestroyBuffer(slot.stagingBuffer);
            slot.stagingBuffer = {};
        }
    }
    m_slots.clear();
}

u32 ReadbackRingBuffer::Submit(ICommandList* cmd, BufferHandle srcBuffer,
                                u32 srcOffset, u32 size, ReadbackCallback callback) {
    std::lock_guard lock(m_mutex);

    if (size > m_config.maxReadbackSize) {
        NGE_LOG_ERROR("Readback size {} exceeds max {}", size, m_config.maxReadbackSize);
        return UINT32_MAX;
    }

    // Find a free slot
    u32 slotIdx = UINT32_MAX;
    for (u32 i = 0; i < static_cast<u32>(m_slots.size()); ++i) {
        u32 idx = (m_nextSlot + i) % static_cast<u32>(m_slots.size());
        if (!m_slots[idx].active) {
            slotIdx = idx;
            break;
        }
    }

    if (slotIdx == UINT32_MAX) {
        NGE_LOG_WARN("Readback ring buffer full, dropping request");
        return UINT32_MAX;
    }

    auto& slot = m_slots[slotIdx];
    slot.size = size;
    slot.submitFrame = m_currentFrame;
    slot.callback = std::move(callback);
    slot.active = true;

    // Issue GPU copy
    cmd->CopyBuffer(srcBuffer, srcOffset, slot.stagingBuffer, 0, size);

    m_nextSlot = (slotIdx + 1) % static_cast<u32>(m_slots.size());
    return slotIdx;
}

u32 ReadbackRingBuffer::SubmitTexture(ICommandList* cmd, TextureHandle srcTexture,
                                        u32 mipLevel, u32 width, u32 height,
                                        u32 bytesPerPixel, ReadbackCallback callback) {
    u32 size = width * height * bytesPerPixel;

    std::lock_guard lock(m_mutex);

    if (size > m_config.maxReadbackSize) {
        NGE_LOG_ERROR("Texture readback size {} exceeds max {}", size, m_config.maxReadbackSize);
        return UINT32_MAX;
    }

    u32 slotIdx = UINT32_MAX;
    for (u32 i = 0; i < static_cast<u32>(m_slots.size()); ++i) {
        u32 idx = (m_nextSlot + i) % static_cast<u32>(m_slots.size());
        if (!m_slots[idx].active) {
            slotIdx = idx;
            break;
        }
    }

    if (slotIdx == UINT32_MAX) {
        NGE_LOG_WARN("Readback ring buffer full, dropping texture request");
        return UINT32_MAX;
    }

    auto& slot = m_slots[slotIdx];
    slot.size = size;
    slot.submitFrame = m_currentFrame;
    slot.callback = std::move(callback);
    slot.active = true;

    cmd->CopyTextureToBuffer(srcTexture, slot.stagingBuffer, mipLevel, 0);

    m_nextSlot = (slotIdx + 1) % static_cast<u32>(m_slots.size());
    return slotIdx;
}

void ReadbackRingBuffer::Poll(u64 currentFrame) {
    std::lock_guard lock(m_mutex);
    m_currentFrame = currentFrame;

    for (auto& slot : m_slots) {
        if (!slot.active) continue;

        // Check if enough frames have passed for the GPU copy to be complete
        if (currentFrame < slot.submitFrame + m_config.framesInFlight) continue;

        // Map and read data
        void* data = m_device->MapBuffer(slot.stagingBuffer);
        if (data && slot.callback) {
            slot.callback(data, slot.size);
        }
        m_device->UnmapBuffer(slot.stagingBuffer);

        slot.active = false;
        slot.callback = nullptr;
        m_completedCount++;
    }
}

u32 ReadbackRingBuffer::GetPendingCount() const {
    u32 count = 0;
    for (const auto& slot : m_slots) {
        if (slot.active) count++;
    }
    return count;
}

} // namespace nge::rhi
