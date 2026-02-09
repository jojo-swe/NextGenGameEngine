#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi::vulkan {

// ─── Vulkan Descriptor Set Layout Cache ──────────────────────────────────
// Deduplicates VkDescriptorSetLayout objects via FNV-1a hashing of
// binding descriptions. Identical layouts share a single Vulkan handle,
// eliminating redundant layout creation and reducing driver overhead.
//
// Features:
//   - Hash-based dedup of layout create info
//   - Automatic ref-counting for shared layouts
//   - Bulk destroy on shutdown
//   - Compatible with push descriptors and update templates

enum class DescriptorType : u8 {
    Sampler = 0,
    CombinedImageSampler = 1,
    SampledImage = 2,
    StorageImage = 3,
    UniformTexelBuffer = 4,
    StorageTexelBuffer = 5,
    UniformBuffer = 6,
    StorageBuffer = 7,
    UniformBufferDynamic = 8,
    StorageBufferDynamic = 9,
    InputAttachment = 10,
};

enum class ShaderStageFlag : u32 {
    Vertex   = 0x00000001,
    Fragment = 0x00000010,
    Compute  = 0x00000020,
    Geometry = 0x00000008,
    TessCtrl = 0x00000002,
    TessEval = 0x00000004,
    Mesh     = 0x00000080,
    Task     = 0x00000040,
    All      = 0x7FFFFFFF,
};

inline ShaderStageFlag operator|(ShaderStageFlag a, ShaderStageFlag b) {
    return static_cast<ShaderStageFlag>(static_cast<u32>(a) | static_cast<u32>(b));
}

struct LayoutBinding {
    u32             binding;
    DescriptorType  type;
    u32             count;          // Descriptor array size (1 for non-array)
    ShaderStageFlag stageFlags;
    bool            partiallyBound; // VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
};

struct LayoutCreateInfo {
    std::vector<LayoutBinding> bindings;
    bool pushDescriptor = false;    // VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR
    std::string debugName;
};

// Opaque handle wrapping VkDescriptorSetLayout
using DescriptorSetLayoutHandle = u64;

struct CachedLayout {
    DescriptorSetLayoutHandle handle;
    u64                       hash;
    u32                       refCount;
    u32                       bindingCount;
    bool                      pushDescriptor;
    std::string               debugName;
};

struct DescriptorLayoutCacheConfig {
    u32 initialCapacity = 64;
};

struct DescriptorLayoutCacheStats {
    u32 totalLayouts;
    u32 cacheHits;
    u32 cacheMisses;
    f32 hitRate;
    u32 totalRefCount;
};

class DescriptorLayoutCache {
public:
    bool Init(const DescriptorLayoutCacheConfig& config = {});
    void Shutdown();

    // Get or create a layout. Returns a cached handle if an identical layout exists.
    DescriptorSetLayoutHandle GetOrCreate(const LayoutCreateInfo& info);

    // Release a reference. Layout is destroyed when refCount reaches 0.
    void Release(DescriptorSetLayoutHandle handle);

    // Add a reference (e.g., when sharing a layout across pipelines)
    void AddRef(DescriptorSetLayoutHandle handle);

    // Query
    const CachedLayout* GetLayout(DescriptorSetLayoutHandle handle) const;
    bool HasLayout(u64 hash) const;
    u32 GetLayoutCount() const;

    DescriptorLayoutCacheStats GetStats() const;

private:
    u64 HashLayout(const LayoutCreateInfo& info) const;
    DescriptorSetLayoutHandle CreateLayout(const LayoutCreateInfo& info);
    void DestroyLayout(DescriptorSetLayoutHandle handle);

    DescriptorLayoutCacheConfig m_config;

    // hash → handle
    std::unordered_map<u64, DescriptorSetLayoutHandle> m_hashToHandle;
    // handle → cached layout data
    std::unordered_map<DescriptorSetLayoutHandle, CachedLayout> m_layouts;

    DescriptorSetLayoutHandle m_nextHandle = 1;

    mutable u32 m_cacheHits = 0;
    mutable u32 m_cacheMisses = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi::vulkan
