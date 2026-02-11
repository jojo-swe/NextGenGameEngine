#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_types.h"
#include <vector>
#include <unordered_map>
#include <mutex>

namespace nge::rhi::vulkan {

// ─── Descriptor Set Layout Cache ─────────────────────────────────────────
// Deduplicates VkDescriptorSetLayout objects by hashing binding descriptions.
// Shared layouts are ref-counted and destroyed only when no longer referenced.

enum class DescriptorBindingType : u8 {
    Sampler, CombinedImageSampler, SampledImage, StorageImage,
    UniformBuffer, StorageBuffer, UniformBufferDynamic, StorageBufferDynamic,
    InputAttachment,
};

struct DescriptorBinding {
    u32                binding;
    DescriptorBindingType type;
    u32                count;
    ShaderStage        stages;
    bool               bindless = false; // Variable descriptor count (Vulkan 1.2+)

    bool operator==(const DescriptorBinding& o) const {
        return binding == o.binding && type == o.type && count == o.count &&
               stages == o.stages && bindless == o.bindless;
    }
};

struct DescriptorSetLayoutDesc {
    std::vector<DescriptorBinding> bindings;

    bool operator==(const DescriptorSetLayoutDesc& o) const {
        if (bindings.size() != o.bindings.size()) return false;
        for (usize i = 0; i < bindings.size(); ++i) {
            if (!(bindings[i] == o.bindings[i])) return false;
        }
        return true;
    }
};

struct DescriptorSetLayoutDescHash {
    usize operator()(const DescriptorSetLayoutDesc& desc) const {
        usize h = 0;
        auto combine = [&](usize val) { h ^= val + 0x9e3779b9 + (h << 6) + (h >> 2); };
        combine(desc.bindings.size());
        for (const auto& b : desc.bindings) {
            combine(static_cast<usize>(b.binding));
            combine(static_cast<usize>(b.type));
            combine(static_cast<usize>(b.count));
            combine(static_cast<usize>(b.stages));
            combine(static_cast<usize>(b.bindless));
        }
        return h;
    }
};

using VkLayoutHandle = u64; // VkDescriptorSetLayout cast

class DescriptorSetCache {
public:
    bool Init(void* vkDevice);
    void Shutdown();

    VkLayoutHandle GetOrCreate(const DescriptorSetLayoutDesc& desc);

    u32 GetCacheSize() const { return static_cast<u32>(m_cache.size()); }

private:
    void* m_device = nullptr;
    std::unordered_map<DescriptorSetLayoutDesc, VkLayoutHandle, DescriptorSetLayoutDescHash> m_cache;
    std::mutex m_mutex;
};

// ─── Descriptor Allocator ────────────────────────────────────────────────
// Pool-based descriptor set allocation with automatic pool growth.

class DescriptorAllocator {
public:
    bool Init(void* vkDevice, u32 setsPerPool = 256);
    void Shutdown();

    // Allocate a descriptor set from pool
    u64 Allocate(VkLayoutHandle layout); // Returns VkDescriptorSet as u64

    // Reset all pools (call at frame boundary)
    void ResetPools();

    u32 GetPoolCount() const { return static_cast<u32>(m_pools.size()); }

private:
    struct PoolSizes {
        u32 uniformBuffers = 64;
        u32 storageBuffers = 64;
        u32 sampledImages = 128;
        u32 storageImages = 32;
        u32 samplers = 32;
        u32 combinedImageSamplers = 128;
    };

    u64 CreatePool(); // Returns VkDescriptorPool as u64

    void* m_device = nullptr;
    u32 m_setsPerPool = 256;
    PoolSizes m_poolSizes;

    std::vector<u64> m_pools;       // VkDescriptorPool handles
    u64 m_currentPool = 0;
    std::mutex m_mutex;
};

} // namespace nge::rhi::vulkan
