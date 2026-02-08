#include "engine/rhi/common/rhi_transient_pool.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool TransientResourcePool::Init(IDevice* device, const Config& config) {
    m_device = device;
    m_config = config;

    m_heaps.reserve(config.maxHeaps);
    m_resources.reserve(config.maxResources);
    m_heapAllocations.reserve(config.maxHeaps);

    NGE_LOG_INFO("Transient resource pool initialized: {} MB heaps, max {} heaps",
                 config.heapSize / (1024 * 1024), config.maxHeaps);
    return true;
}

void TransientResourcePool::Shutdown() {
    // Destroy all heaps
    // TODO: vkFreeMemory for each heap
    for (auto& heap : m_heaps) {
        heap.handle = 0;
    }
    m_heaps.clear();
    m_heapAllocations.clear();
    m_resources.clear();
}

void TransientResourcePool::BeginFrame() {
    // Reset all allocations — transient resources only live one frame
    for (auto& slots : m_heapAllocations) {
        for (auto& slot : slots) {
            slot.inUse = false;
        }
    }
    m_resources.clear();
    m_totalAllocatedWithoutAliasing = 0;
    m_totalActualMemory = 0;
}

void TransientResourcePool::EndFrame() {
    // Destroy transient texture/buffer views (the memory stays)
    for (auto& res : m_resources) {
        if (res.type == TransientResource::Type::Texture && res.texture.IsValid()) {
            m_device->DestroyTexture(res.texture);
        } else if (res.type == TransientResource::Type::Buffer && res.buffer.IsValid()) {
            m_device->DestroyBuffer(res.buffer);
        }
    }
}

TextureHandle TransientResourcePool::AllocateTexture(const TransientTextureDesc& desc,
                                                        u32 firstPass, u32 lastPass) {
    // Calculate memory requirements
    u64 size = FormatUtils::CalculateTextureSize(desc.format, desc.width, desc.height,
                                                   desc.depth, desc.mipLevels);
    size *= desc.arrayLayers;
    if (desc.sampleCount > 1) size *= desc.sampleCount;

    // Align to 256 bytes (typical Vulkan image alignment)
    u64 alignment = 256;
    size = (size + alignment - 1) & ~(alignment - 1);

    m_totalAllocatedWithoutAliasing += size;

    // Find a heap and offset (with aliasing)
    u32 heapIndex = FindOrCreateHeap(size);
    u64 offset = AllocateFromHeap(heapIndex, size, alignment, firstPass, lastPass);

    // TODO: Create VkImage bound to this heap memory at offset
    // VkImageCreateInfo ci{};
    // ...
    // vkCreateImage(device, &ci, nullptr, &image);
    // vkBindImageMemory(device, image, heap, offset);

    TextureHandle texture{}; // Stub

    TransientResource res;
    res.type = TransientResource::Type::Texture;
    res.texture = texture;
    res.memoryOffset = offset;
    res.memorySize = size;
    res.heapIndex = heapIndex;
    res.firstPassIndex = firstPass;
    res.lastPassIndex = lastPass;
    m_resources.push_back(res);

    return texture;
}

BufferHandle TransientResourcePool::AllocateBuffer(const TransientBufferDesc& desc,
                                                      u32 firstPass, u32 lastPass) {
    u64 size = desc.size;
    u64 alignment = 256;
    size = (size + alignment - 1) & ~(alignment - 1);

    m_totalAllocatedWithoutAliasing += size;

    u32 heapIndex = FindOrCreateHeap(size);
    u64 offset = AllocateFromHeap(heapIndex, size, alignment, firstPass, lastPass);

    // TODO: Create VkBuffer bound to heap memory at offset

    BufferHandle buffer{}; // Stub

    TransientResource res;
    res.type = TransientResource::Type::Buffer;
    res.buffer = buffer;
    res.memoryOffset = offset;
    res.memorySize = size;
    res.heapIndex = heapIndex;
    res.firstPassIndex = firstPass;
    res.lastPassIndex = lastPass;
    m_resources.push_back(res);

    return buffer;
}

bool TransientResourcePool::CanAlias(u32 resourceA, u32 resourceB) const {
    if (resourceA >= m_resources.size() || resourceB >= m_resources.size()) return false;

    const auto& a = m_resources[resourceA];
    const auto& b = m_resources[resourceB];

    // Non-overlapping lifetimes = can alias
    return a.lastPassIndex < b.firstPassIndex || b.lastPassIndex < a.firstPassIndex;
}

void TransientResourcePool::InsertAliasingBarriers(ICommandList* cmd, u32 currentPass) {
    // For each resource starting at currentPass, check if it aliases with
    // a resource that ended before. If so, insert an aliasing barrier.
    for (const auto& res : m_resources) {
        if (res.firstPassIndex == currentPass) {
            // TODO: vkCmdPipelineBarrier with VK_IMAGE_LAYOUT_UNDEFINED
            // This signals that the memory contents are now invalid and
            // the new resource can safely use the same memory region.
            if (res.type == TransientResource::Type::Texture) {
                cmd->TextureBarrier(res.texture, ResourceState::Undefined, ResourceState::ShaderWrite);
            } else {
                cmd->BufferBarrier(res.buffer, ResourceState::Undefined, ResourceState::ShaderWrite);
            }
        }
    }
}

u64 TransientResourcePool::AllocateFromHeap(u32 heapIndex, u64 size, u64 alignment,
                                               u32 firstPass, u32 lastPass) {
    auto& slots = m_heapAllocations[heapIndex];

    // Try to alias with an existing slot that has a non-overlapping lifetime
    for (auto& slot : slots) {
        if (!slot.inUse) continue;
        if (slot.size >= size) {
            // Check lifetime overlap
            if (slot.lastPass < firstPass || lastPass < slot.firstPass) {
                // Can alias! Reuse this slot's memory
                slot.firstPass = std::min(slot.firstPass, firstPass);
                slot.lastPass = std::max(slot.lastPass, lastPass);
                return slot.offset;
            }
        }
    }

    // No aliasing possible — allocate new slot
    u64 offset = 0;
    if (!slots.empty()) {
        const auto& lastSlot = slots.back();
        offset = lastSlot.offset + lastSlot.size;
        offset = (offset + alignment - 1) & ~(alignment - 1);
    }

    AllocationSlot newSlot;
    newSlot.offset = offset;
    newSlot.size = size;
    newSlot.firstPass = firstPass;
    newSlot.lastPass = lastPass;
    newSlot.inUse = true;
    slots.push_back(newSlot);

    m_totalActualMemory += size;
    m_heaps[heapIndex].peakUsage = std::max(m_heaps[heapIndex].peakUsage, offset + size);

    return offset;
}

u32 TransientResourcePool::FindOrCreateHeap(u64 requiredSize) {
    // Try existing heaps with enough capacity
    for (u32 i = 0; i < static_cast<u32>(m_heaps.size()); ++i) {
        if (m_heaps[i].peakUsage + requiredSize <= m_heaps[i].capacity) {
            return i;
        }
    }

    // Create new heap
    if (m_heaps.size() >= m_config.maxHeaps) {
        NGE_LOG_ERROR("Transient pool: max heaps reached ({})", m_config.maxHeaps);
        return 0; // Fallback to first heap
    }

    Heap heap;
    heap.capacity = std::max(m_config.heapSize, requiredSize);
    heap.peakUsage = 0;

    // TODO: vkAllocateMemory
    // VkMemoryAllocateInfo allocInfo{};
    // allocInfo.allocationSize = heap.capacity;
    // allocInfo.memoryTypeIndex = FindMemoryType(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    // vkAllocateMemory(device, &allocInfo, nullptr, &memory);
    heap.handle = static_cast<u64>(m_heaps.size() + 1); // Stub

    u32 heapIndex = static_cast<u32>(m_heaps.size());
    m_heaps.push_back(heap);
    m_heapAllocations.emplace_back();

    NGE_LOG_DEBUG("Created transient heap {} ({} MB)", heapIndex, heap.capacity / (1024 * 1024));
    return heapIndex;
}

u64 TransientResourcePool::GetTotalAllocated() const {
    return m_totalActualMemory;
}

u64 TransientResourcePool::GetTotalAliased() const {
    if (m_totalAllocatedWithoutAliasing <= m_totalActualMemory) return 0;
    return m_totalAllocatedWithoutAliasing - m_totalActualMemory;
}

f32 TransientResourcePool::GetAliasingEfficiency() const {
    if (m_totalAllocatedWithoutAliasing == 0) return 0.0f;
    return 1.0f - static_cast<f32>(m_totalActualMemory) / static_cast<f32>(m_totalAllocatedWithoutAliasing);
}

} // namespace nge::rhi
