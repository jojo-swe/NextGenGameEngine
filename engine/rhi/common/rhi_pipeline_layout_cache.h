#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/assets/shader_reflection.h"
#include <vector>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── Pipeline Layout Cache ───────────────────────────────────────────────
// Caches VkPipelineLayout objects built automatically from shader
// reflection data. Avoids redundant layout creation and ensures
// compatible layouts are shared across pipelines.

struct PipelineLayoutKey {
    std::vector<assets::ShaderReflector::DescriptorSetInfo> setLayouts;
    std::vector<assets::ReflectedPushConstant> pushConstants;

    bool operator==(const PipelineLayoutKey& other) const;
};

struct PipelineLayoutKeyHash {
    size_t operator()(const PipelineLayoutKey& key) const;
};

struct CachedPipelineLayout {
    u64 handle = 0;                // VkPipelineLayout
    u32 setCount = 0;
    u32 pushConstantRanges = 0;
    u64 lastUsedFrame = 0;
};

class PipelineLayoutCache {
public:
    bool Init(IDevice* device);
    void Shutdown();

    // Build layout from reflected shader stages
    CachedPipelineLayout GetOrCreate(
        const std::vector<assets::ShaderReflectionData>& stages,
        u64 frameNumber);

    // Build from explicit key
    CachedPipelineLayout GetOrCreate(const PipelineLayoutKey& key, u64 frameNumber);

    // Evict unused layouts
    u32 EvictUnused(u64 currentFrame, u64 maxAge = 600);

    // Stats
    u32 GetCachedCount() const;
    u32 GetHitCount() const { return m_hitCount; }
    u32 GetMissCount() const { return m_missCount; }

private:
    CachedPipelineLayout CreateLayout(const PipelineLayoutKey& key);

    IDevice* m_device = nullptr;
    std::unordered_map<PipelineLayoutKey, CachedPipelineLayout, PipelineLayoutKeyHash> m_cache;
    mutable std::mutex m_mutex;
    u32 m_hitCount = 0;
    u32 m_missCount = 0;
};

} // namespace nge::rhi
