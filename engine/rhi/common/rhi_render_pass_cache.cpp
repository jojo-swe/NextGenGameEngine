#include "engine/rhi/common/rhi_render_pass_cache.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool RenderPassKey::operator==(const RenderPassKey& other) const {
    return colorFormats == other.colorFormats &&
           depthFormat == other.depthFormat &&
           sampleCount == other.sampleCount &&
           colorLoadOps == other.colorLoadOps &&
           colorStoreOps == other.colorStoreOps &&
           depthLoadOp == other.depthLoadOp &&
           depthStoreOp == other.depthStoreOp &&
           hasDepth == other.hasDepth;
}

size_t RenderPassKeyHash::operator()(const RenderPassKey& key) const {
    size_t h = 0;
    auto hashCombine = [&h](size_t val) {
        h ^= val + 0x9e3779b9 + (h << 6) + (h >> 2);
    };

    for (auto fmt : key.colorFormats) hashCombine(static_cast<size_t>(fmt));
    hashCombine(static_cast<size_t>(key.depthFormat));
    hashCombine(key.sampleCount);
    for (auto op : key.colorLoadOps) hashCombine(static_cast<size_t>(op));
    for (auto op : key.colorStoreOps) hashCombine(static_cast<size_t>(op));
    hashCombine(static_cast<size_t>(key.depthLoadOp));
    hashCombine(static_cast<size_t>(key.depthStoreOp));
    hashCombine(key.hasDepth ? 1 : 0);
    return h;
}

bool RenderPassCache::Init(IDevice* device) {
    m_device = device;
    m_hitCount = 0;
    m_missCount = 0;
    NGE_LOG_INFO("Render pass cache initialized");
    return true;
}

void RenderPassCache::Shutdown() {
    // TODO: vkDestroyRenderPass for each cached entry
    std::lock_guard lock(m_mutex);
    m_cache.clear();
}

CachedRenderPass RenderPassCache::GetOrCreate(const RenderPassDesc& desc, u64 frameNumber) {
    return GetOrCreate(BuildKey(desc), frameNumber);
}

CachedRenderPass RenderPassCache::GetOrCreate(const RenderPassKey& key, u64 frameNumber) {
    std::lock_guard lock(m_mutex);

    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        it->second.lastUsedFrame = frameNumber;
        m_hitCount++;
        return it->second;
    }

    m_missCount++;
    auto pass = CreateRenderPass(key);
    pass.lastUsedFrame = frameNumber;
    m_cache[key] = pass;
    return pass;
}

u32 RenderPassCache::EvictUnused(u64 currentFrame, u64 maxAge) {
    std::lock_guard lock(m_mutex);
    u32 evicted = 0;

    for (auto it = m_cache.begin(); it != m_cache.end(); ) {
        if (currentFrame - it->second.lastUsedFrame > maxAge) {
            // TODO: vkDestroyRenderPass(device, it->second.handle, nullptr);
            it = m_cache.erase(it);
            evicted++;
        } else {
            ++it;
        }
    }

    if (evicted > 0) {
        NGE_LOG_DEBUG("Render pass cache: evicted {} unused passes", evicted);
    }
    return evicted;
}

u32 RenderPassCache::GetCachedCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_cache.size());
}

f32 RenderPassCache::GetHitRate() const {
    u32 total = m_hitCount + m_missCount;
    return total > 0 ? static_cast<f32>(m_hitCount) / static_cast<f32>(total) : 0.0f;
}

CachedRenderPass RenderPassCache::CreateRenderPass(const RenderPassKey& key) {
    // TODO: Create VkRenderPass
    // VkRenderPassCreateInfo2 rpci{};
    // rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
    //
    // For VK_KHR_dynamic_rendering, no VkRenderPass needed — just cache the config.
    //
    // VkAttachmentDescription2 for each color + depth attachment
    // VkSubpassDescription2 with color/depth/resolve references
    // VkSubpassDependency2 for external dependencies

    CachedRenderPass pass;
    pass.handle = static_cast<u64>(m_cache.size() + 1); // Stub
    pass.colorAttachmentCount = static_cast<u32>(key.colorFormats.size());
    pass.hasDepth = key.hasDepth;
    return pass;
}

RenderPassKey RenderPassCache::BuildKey(const RenderPassDesc& desc) const {
    RenderPassKey key;
    key.sampleCount = desc.sampleCount;
    key.hasDepth = desc.hasDepthStencil;

    for (const auto& att : desc.colorAttachments) {
        key.colorFormats.push_back(att.format);
        key.colorLoadOps.push_back(att.loadOp);
        key.colorStoreOps.push_back(att.storeOp);
    }

    if (desc.hasDepthStencil) {
        key.depthFormat = desc.depthStencil.format;
        key.depthLoadOp = desc.depthStencil.depthLoadOp;
        key.depthStoreOp = desc.depthStencil.depthStoreOp;
    }

    return key;
}

} // namespace nge::rhi
