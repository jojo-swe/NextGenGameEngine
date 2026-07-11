#include "engine/rhi/common/rhi_descriptor_heap.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

// ─── DescriptorHeap ──────────────────────────────────────────────────────

bool DescriptorHeap::Init(IDevice* device, const DescriptorHeapConfig& config) {
    m_device = device;
    m_config = config;
    m_nextFreeSlot = 0;
    m_allocatedCount = 0;

    // TODO: Create VkDescriptorPool or descriptor buffer
    // For VK_EXT_descriptor_buffer:
    //   VkDescriptorSetLayoutCreateInfo → get descriptor size
    //   VkBufferCreateInfo with VK_BUFFER_USAGE_DESCRIPTOR_BUFFER_BIT_EXT
    //   Allocate config.capacity × descriptorSize bytes

    m_descriptorSize = 32; // Typical VkDescriptorBufferInfo size
    m_heapHandle = 1; // Stub

    m_freeList.reserve(config.capacity / 4); // Preallocate some free list space

    NGE_LOG_INFO("Descriptor heap '{}' initialized: {} slots, type={}, shaderVisible={}",
                 config.debugName ? config.debugName : "unnamed",
                 config.capacity, static_cast<u32>(config.type), config.shaderVisible);
    return true;
}

void DescriptorHeap::Shutdown() {
    // TODO: vkDestroyDescriptorPool or free descriptor buffer
    m_freeList.clear();
    m_allocatedCount = 0;
}

DescriptorHandle DescriptorHeap::Allocate() {
    std::lock_guard lock(m_mutex);
    DescriptorHandle handle;

    if (!m_freeList.empty()) {
        // Recycle from free list
        handle.heapIndex = m_freeList.back();
        m_freeList.pop_back();
    } else if (m_nextFreeSlot < m_config.capacity) {
        handle.heapIndex = m_nextFreeSlot++;
    } else {
        NGE_LOG_ERROR("Descriptor heap full ({} capacity)", m_config.capacity);
        return handle; // Invalid
    }

    handle.cpuHandle = static_cast<u64>(handle.heapIndex) * m_descriptorSize;
    handle.gpuHandle = m_config.shaderVisible ? handle.cpuHandle : 0;
    m_allocatedCount++;
    return handle;
}

DescriptorHandle DescriptorHeap::AllocateRange(u32 count) {
    std::lock_guard lock(m_mutex);
    DescriptorHandle handle;

    // Range allocation only from the linear region (can't guarantee contiguous from free list)
    if (m_nextFreeSlot + count <= m_config.capacity) {
        handle.heapIndex = m_nextFreeSlot;
        handle.cpuHandle = static_cast<u64>(handle.heapIndex) * m_descriptorSize;
        handle.gpuHandle = m_config.shaderVisible ? handle.cpuHandle : 0;
        m_nextFreeSlot += count;
        m_allocatedCount += count;
    } else {
        NGE_LOG_ERROR("Descriptor heap: cannot allocate contiguous range of {}", count);
    }

    return handle;
}

void DescriptorHeap::Free(DescriptorHandle handle) {
    if (!handle.IsValid()) return;
    std::lock_guard lock(m_mutex);
    m_freeList.push_back(handle.heapIndex);
    m_allocatedCount--;
}

void DescriptorHeap::FreeRange(DescriptorHandle handle, u32 count) {
    if (!handle.IsValid()) return;
    std::lock_guard lock(m_mutex);
    for (u32 i = 0; i < count; ++i) {
        m_freeList.push_back(handle.heapIndex + i);
    }
    m_allocatedCount -= count;
}

void DescriptorHeap::CopyDescriptor(const DescriptorHandle& src, const DescriptorHandle& dst) {
    // TODO: vkUpdateDescriptorSets or memcpy for descriptor buffers
    (void)src; (void)dst;
}

f32 DescriptorHeap::GetOccupancy() const {
    return m_config.capacity > 0
        ? static_cast<f32>(m_allocatedCount) / static_cast<f32>(m_config.capacity)
        : 0.0f;
}

// ─── DescriptorRingBuffer ────────────────────────────────────────────────

bool DescriptorRingBuffer::Init(IDevice* device, [[maybe_unused]] DescriptorHeapType type,
                                  u32 capacity, u32 framesInFlight) {
    m_device = device;
    m_framesInFlight = framesInFlight;
    m_totalCapacity = capacity;
    m_capacityPerFrame = capacity / framesInFlight;
    m_currentFrame = 0;
    m_currentFrameOffset = 0;
    m_currentFrameCount = 0;

    // TODO: Create descriptor buffer large enough for all frames
    m_descriptorSize = 32;
    m_heapHandle = 1; // Stub

    NGE_LOG_INFO("Descriptor ring buffer initialized: {} total, {} per frame, {} frames",
                 capacity, m_capacityPerFrame, framesInFlight);
    return true;
}

void DescriptorRingBuffer::Shutdown() {
    // TODO: Destroy descriptor buffer
}

void DescriptorRingBuffer::BeginFrame(u32 frameIndex) {
    std::lock_guard lock(m_mutex);
    m_currentFrame = frameIndex % m_framesInFlight;
    m_currentFrameOffset = m_currentFrame * m_capacityPerFrame;
    m_currentFrameCount = 0;
}

DescriptorHandle DescriptorRingBuffer::Allocate() {
    std::lock_guard lock(m_mutex);
    DescriptorHandle handle;

    if (m_currentFrameCount >= m_capacityPerFrame) {
        NGE_LOG_ERROR("Descriptor ring buffer: frame capacity exhausted ({}/{})",
                      m_currentFrameCount, m_capacityPerFrame);
        return handle;
    }

    u32 slot = m_currentFrameOffset + m_currentFrameCount;
    handle.heapIndex = slot;
    handle.cpuHandle = static_cast<u64>(slot) * m_descriptorSize;
    handle.gpuHandle = handle.cpuHandle;
    m_currentFrameCount++;
    return handle;
}

DescriptorHandle DescriptorRingBuffer::AllocateRange(u32 count) {
    std::lock_guard lock(m_mutex);
    DescriptorHandle handle;

    if (m_currentFrameCount + count > m_capacityPerFrame) {
        NGE_LOG_ERROR("Descriptor ring buffer: cannot allocate range of {} (used {}/{})",
                      count, m_currentFrameCount, m_capacityPerFrame);
        return handle;
    }

    u32 slot = m_currentFrameOffset + m_currentFrameCount;
    handle.heapIndex = slot;
    handle.cpuHandle = static_cast<u64>(slot) * m_descriptorSize;
    handle.gpuHandle = handle.cpuHandle;
    m_currentFrameCount += count;
    return handle;
}

} // namespace nge::rhi
