#include "engine/rhi/vulkan/vk_sampler_cache.h"
#include "engine/core/logging/log.h"
#include <cstring>
#include <functional>

namespace nge::rhi::vulkan {

bool SamplerDesc::operator==(const SamplerDesc& o) const {
    return minFilter == o.minFilter && magFilter == o.magFilter && mipFilter == o.mipFilter &&
           addressU == o.addressU && addressV == o.addressV && addressW == o.addressW &&
           mipLodBias == o.mipLodBias && maxAnisotropy == o.maxAnisotropy &&
           anisotropyEnable == o.anisotropyEnable && compareEnable == o.compareEnable &&
           compareOp == o.compareOp && minLod == o.minLod && maxLod == o.maxLod &&
           borderColor == o.borderColor && unnormalizedCoordinates == o.unnormalizedCoordinates;
}

usize SamplerDescHash::operator()(const SamplerDesc& desc) const {
    usize h = 0;
    auto combine = [&](usize val) { h ^= val + 0x9e3779b9 + (h << 6) + (h >> 2); };
    combine(std::hash<u8>{}(static_cast<u8>(desc.minFilter)));
    combine(std::hash<u8>{}(static_cast<u8>(desc.magFilter)));
    combine(std::hash<u8>{}(static_cast<u8>(desc.mipFilter)));
    combine(std::hash<u8>{}(static_cast<u8>(desc.addressU)));
    combine(std::hash<u8>{}(static_cast<u8>(desc.addressV)));
    combine(std::hash<u8>{}(static_cast<u8>(desc.addressW)));
    combine(std::hash<u32>{}(*reinterpret_cast<const u32*>(&desc.mipLodBias)));
    combine(std::hash<u32>{}(*reinterpret_cast<const u32*>(&desc.maxAnisotropy)));
    combine(std::hash<bool>{}(desc.anisotropyEnable));
    combine(std::hash<bool>{}(desc.compareEnable));
    combine(std::hash<u8>{}(static_cast<u8>(desc.compareOp)));
    combine(std::hash<u32>{}(*reinterpret_cast<const u32*>(&desc.minLod)));
    combine(std::hash<u32>{}(*reinterpret_cast<const u32*>(&desc.maxLod)));
    combine(std::hash<u8>{}(static_cast<u8>(desc.borderColor)));
    combine(std::hash<bool>{}(desc.unnormalizedCoordinates));
    return h;
}

bool SamplerCache::Init(void* vkDevice) {
    m_device = vkDevice;
    NGE_LOG_INFO("Sampler cache initialized");
    return true;
}

void SamplerCache::Shutdown() {
    // TODO: vkDestroySampler for each cached sampler
    // for (auto& [desc, handle] : m_cache) {
    //     vkDestroySampler(static_cast<VkDevice>(m_device), reinterpret_cast<VkSampler>(handle), nullptr);
    // }
    m_cache.clear();
    NGE_LOG_INFO("Sampler cache shutdown ({} samplers destroyed)", m_cache.size());
}

VkSamplerHandle SamplerCache::GetOrCreate(const SamplerDesc& desc) {
    std::lock_guard lock(m_mutex);

    auto it = m_cache.find(desc);
    if (it != m_cache.end()) {
        return it->second;
    }

    // TODO: Create VkSampler via vkCreateSampler
    // VkSamplerCreateInfo ci{};
    // ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    // ci.minFilter = ConvertFilter(desc.minFilter);
    // ci.magFilter = ConvertFilter(desc.magFilter);
    // ... etc
    // VkSampler sampler;
    // vkCreateSampler(device, &ci, nullptr, &sampler);
    // VkSamplerHandle handle = reinterpret_cast<u64>(sampler);

    VkSamplerHandle handle = static_cast<u64>(m_cache.size() + 1); // Stub handle
    m_cache[desc] = handle;

    NGE_LOG_DEBUG("Created sampler (cache size: {})", m_cache.size());
    return handle;
}

VkSamplerHandle SamplerCache::GetLinearClamp() {
    SamplerDesc desc;
    desc.minFilter = Filter::Linear;
    desc.magFilter = Filter::Linear;
    desc.mipFilter = Filter::Linear;
    desc.addressU = AddressMode::ClampToEdge;
    desc.addressV = AddressMode::ClampToEdge;
    desc.addressW = AddressMode::ClampToEdge;
    return GetOrCreate(desc);
}

VkSamplerHandle SamplerCache::GetLinearRepeat() {
    SamplerDesc desc;
    desc.minFilter = Filter::Linear;
    desc.magFilter = Filter::Linear;
    desc.mipFilter = Filter::Linear;
    desc.addressU = AddressMode::Repeat;
    desc.addressV = AddressMode::Repeat;
    desc.addressW = AddressMode::Repeat;
    return GetOrCreate(desc);
}

VkSamplerHandle SamplerCache::GetNearestClamp() {
    SamplerDesc desc;
    desc.minFilter = Filter::Nearest;
    desc.magFilter = Filter::Nearest;
    desc.mipFilter = Filter::Nearest;
    desc.addressU = AddressMode::ClampToEdge;
    desc.addressV = AddressMode::ClampToEdge;
    desc.addressW = AddressMode::ClampToEdge;
    return GetOrCreate(desc);
}

VkSamplerHandle SamplerCache::GetNearestRepeat() {
    SamplerDesc desc;
    desc.minFilter = Filter::Nearest;
    desc.magFilter = Filter::Nearest;
    desc.mipFilter = Filter::Nearest;
    desc.addressU = AddressMode::Repeat;
    desc.addressV = AddressMode::Repeat;
    desc.addressW = AddressMode::Repeat;
    return GetOrCreate(desc);
}

VkSamplerHandle SamplerCache::GetShadowSampler() {
    SamplerDesc desc;
    desc.minFilter = Filter::Linear;
    desc.magFilter = Filter::Linear;
    desc.mipFilter = Filter::Nearest;
    desc.addressU = AddressMode::ClampToBorder;
    desc.addressV = AddressMode::ClampToBorder;
    desc.addressW = AddressMode::ClampToBorder;
    desc.compareEnable = true;
    desc.compareOp = CompareOp::LessOrEqual;
    desc.borderColor = BorderColor::FloatOpaqueWhite;
    return GetOrCreate(desc);
}

VkSamplerHandle SamplerCache::GetAnisotropic(f32 maxAniso) {
    SamplerDesc desc;
    desc.minFilter = Filter::Linear;
    desc.magFilter = Filter::Linear;
    desc.mipFilter = Filter::Linear;
    desc.addressU = AddressMode::Repeat;
    desc.addressV = AddressMode::Repeat;
    desc.addressW = AddressMode::Repeat;
    desc.anisotropyEnable = true;
    desc.maxAnisotropy = maxAniso;
    return GetOrCreate(desc);
}

} // namespace nge::rhi::vulkan
