#include "engine/rhi/vulkan/vk_descriptor_cache.h"
#include "engine/core/logging/log.h"

namespace nge::rhi::vulkan {

// ─── Descriptor Layout Cache ─────────────────────────────────────────────

bool DescriptorSetCache::Init(void* vkDevice) {
    m_device = vkDevice;
    NGE_LOG_INFO("Descriptor layout cache initialized");
    return true;
}

void DescriptorSetCache::Shutdown() {
    // TODO: vkDestroyDescriptorSetLayout for each cached layout
    // for (auto& [desc, handle] : m_cache) {
    //     vkDestroyDescriptorSetLayout(device, reinterpret_cast<VkDescriptorSetLayout>(handle), nullptr);
    // }
    NGE_LOG_INFO("Descriptor layout cache shutdown ({} layouts)", m_cache.size());
    m_cache.clear();
}

VkLayoutHandle DescriptorSetCache::GetOrCreate(const DescriptorSetLayoutDesc& desc) {
    std::lock_guard lock(m_mutex);

    auto it = m_cache.find(desc);
    if (it != m_cache.end()) {
        return it->second;
    }

    // TODO: Create VkDescriptorSetLayout via Vulkan API
    // VkDescriptorSetLayoutCreateInfo ci{};
    // ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    // std::vector<VkDescriptorSetLayoutBinding> vkBindings(desc.bindings.size());
    // for (usize i = 0; i < desc.bindings.size(); ++i) {
    //     vkBindings[i].binding = desc.bindings[i].binding;
    //     vkBindings[i].descriptorType = ConvertType(desc.bindings[i].type);
    //     vkBindings[i].descriptorCount = desc.bindings[i].count;
    //     vkBindings[i].stageFlags = ConvertStages(desc.bindings[i].stages);
    // }
    // ci.bindingCount = static_cast<u32>(vkBindings.size());
    // ci.pBindings = vkBindings.data();
    //
    // For bindless descriptors, add VkDescriptorSetLayoutBindingFlagsCreateInfo
    //
    // VkDescriptorSetLayout layout;
    // vkCreateDescriptorSetLayout(device, &ci, nullptr, &layout);

    VkLayoutHandle handle = static_cast<u64>(m_cache.size() + 1); // Stub
    m_cache[desc] = handle;

    NGE_LOG_DEBUG("Created descriptor set layout ({} bindings, cache size: {})",
                  desc.bindings.size(), m_cache.size());
    return handle;
}

// ─── Descriptor Allocator ────────────────────────────────────────────────

bool DescriptorAllocator::Init(void* vkDevice, u32 setsPerPool) {
    m_device = vkDevice;
    m_setsPerPool = setsPerPool;

    // Create initial pool
    m_currentPool = CreatePool();

    NGE_LOG_INFO("Descriptor allocator initialized ({} sets/pool)", setsPerPool);
    return true;
}

void DescriptorAllocator::Shutdown() {
    // TODO: vkDestroyDescriptorPool for each pool
    // for (u64 pool : m_pools) {
    //     vkDestroyDescriptorPool(device, reinterpret_cast<VkDescriptorPool>(pool), nullptr);
    // }
    NGE_LOG_INFO("Descriptor allocator shutdown ({} pools)", m_pools.size());
    m_pools.clear();
    m_currentPool = 0;
}

u64 DescriptorAllocator::Allocate(VkLayoutHandle layout) {
    std::lock_guard lock(m_mutex);

    // TODO: Allocate descriptor set from current pool
    // VkDescriptorSetAllocateInfo ai{};
    // ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    // ai.descriptorPool = reinterpret_cast<VkDescriptorPool>(m_currentPool);
    // ai.descriptorSetCount = 1;
    // VkDescriptorSetLayout vkLayout = reinterpret_cast<VkDescriptorSetLayout>(layout);
    // ai.pSetLayouts = &vkLayout;
    //
    // VkDescriptorSet set;
    // VkResult result = vkAllocateDescriptorSets(device, &ai, &set);
    //
    // If allocation fails (pool full), create new pool and retry:
    // if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
    //     m_currentPool = CreatePool();
    //     ai.descriptorPool = reinterpret_cast<VkDescriptorPool>(m_currentPool);
    //     vkAllocateDescriptorSets(device, &ai, &set);
    // }

    (void)layout;
    return 0; // Stub
}

void DescriptorAllocator::ResetPools() {
    std::lock_guard lock(m_mutex);

    // TODO: Reset all pools for reuse
    // for (u64 pool : m_pools) {
    //     vkResetDescriptorPool(device, reinterpret_cast<VkDescriptorPool>(pool), 0);
    // }

    if (!m_pools.empty()) {
        m_currentPool = m_pools[0];
    }
}

u64 DescriptorAllocator::CreatePool() {
    // TODO: Create VkDescriptorPool
    // VkDescriptorPoolSize poolSizes[] = {
    //     { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         m_poolSizes.uniformBuffers },
    //     { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         m_poolSizes.storageBuffers },
    //     { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          m_poolSizes.sampledImages },
    //     { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          m_poolSizes.storageImages },
    //     { VK_DESCRIPTOR_TYPE_SAMPLER,                m_poolSizes.samplers },
    //     { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, m_poolSizes.combinedImageSamplers },
    // };
    // VkDescriptorPoolCreateInfo ci{};
    // ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    // ci.maxSets = m_setsPerPool;
    // ci.poolSizeCount = 6;
    // ci.pPoolSizes = poolSizes;
    // VkDescriptorPool pool;
    // vkCreateDescriptorPool(device, &ci, nullptr, &pool);

    u64 pool = static_cast<u64>(m_pools.size() + 1); // Stub
    m_pools.push_back(pool);

    NGE_LOG_DEBUG("Created descriptor pool (total: {})", m_pools.size());
    return pool;
}

} // namespace nge::rhi::vulkan
