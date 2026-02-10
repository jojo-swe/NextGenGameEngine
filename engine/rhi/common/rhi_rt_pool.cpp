#include "engine/rhi/common/rhi_rt_pool.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool RenderTargetPool::Init(const RTPoolConfig& config) {
    m_config = config;
    m_nextId = 0;
    m_allocsThisFrame = 0;
    m_reusesThisFrame = 0;
    m_totalAllocs = 0;
    m_totalReuses = 0;
    m_totalRecycled = 0;

    NGE_LOG_INFO("Render target pool initialized: maxTargets={}, recycleAfter={} frames, budget={} MB",
                 config.maxTargets, config.recycleAfterFrames, config.vramBudget / (1024 * 1024));
    return true;
}

void RenderTargetPool::Shutdown() {
    m_targets.clear();
}

u32 RenderTargetPool::Acquire(const RenderTargetDesc& desc, u64 gpuHandle) {
    std::lock_guard lock(m_mutex);

    // Try to find a compatible free target
    u32 existing = FindCompatible(desc);
    if (existing != UINT32_MAX) {
        auto& target = m_targets[existing];
        target.inUse = true;
        target.desc.debugName = desc.debugName; // Update debug name
        m_reusesThisFrame++;
        m_totalReuses++;
        return existing;
    }

    // Check limits
    if (m_targets.size() >= m_config.maxTargets) {
        NGE_LOG_WARN("RT pool: max targets reached ({})", m_config.maxTargets);
        return UINT32_MAX;
    }

    // Check budget
    u64 newSize = EstimateSize(desc);
    u64 currentVRAM = GetEstimatedVRAM();
    if (currentVRAM + newSize > m_config.vramBudget) {
        NGE_LOG_WARN("RT pool: VRAM budget exceeded ({} + {} > {})",
                     currentVRAM, newSize, m_config.vramBudget);
        return UINT32_MAX;
    }

    // Allocate new
    u32 id = m_nextId++;

    PooledRenderTarget rt;
    rt.rtId = id;
    rt.desc = desc;
    rt.gpuHandle = gpuHandle;
    rt.lastUsedFrame = 0;
    rt.inUse = true;
    rt.sizeBytes = newSize;

    m_targets[id] = std::move(rt);
    m_allocsThisFrame++;
    m_totalAllocs++;

    return id;
}

void RenderTargetPool::Release(u32 rtId) {
    std::lock_guard lock(m_mutex);

    auto it = m_targets.find(rtId);
    if (it == m_targets.end()) return;

    it->second.inUse = false;
}

void RenderTargetPool::ProcessFrame(u32 currentFrame) {
    std::lock_guard lock(m_mutex);

    // Update last used frame for in-use targets
    for (auto& [id, rt] : m_targets) {
        if (rt.inUse) {
            rt.lastUsedFrame = currentFrame;
        }
    }

    // Recycle unused targets
    if (m_config.enableRecycling) {
        std::vector<u32> toRemove;
        for (const auto& [id, rt] : m_targets) {
            if (!rt.inUse && currentFrame - rt.lastUsedFrame > m_config.recycleAfterFrames) {
                toRemove.push_back(id);
            }
        }

        for (u32 id : toRemove) {
            m_targets.erase(id);
            m_totalRecycled++;
        }
    }

    // Reset per-frame stats
    m_allocsThisFrame = 0;
    m_reusesThisFrame = 0;
}

const PooledRenderTarget* RenderTargetPool::GetTarget(u32 rtId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_targets.find(rtId);
    if (it == m_targets.end()) return nullptr;
    return &it->second;
}

bool RenderTargetPool::HasAvailable(const RenderTargetDesc& desc) const {
    std::lock_guard lock(m_mutex);
    return FindCompatible(desc) != UINT32_MAX;
}

u32 RenderTargetPool::CountAvailable(const RenderTargetDesc& desc) const {
    std::lock_guard lock(m_mutex);

    u32 count = 0;
    for (const auto& [id, rt] : m_targets) {
        if (!rt.inUse && rt.desc.IsCompatible(desc)) {
            count++;
        }
    }
    return count;
}

void RenderTargetPool::ReleaseAll() {
    std::lock_guard lock(m_mutex);

    for (auto& [id, rt] : m_targets) {
        rt.inUse = false;
    }
}

u32 RenderTargetPool::GetTotalCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_targets.size());
}

u32 RenderTargetPool::GetInUseCount() const {
    std::lock_guard lock(m_mutex);

    u32 count = 0;
    for (const auto& [id, rt] : m_targets) {
        if (rt.inUse) count++;
    }
    return count;
}

u32 RenderTargetPool::GetFreeCount() const {
    std::lock_guard lock(m_mutex);

    u32 count = 0;
    for (const auto& [id, rt] : m_targets) {
        if (!rt.inUse) count++;
    }
    return count;
}

u64 RenderTargetPool::GetEstimatedVRAM() const {
    u64 total = 0;
    for (const auto& [id, rt] : m_targets) {
        total += rt.sizeBytes;
    }
    return total;
}

void RenderTargetPool::Reset() {
    std::lock_guard lock(m_mutex);
    m_targets.clear();
    m_nextId = 0;
    m_allocsThisFrame = 0;
    m_reusesThisFrame = 0;
    m_totalAllocs = 0;
    m_totalReuses = 0;
    m_totalRecycled = 0;
}

RTPoolStats RenderTargetPool::GetStats() const {
    std::lock_guard lock(m_mutex);

    RTPoolStats stats{};
    stats.totalTargets = static_cast<u32>(m_targets.size());

    u32 inUse = 0, free = 0;
    u64 totalVRAM = 0;
    for (const auto& [id, rt] : m_targets) {
        if (rt.inUse) inUse++;
        else free++;
        totalVRAM += rt.sizeBytes;
    }

    stats.targetsInUse = inUse;
    stats.targetsFree = free;
    stats.allocationsThisFrame = m_allocsThisFrame;
    stats.reusesThisFrame = m_reusesThisFrame;
    stats.totalAllocations = m_totalAllocs;
    stats.totalReuses = m_totalReuses;
    stats.totalRecycled = m_totalRecycled;
    stats.totalVRAMUsed = totalVRAM;
    stats.vramBudget = m_config.vramBudget;
    stats.budgetUtilization = m_config.vramBudget > 0
        ? static_cast<float>(totalVRAM) / static_cast<float>(m_config.vramBudget)
        : 0.0f;

    return stats;
}

u64 RenderTargetPool::EstimateSize(const RenderTargetDesc& desc) const {
    u32 bpp = 4; // Default bytes per pixel
    switch (desc.format) {
        case RTFormat::RGBA8_Unorm:
        case RTFormat::RGBA8_SRGB:       bpp = 4; break;
        case RTFormat::RGBA16_Float:     bpp = 8; break;
        case RTFormat::RGBA32_Float:     bpp = 16; break;
        case RTFormat::R11G11B10_Float:  bpp = 4; break;
        case RTFormat::RG16_Float:       bpp = 4; break;
        case RTFormat::R16_Float:        bpp = 2; break;
        case RTFormat::R32_Float:        bpp = 4; break;
        case RTFormat::D24_S8:           bpp = 4; break;
        case RTFormat::D32_Float:        bpp = 4; break;
        case RTFormat::D32_S8:           bpp = 8; break;
    }

    u64 size = static_cast<u64>(desc.width) * desc.height * bpp * desc.arrayLayers * desc.sampleCount;

    // Approximate mip chain (sum of 1/4 per level)
    if (desc.mipLevels > 1) {
        u64 mipSize = size;
        for (u32 m = 1; m < desc.mipLevels; ++m) {
            mipSize /= 4;
            size += mipSize;
        }
    }

    return size;
}

u32 RenderTargetPool::FindCompatible(const RenderTargetDesc& desc) const {
    for (const auto& [id, rt] : m_targets) {
        if (!rt.inUse && rt.desc.IsCompatible(desc)) {
            return id;
        }
    }
    return UINT32_MAX;
}

} // namespace nge::rhi
