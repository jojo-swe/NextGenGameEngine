#include "engine/rhi/common/rhi_bindless_updater.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool BindlessTableUpdater::Init(IDevice* device, const BindlessTableConfig& config) {
    m_device = device;
    m_config = config;

    m_sampledImageFreeList.maxCount = config.maxSampledImages;
    m_storageImageFreeList.maxCount = config.maxStorageImages;
    m_uniformBufferFreeList.maxCount = config.maxUniformBuffers;
    m_storageBufferFreeList.maxCount = config.maxStorageBuffers;
    m_samplerFreeList.maxCount = config.maxSamplers;
    m_accelerationStructureFreeList.maxCount = config.maxAccelerationStructures;

    m_pendingWrites.reserve(1024);

    // TODO: Create the global bindless descriptor set
    // VkDescriptorSetLayoutBinding bindings[] = {
    //   { 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, maxSampledImages, VK_SHADER_STAGE_ALL, nullptr },
    //   { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxStorageImages, VK_SHADER_STAGE_ALL, nullptr },
    //   { 2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxUniformBuffers, VK_SHADER_STAGE_ALL, nullptr },
    //   { 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, maxStorageBuffers, VK_SHADER_STAGE_ALL, nullptr },
    //   { 4, VK_DESCRIPTOR_TYPE_SAMPLER, maxSamplers, VK_SHADER_STAGE_ALL, nullptr },
    // };
    // Use VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
    m_descriptorSet = 1; // Stub

    NGE_LOG_INFO("Bindless table updater initialized: {} images, {} storage, {} buffers, {} samplers",
                 config.maxSampledImages, config.maxStorageImages,
                 config.maxStorageBuffers, config.maxSamplers);
    return true;
}

void BindlessTableUpdater::Shutdown() {
    // TODO: Free descriptor set and layout
    m_pendingWrites.clear();
}

u32 BindlessTableUpdater::AllocateSlot(BindlessResourceType type) {
    std::lock_guard lock(m_mutex);
    auto& fl = GetFreeList(type);

    if (!fl.freeIndices.empty()) {
        u32 index = fl.freeIndices.back();
        fl.freeIndices.pop_back();
        return index;
    }

    if (fl.nextIndex >= fl.maxCount) {
        NGE_LOG_ERROR("Bindless table: out of slots for type {}", static_cast<u32>(type));
        return UINT32_MAX;
    }

    return fl.nextIndex++;
}

void BindlessTableUpdater::FreeSlot(u32 index, BindlessResourceType type) {
    std::lock_guard lock(m_mutex);
    auto& fl = GetFreeList(type);
    fl.freeIndices.push_back(index);
}

void BindlessTableUpdater::Write(const BindlessWrite& write) {
    std::lock_guard lock(m_mutex);
    m_pendingWrites.push_back(write);
}

void BindlessTableUpdater::WriteSampledImage(u32 index, TextureHandle texture, u32 mipLevel, u32 arrayLayer) {
    BindlessWrite w;
    w.index = index;
    w.type = BindlessResourceType::SampledImage;
    w.resourceHandle = texture.IsValid() ? static_cast<u64>(texture.id) : 0;
    w.mipLevel = mipLevel;
    w.arrayLayer = arrayLayer;
    Write(w);
}

void BindlessTableUpdater::WriteStorageImage(u32 index, TextureHandle texture, u32 mipLevel) {
    BindlessWrite w;
    w.index = index;
    w.type = BindlessResourceType::StorageImage;
    w.resourceHandle = texture.IsValid() ? static_cast<u64>(texture.id) : 0;
    w.mipLevel = mipLevel;
    Write(w);
}

void BindlessTableUpdater::WriteStorageBuffer(u32 index, BufferHandle buffer) {
    BindlessWrite w;
    w.index = index;
    w.type = BindlessResourceType::StorageBuffer;
    w.resourceHandle = buffer.IsValid() ? static_cast<u64>(buffer.id) : 0;
    Write(w);
}

void BindlessTableUpdater::WriteUniformBuffer(u32 index, BufferHandle buffer) {
    BindlessWrite w;
    w.index = index;
    w.type = BindlessResourceType::UniformBuffer;
    w.resourceHandle = buffer.IsValid() ? static_cast<u64>(buffer.id) : 0;
    Write(w);
}

void BindlessTableUpdater::WriteSampler(u32 index, SamplerHandle sampler) {
    BindlessWrite w;
    w.index = index;
    w.type = BindlessResourceType::Sampler;
    w.resourceHandle = static_cast<u64>(sampler.id);
    Write(w);
}

u32 BindlessTableUpdater::Flush() {
    std::lock_guard lock(m_mutex);

    if (m_pendingWrites.empty()) return 0;

    u32 writeCount = static_cast<u32>(m_pendingWrites.size());

    // TODO: Batch into vkUpdateDescriptorSets
    // std::vector<VkWriteDescriptorSet> writes;
    // writes.reserve(m_pendingWrites.size());
    //
    // for (const auto& pw : m_pendingWrites) {
    //     VkWriteDescriptorSet write{};
    //     write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    //     write.dstSet = m_descriptorSet;
    //     write.dstBinding = static_cast<u32>(pw.type); // Binding per type
    //     write.dstArrayElement = pw.index;
    //     write.descriptorCount = 1;
    //     write.descriptorType = toVkDescriptorType(pw.type);
    //     // Set pImageInfo or pBufferInfo based on type
    //     writes.push_back(write);
    // }
    // vkUpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);

    m_totalUpdatesThisFrame += writeCount;
    m_pendingWrites.clear();
    return writeCount;
}

void BindlessTableUpdater::BeginFrame() {
    m_totalUpdatesThisFrame = 0;
}

BindlessTableStats BindlessTableUpdater::GetStats() const {
    std::lock_guard lock(m_mutex);
    BindlessTableStats stats{};
    stats.sampledImageCount = m_sampledImageFreeList.nextIndex -
        static_cast<u32>(m_sampledImageFreeList.freeIndices.size());
    stats.storageImageCount = m_storageImageFreeList.nextIndex -
        static_cast<u32>(m_storageImageFreeList.freeIndices.size());
    stats.uniformBufferCount = m_uniformBufferFreeList.nextIndex -
        static_cast<u32>(m_uniformBufferFreeList.freeIndices.size());
    stats.storageBufferCount = m_storageBufferFreeList.nextIndex -
        static_cast<u32>(m_storageBufferFreeList.freeIndices.size());
    stats.samplerCount = m_samplerFreeList.nextIndex -
        static_cast<u32>(m_samplerFreeList.freeIndices.size());
    stats.pendingWrites = static_cast<u32>(m_pendingWrites.size());
    stats.totalUpdatesThisFrame = m_totalUpdatesThisFrame;
    return stats;
}

BindlessTableUpdater::FreeList& BindlessTableUpdater::GetFreeList(BindlessResourceType type) {
    switch (type) {
        case BindlessResourceType::SampledImage:          return m_sampledImageFreeList;
        case BindlessResourceType::StorageImage:          return m_storageImageFreeList;
        case BindlessResourceType::UniformBuffer:         return m_uniformBufferFreeList;
        case BindlessResourceType::StorageBuffer:         return m_storageBufferFreeList;
        case BindlessResourceType::Sampler:               return m_samplerFreeList;
        case BindlessResourceType::AccelerationStructure: return m_accelerationStructureFreeList;
    }
    return m_sampledImageFreeList; // Fallback
}

const BindlessTableUpdater::FreeList& BindlessTableUpdater::GetFreeList(BindlessResourceType type) const {
    switch (type) {
        case BindlessResourceType::SampledImage:          return m_sampledImageFreeList;
        case BindlessResourceType::StorageImage:          return m_storageImageFreeList;
        case BindlessResourceType::UniformBuffer:         return m_uniformBufferFreeList;
        case BindlessResourceType::StorageBuffer:         return m_storageBufferFreeList;
        case BindlessResourceType::Sampler:               return m_samplerFreeList;
        case BindlessResourceType::AccelerationStructure: return m_accelerationStructureFreeList;
    }
    return m_sampledImageFreeList;
}

} // namespace nge::rhi
