#include "engine/rhi/common/rhi_render_target_pool.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool RenderTargetPool::Init(IDevice* device, const RenderTargetPoolConfig& config) {
    m_device = device;
    m_config = config;
    m_currentFrame = 0;
    m_hits = 0;
    m_misses = 0;
    m_pool.reserve(config.maxPooled);

    NGE_LOG_INFO("Render target pool initialized: max {} pooled, evict after {} frames",
                 config.maxPooled, config.evictionAgeFrames);
    return true;
}

void RenderTargetPool::Shutdown() {
    for (auto& rt : m_pool) {
        if (rt.handle.IsValid()) {
            m_device->DestroyTexture(rt.handle);
        }
    }
    m_pool.clear();
}

TextureHandle RenderTargetPool::Acquire(const RenderTargetDesc& desc) {
    std::lock_guard lock(m_mutex);

    // Search for compatible pooled RT
    for (auto& rt : m_pool) {
        if (!rt.inUse && IsCompatible(rt, desc)) {
            rt.inUse = true;
            rt.lastUsedFrame = m_currentFrame;
            m_hits++;
            return rt.handle;
        }
    }

    m_misses++;

    // Create new RT
    TextureDesc texDesc;
    texDesc.width = desc.width;
    texDesc.height = desc.height;
    texDesc.format = desc.format;
    texDesc.mipLevels = desc.mipLevels;
    texDesc.arrayLayers = desc.arrayLayers;
    texDesc.sampleCount = desc.sampleCount;
    texDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled |
                    TextureUsage::Storage | TextureUsage::TransferSrc | TextureUsage::TransferDst;
    texDesc.debugName = desc.debugName.empty()
        ? "RTPool_" + std::to_string(m_pool.size())
        : desc.debugName;

    auto handle = m_device->CreateTexture(texDesc);

    PooledRenderTarget pooled;
    pooled.handle = handle;
    pooled.desc = desc;
    pooled.lastUsedFrame = m_currentFrame;
    pooled.inUse = true;
    m_pool.push_back(pooled);

    return handle;
}

void RenderTargetPool::Release(TextureHandle handle) {
    std::lock_guard lock(m_mutex);
    for (auto& rt : m_pool) {
        if (rt.handle == handle) {
            rt.inUse = false;
            return;
        }
    }
}

void RenderTargetPool::ReleaseAll() {
    std::lock_guard lock(m_mutex);
    for (auto& rt : m_pool) {
        rt.inUse = false;
    }
}

void RenderTargetPool::BeginFrame(u64 frameNumber) {
    m_currentFrame = frameNumber;
    if (frameNumber % 60 == 0) {
        u32 evicted = Evict(frameNumber);
        if (evicted > 0) {
            NGE_LOG_DEBUG("RT pool: evicted {} unused render targets", evicted);
        }
    }
}

u32 RenderTargetPool::Evict(u64 currentFrame) {
    std::lock_guard lock(m_mutex);
    u32 evicted = 0;
    for (auto it = m_pool.begin(); it != m_pool.end(); ) {
        if (!it->inUse && currentFrame - it->lastUsedFrame > m_config.evictionAgeFrames) {
            if (it->handle.IsValid()) {
                m_device->DestroyTexture(it->handle);
            }
            it = m_pool.erase(it);
            evicted++;
        } else {
            ++it;
        }
    }
    return evicted;
}

RenderTargetPoolStats RenderTargetPool::GetStats() const {
    std::lock_guard lock(m_mutex);
    RenderTargetPoolStats stats{};
    stats.pooledCount = static_cast<u32>(m_pool.size());
    stats.hits = m_hits;
    stats.misses = m_misses;
    for (const auto& rt : m_pool) {
        if (rt.inUse) stats.activeCount++;
        stats.totalMemoryBytes += EstimateMemory(rt.desc);
    }
    return stats;
}

bool RenderTargetPool::IsCompatible(const PooledRenderTarget& rt, const RenderTargetDesc& desc) const {
    return rt.desc.width == desc.width &&
           rt.desc.height == desc.height &&
           rt.desc.format == desc.format &&
           rt.desc.sampleCount == desc.sampleCount &&
           rt.desc.mipLevels == desc.mipLevels &&
           rt.desc.arrayLayers == desc.arrayLayers;
}

u64 RenderTargetPool::EstimateMemory(const RenderTargetDesc& desc) const {
    u32 bpp = 4; // Default assumption
    switch (desc.format) {
        case Format::RGBA16_FLOAT: bpp = 8; break;
        case Format::RGBA32_FLOAT: bpp = 16; break;
        case Format::RG16_FLOAT: bpp = 4; break;
        case Format::R32_FLOAT: bpp = 4; break;
        case Format::D32_FLOAT: bpp = 4; break;
        default: bpp = 4; break;
    }
    return static_cast<u64>(desc.width) * desc.height * bpp *
           desc.sampleCount * desc.arrayLayers;
}

} // namespace nge::rhi
