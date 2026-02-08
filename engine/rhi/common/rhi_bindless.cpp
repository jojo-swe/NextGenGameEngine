#include "engine/rhi/common/rhi_bindless.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool BindlessTable::Init(IDevice* device, const BindlessTableConfig& config) {
    m_device = device;
    m_config = config;

    auto initTable = [](TypeTable& table, u32 capacity) {
        table.capacity = capacity;
        table.used.resize(capacity, false);
        table.allocated = 0;
        // Fill free list (reverse order so slot 0 is allocated first)
        for (u32 i = capacity; i > 0; --i) {
            table.freeList.push(i - 1);
        }
    };

    initTable(m_tables[static_cast<u32>(BindlessType::Texture2D)],    config.maxTextures2D);
    initTable(m_tables[static_cast<u32>(BindlessType::TextureCube)],  config.maxTexturesCube);
    initTable(m_tables[static_cast<u32>(BindlessType::Texture3D)],    config.maxTextures3D);
    initTable(m_tables[static_cast<u32>(BindlessType::Buffer)],       config.maxBuffers);
    initTable(m_tables[static_cast<u32>(BindlessType::Sampler)],      config.maxSamplers);
    initTable(m_tables[static_cast<u32>(BindlessType::StorageImage)], config.maxStorageImages);

    // TODO: Create the actual VkDescriptorSet with variable-count bindings
    // One binding per BindlessType, each with the max count.
    // Use VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT and
    // VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT.

    u32 totalSlots = config.maxTextures2D + config.maxTexturesCube + config.maxTextures3D +
                     config.maxBuffers + config.maxSamplers + config.maxStorageImages;

    NGE_LOG_INFO("Bindless table initialized: {} total slots "
                 "(tex2D={}, cube={}, 3D={}, buf={}, samp={}, storage={})",
                 totalSlots, config.maxTextures2D, config.maxTexturesCube,
                 config.maxTextures3D, config.maxBuffers, config.maxSamplers,
                 config.maxStorageImages);
    return true;
}

void BindlessTable::Shutdown() {
    // TODO: Destroy VkDescriptorSet / pool
    for (auto& table : m_tables) {
        table.used.clear();
        while (!table.freeList.empty()) table.freeList.pop();
        table.pendingReleases.clear();
        table.allocated = 0;
        table.capacity = 0;
    }
}

BindlessIndex BindlessTable::AllocateSlot(BindlessType type) {
    auto& table = m_tables[static_cast<u32>(type)];
    if (table.freeList.empty()) {
        NGE_LOG_ERROR("Bindless table full for type {}", static_cast<u32>(type));
        return INVALID_BINDLESS;
    }

    u32 index = table.freeList.front();
    table.freeList.pop();
    table.used[index] = true;
    table.allocated++;
    return index;
}

BindlessIndex BindlessTable::AllocateTexture2D(TextureHandle texture) {
    std::lock_guard lock(m_mutex);
    BindlessIndex idx = AllocateSlot(BindlessType::Texture2D);
    if (idx != INVALID_BINDLESS) {
        WriteDescriptor(BindlessType::Texture2D, idx, texture, BufferHandle{});
    }
    return idx;
}

BindlessIndex BindlessTable::AllocateTextureCube(TextureHandle texture) {
    std::lock_guard lock(m_mutex);
    BindlessIndex idx = AllocateSlot(BindlessType::TextureCube);
    if (idx != INVALID_BINDLESS) {
        WriteDescriptor(BindlessType::TextureCube, idx, texture, BufferHandle{});
    }
    return idx;
}

BindlessIndex BindlessTable::AllocateTexture3D(TextureHandle texture) {
    std::lock_guard lock(m_mutex);
    BindlessIndex idx = AllocateSlot(BindlessType::Texture3D);
    if (idx != INVALID_BINDLESS) {
        WriteDescriptor(BindlessType::Texture3D, idx, texture, BufferHandle{});
    }
    return idx;
}

BindlessIndex BindlessTable::AllocateBuffer(BufferHandle buffer) {
    std::lock_guard lock(m_mutex);
    BindlessIndex idx = AllocateSlot(BindlessType::Buffer);
    if (idx != INVALID_BINDLESS) {
        WriteDescriptor(BindlessType::Buffer, idx, TextureHandle{}, buffer);
    }
    return idx;
}

BindlessIndex BindlessTable::AllocateSampler() {
    std::lock_guard lock(m_mutex);
    return AllocateSlot(BindlessType::Sampler);
    // TODO: Write sampler descriptor
}

BindlessIndex BindlessTable::AllocateStorageImage(TextureHandle texture) {
    std::lock_guard lock(m_mutex);
    BindlessIndex idx = AllocateSlot(BindlessType::StorageImage);
    if (idx != INVALID_BINDLESS) {
        WriteDescriptor(BindlessType::StorageImage, idx, texture, BufferHandle{});
    }
    return idx;
}

void BindlessTable::Release(BindlessType type, BindlessIndex index) {
    std::lock_guard lock(m_mutex);
    auto& table = m_tables[static_cast<u32>(type)];

    if (index >= table.capacity || !table.used[index]) {
        NGE_LOG_WARN("Bindless release: invalid index {} for type {}", index, static_cast<u32>(type));
        return;
    }

    // Defer release until GPU is done using this slot
    TypeTable::DeferredRelease dr;
    dr.index = index;
    dr.releaseFrame = m_currentFrame + m_config.releaseLatency;
    table.pendingReleases.push_back(dr);
}

void BindlessTable::UpdateTexture2D(BindlessIndex index, TextureHandle newTexture) {
    std::lock_guard lock(m_mutex);
    WriteDescriptor(BindlessType::Texture2D, index, newTexture, BufferHandle{});
}

void BindlessTable::UpdateBuffer(BindlessIndex index, BufferHandle newBuffer) {
    std::lock_guard lock(m_mutex);
    WriteDescriptor(BindlessType::Buffer, index, TextureHandle{}, newBuffer);
}

void BindlessTable::BeginFrame(u64 frameIndex) {
    std::lock_guard lock(m_mutex);
    m_currentFrame = frameIndex;

    // Process deferred releases
    for (auto& table : m_tables) {
        auto it = table.pendingReleases.begin();
        while (it != table.pendingReleases.end()) {
            if (frameIndex >= it->releaseFrame) {
                table.used[it->index] = false;
                table.freeList.push(it->index);
                table.allocated--;
                it = table.pendingReleases.erase(it);
            } else {
                ++it;
            }
        }
    }
}

u32 BindlessTable::GetAllocatedCount(BindlessType type) const {
    return m_tables[static_cast<u32>(type)].allocated;
}

u32 BindlessTable::GetCapacity(BindlessType type) const {
    return m_tables[static_cast<u32>(type)].capacity;
}

f32 BindlessTable::GetOccupancy(BindlessType type) const {
    auto& table = m_tables[static_cast<u32>(type)];
    return table.capacity > 0 ? static_cast<f32>(table.allocated) / static_cast<f32>(table.capacity) : 0.0f;
}

void BindlessTable::WriteDescriptor(BindlessType type, BindlessIndex index,
                                      TextureHandle tex, BufferHandle buf) {
    // TODO: Write to the actual VkDescriptorSet via vkUpdateDescriptorSets
    // VkWriteDescriptorSet write{};
    // write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    // write.dstSet = reinterpret_cast<VkDescriptorSet>(m_descriptorSet);
    // write.dstBinding = static_cast<u32>(type);
    // write.dstArrayElement = index;
    // write.descriptorCount = 1;
    //
    // switch (type) {
    //     case BindlessType::Texture2D:
    //     case BindlessType::TextureCube:
    //     case BindlessType::Texture3D:
    //         write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    //         VkDescriptorImageInfo imgInfo{};
    //         imgInfo.imageView = GetImageView(tex);
    //         imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    //         write.pImageInfo = &imgInfo;
    //         break;
    //     case BindlessType::Buffer:
    //         write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    //         VkDescriptorBufferInfo bufInfo{};
    //         bufInfo.buffer = GetVkBuffer(buf);
    //         bufInfo.range = VK_WHOLE_SIZE;
    //         write.pBufferInfo = &bufInfo;
    //         break;
    //     case BindlessType::StorageImage:
    //         write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    //         ...
    //         break;
    // }
    // vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);

    (void)type; (void)index; (void)tex; (void)buf;
}

} // namespace nge::rhi
