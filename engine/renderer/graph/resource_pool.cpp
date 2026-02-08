#include "engine/renderer/graph/resource_pool.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::renderer {

bool RenderGraphResourcePool::Init(rhi::IDevice* device, const ResourcePoolConfig& config) {
    m_device = device;
    m_config = config;
    m_currentFrame = 0;

    m_textures.reserve(config.maxPooledTextures);
    m_buffers.reserve(config.maxPooledBuffers);

    NGE_LOG_INFO("Render graph resource pool initialized: max {} textures, {} buffers, evict after {} frames",
                 config.maxPooledTextures, config.maxPooledBuffers, config.evictionAgeFrames);
    return true;
}

void RenderGraphResourcePool::Shutdown() {
    for (auto& tex : m_textures) {
        if (tex.handle.IsValid()) {
            m_device->DestroyTexture(tex.handle);
        }
    }
    for (auto& buf : m_buffers) {
        if (buf.handle.IsValid()) {
            m_device->DestroyBuffer(buf.handle);
        }
    }
    m_textures.clear();
    m_buffers.clear();
}

rhi::TextureHandle RenderGraphResourcePool::AcquireTexture(u32 width, u32 height, rhi::Format format,
                                                             u32 mipLevels, u32 arrayLayers) {
    std::lock_guard lock(m_mutex);

    // Search for a compatible pooled texture
    for (auto& tex : m_textures) {
        if (!tex.inUse &&
            tex.width == width &&
            tex.height == height &&
            tex.format == format &&
            tex.mipLevels == mipLevels &&
            tex.arrayLayers == arrayLayers) {
            tex.inUse = true;
            tex.lastUsedFrame = m_currentFrame;
            m_textureHits++;
            return tex.handle;
        }
    }

    // No match — create a new one
    m_textureMisses++;

    rhi::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = format;
    desc.mipLevels = mipLevels;
    desc.arrayLayers = arrayLayers;
    desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::ColorAttachment |
                 rhi::TextureUsage::Storage | rhi::TextureUsage::TransferDst | rhi::TextureUsage::TransferSrc;
    desc.debugName = "RGPool_Tex_" + std::to_string(m_textures.size());

    auto handle = m_device->CreateTexture(desc);

    PooledTexture pooled;
    pooled.handle = handle;
    pooled.width = width;
    pooled.height = height;
    pooled.format = format;
    pooled.mipLevels = mipLevels;
    pooled.arrayLayers = arrayLayers;
    pooled.lastUsedFrame = m_currentFrame;
    pooled.inUse = true;
    m_textures.push_back(pooled);

    return handle;
}

rhi::BufferHandle RenderGraphResourcePool::AcquireBuffer(u64 size, rhi::BufferUsage usage) {
    std::lock_guard lock(m_mutex);

    // Search for a compatible pooled buffer (same usage, size >= requested)
    for (auto& buf : m_buffers) {
        if (!buf.inUse && buf.usage == usage && buf.size >= size) {
            buf.inUse = true;
            buf.lastUsedFrame = m_currentFrame;
            m_bufferHits++;
            return buf.handle;
        }
    }

    m_bufferMisses++;

    rhi::BufferDesc desc;
    desc.size = size;
    desc.usage = usage;
    desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
    desc.debugName = "RGPool_Buf_" + std::to_string(m_buffers.size());

    auto handle = m_device->CreateBuffer(desc);

    PooledBuffer pooled;
    pooled.handle = handle;
    pooled.size = size;
    pooled.usage = usage;
    pooled.lastUsedFrame = m_currentFrame;
    pooled.inUse = true;
    m_buffers.push_back(pooled);

    return handle;
}

void RenderGraphResourcePool::ReleaseTexture(rhi::TextureHandle handle) {
    std::lock_guard lock(m_mutex);
    for (auto& tex : m_textures) {
        if (tex.handle == handle) {
            tex.inUse = false;
            return;
        }
    }
}

void RenderGraphResourcePool::ReleaseBuffer(rhi::BufferHandle handle) {
    std::lock_guard lock(m_mutex);
    for (auto& buf : m_buffers) {
        if (buf.handle == handle) {
            buf.inUse = false;
            return;
        }
    }
}

void RenderGraphResourcePool::ReleaseAll() {
    std::lock_guard lock(m_mutex);
    for (auto& tex : m_textures) tex.inUse = false;
    for (auto& buf : m_buffers) buf.inUse = false;
}

u32 RenderGraphResourcePool::Evict(u64 currentFrame) {
    std::lock_guard lock(m_mutex);
    u32 evicted = 0;

    for (auto it = m_textures.begin(); it != m_textures.end(); ) {
        if (!it->inUse && currentFrame - it->lastUsedFrame > m_config.evictionAgeFrames) {
            if (it->handle.IsValid()) {
                m_device->DestroyTexture(it->handle);
            }
            it = m_textures.erase(it);
            evicted++;
        } else {
            ++it;
        }
    }

    for (auto it = m_buffers.begin(); it != m_buffers.end(); ) {
        if (!it->inUse && currentFrame - it->lastUsedFrame > m_config.evictionAgeFrames) {
            if (it->handle.IsValid()) {
                m_device->DestroyBuffer(it->handle);
            }
            it = m_buffers.erase(it);
            evicted++;
        } else {
            ++it;
        }
    }

    return evicted;
}

void RenderGraphResourcePool::BeginFrame(u64 frameNumber) {
    m_currentFrame = frameNumber;

    // Periodic eviction every 60 frames
    if (frameNumber % 60 == 0) {
        u32 evicted = Evict(frameNumber);
        if (evicted > 0) {
            NGE_LOG_DEBUG("Resource pool: evicted {} unused resources", evicted);
        }
    }
}

ResourcePoolStats RenderGraphResourcePool::GetStats() const {
    std::lock_guard lock(m_mutex);
    ResourcePoolStats stats{};
    stats.pooledTextureCount = static_cast<u32>(m_textures.size());
    stats.pooledBufferCount = static_cast<u32>(m_buffers.size());
    stats.textureHits = m_textureHits;
    stats.textureMisses = m_textureMisses;
    stats.bufferHits = m_bufferHits;
    stats.bufferMisses = m_bufferMisses;

    for (const auto& tex : m_textures) {
        if (tex.inUse) stats.activeTextures++;
        // Estimate memory: width * height * 4 bytes * mips
        u64 texMem = static_cast<u64>(tex.width) * tex.height * 4 * tex.arrayLayers;
        stats.totalTextureMemory += texMem;
    }
    for (const auto& buf : m_buffers) {
        if (buf.inUse) stats.activeBuffers++;
        stats.totalBufferMemory += buf.size;
    }

    return stats;
}

} // namespace nge::renderer
