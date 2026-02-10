#include "engine/rhi/common/rhi_sampler_pool.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

// SamplerDesc::operator== is defaulted in rhi_types.h

size_t SamplerDescHash::operator()(const SamplerDesc& desc) const {
    size_t h = 0;
    auto combine = [&h](size_t val) {
        h ^= val + 0x9e3779b9 + (h << 6) + (h >> 2);
    };
    combine(static_cast<size_t>(desc.minFilter));
    combine(static_cast<size_t>(desc.magFilter));
    combine(static_cast<size_t>(desc.mipFilter));
    combine(static_cast<size_t>(desc.addressU));
    combine(static_cast<size_t>(desc.addressV));
    combine(static_cast<size_t>(desc.addressW));
    combine(std::hash<f32>{}(desc.mipLodBias));
    combine(desc.maxAnisotropy);
    combine(desc.enableCompare ? 1 : 0);
    combine(static_cast<size_t>(desc.compareOp));
    return h;
}

bool SamplerPool::Init(IDevice* device) {
    m_device = device;
    NGE_LOG_INFO("Sampler pool initialized");
    return true;
}

void SamplerPool::Shutdown() {
    // TODO: vkDestroySampler for each cached sampler
    std::lock_guard lock(m_mutex);
    m_cache.clear();
}

SamplerHandle SamplerPool::GetOrCreate(const SamplerDesc& desc) {
    std::lock_guard lock(m_mutex);

    auto it = m_cache.find(desc);
    if (it != m_cache.end()) return it->second;

    // TODO: Create VkSampler
    // VkSamplerCreateInfo ci{};
    // ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    // ci.magFilter = toVk(desc.magFilter);
    // ci.minFilter = toVk(desc.minFilter);
    // ci.mipmapMode = toVk(desc.mipFilter);
    // ci.addressModeU = toVk(desc.addressU);
    // ci.addressModeV = toVk(desc.addressV);
    // ci.addressModeW = toVk(desc.addressW);
    // ci.mipLodBias = desc.mipLodBias;
    // ci.anisotropyEnable = desc.minFilter == FilterMode::Anisotropic;
    // ci.maxAnisotropy = desc.maxAnisotropy;
    // ci.compareEnable = desc.enableCompare;
    // ci.compareOp = toVk(desc.compareOp);
    // ci.minLod = desc.minLod;
    // ci.maxLod = desc.maxLod;
    // vkCreateSampler(device, &ci, nullptr, &sampler);

    SamplerHandle handle;
    handle.id = static_cast<u32>(m_cache.size() + 1);
    m_cache[desc] = handle;
    return handle;
}

SamplerHandle SamplerPool::GetLinearClamp() {
    SamplerDesc desc;
    desc.minFilter = FilterMode::Linear;
    desc.magFilter = FilterMode::Linear;
    desc.mipFilter = FilterMode::Linear;
    desc.addressU = AddressMode::ClampToEdge;
    desc.addressV = AddressMode::ClampToEdge;
    desc.addressW = AddressMode::ClampToEdge;
    return GetOrCreate(desc);
}

SamplerHandle SamplerPool::GetLinearRepeat() {
    SamplerDesc desc;
    desc.minFilter = FilterMode::Linear;
    desc.magFilter = FilterMode::Linear;
    desc.mipFilter = FilterMode::Linear;
    desc.addressU = AddressMode::Repeat;
    desc.addressV = AddressMode::Repeat;
    desc.addressW = AddressMode::Repeat;
    return GetOrCreate(desc);
}

SamplerHandle SamplerPool::GetNearestClamp() {
    SamplerDesc desc;
    desc.minFilter = FilterMode::Nearest;
    desc.magFilter = FilterMode::Nearest;
    desc.mipFilter = FilterMode::Nearest;
    desc.addressU = AddressMode::ClampToEdge;
    desc.addressV = AddressMode::ClampToEdge;
    desc.addressW = AddressMode::ClampToEdge;
    return GetOrCreate(desc);
}

SamplerHandle SamplerPool::GetNearestRepeat() {
    SamplerDesc desc;
    desc.minFilter = FilterMode::Nearest;
    desc.magFilter = FilterMode::Nearest;
    desc.mipFilter = FilterMode::Nearest;
    desc.addressU = AddressMode::Repeat;
    desc.addressV = AddressMode::Repeat;
    desc.addressW = AddressMode::Repeat;
    return GetOrCreate(desc);
}

SamplerHandle SamplerPool::GetAnisotropicRepeat(u32 maxAniso) {
    SamplerDesc desc;
    desc.minFilter = FilterMode::Anisotropic;
    desc.magFilter = FilterMode::Linear;
    desc.mipFilter = FilterMode::Linear;
    desc.addressU = AddressMode::Repeat;
    desc.addressV = AddressMode::Repeat;
    desc.addressW = AddressMode::Repeat;
    desc.maxAnisotropy = maxAniso;
    return GetOrCreate(desc);
}

SamplerHandle SamplerPool::GetShadowSampler() {
    SamplerDesc desc;
    desc.minFilter = FilterMode::Linear;
    desc.magFilter = FilterMode::Linear;
    desc.mipFilter = FilterMode::Nearest;
    desc.addressU = AddressMode::ClampToBorder;
    desc.addressV = AddressMode::ClampToBorder;
    desc.addressW = AddressMode::ClampToBorder;
    desc.enableCompare = true;
    desc.compareOp = CompareOp::LessEqual;
    return GetOrCreate(desc);
}

u32 SamplerPool::GetCachedCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_cache.size());
}

} // namespace nge::rhi
