#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_types.h"
#include <unordered_map>
#include <mutex>

namespace nge::rhi::vulkan {

using nge::rhi::Filter;
using nge::rhi::FilterMode;
using nge::rhi::AddressMode;
using nge::rhi::CompareOp;
using nge::rhi::BorderColor;

// ─── Sampler Cache ───────────────────────────────────────────────────────
// Deduplicates VkSampler objects by hashing sampler descriptions.
// Most engines only need ~20-50 unique sampler configurations.

struct SamplerDesc {
    Filter      minFilter = Filter::Linear;
    Filter      magFilter = Filter::Linear;
    Filter      mipFilter = Filter::Linear;
    AddressMode addressU  = AddressMode::Repeat;
    AddressMode addressV  = AddressMode::Repeat;
    AddressMode addressW  = AddressMode::Repeat;
    f32         mipLodBias = 0.0f;
    f32         maxAnisotropy = 1.0f;
    bool        anisotropyEnable = false;
    bool        compareEnable = false;
    CompareOp   compareOp = CompareOp::Never;
    f32         minLod = 0.0f;
    f32         maxLod = 14.0f; // VK_LOD_CLAMP_NONE equivalent
    BorderColor borderColor = BorderColor::FloatOpaqueBlack;
    bool        unnormalizedCoordinates = false;

    bool operator==(const SamplerDesc& o) const;
};

struct SamplerDescHash {
    usize operator()(const SamplerDesc& desc) const;
};

// Opaque sampler handle
using VkSamplerHandle = u64; // VkSampler cast to u64

class SamplerCache {
public:
    bool Init(void* vkDevice); // VkDevice
    void Shutdown();

    // Get or create a sampler from description
    VkSamplerHandle GetOrCreate(const SamplerDesc& desc);

    // Convenience: common sampler presets
    VkSamplerHandle GetLinearClamp();
    VkSamplerHandle GetLinearRepeat();
    VkSamplerHandle GetNearestClamp();
    VkSamplerHandle GetNearestRepeat();
    VkSamplerHandle GetShadowSampler();
    VkSamplerHandle GetAnisotropic(f32 maxAniso = 16.0f);

    u32 GetCacheSize() const { return static_cast<u32>(m_cache.size()); }

private:
    void* m_device = nullptr; // VkDevice
    std::unordered_map<SamplerDesc, VkSamplerHandle, SamplerDescHash> m_cache;
    std::mutex m_mutex;
};

} // namespace nge::rhi::vulkan
