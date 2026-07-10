#include "engine/rhi/common/rhi_frame_resource_tracker.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool FrameResourceTracker::Init(const FrameResourceTrackerConfig& config) {
    m_config = config;
    m_resources.reserve(config.maxTrackedResources);
    m_allocsThisFrame = 0;
    m_deallocsThisFrame = 0;
    m_bytesAllocThisFrame = 0;
    m_bytesFreedThisFrame = 0;
    m_currentBytesAlive = 0;
    m_peakBytesAlive = 0;
    m_peakResourceCount = 0;

    NGE_LOG_INFO("Frame resource tracker initialized: enabled={}, leakCheck={}f, stale={}f, max={}",
                 config.enabled, config.leakCheckInterval, config.staleResourceFrames,
                 config.maxTrackedResources);
    return true;
}

void FrameResourceTracker::Shutdown() {
    if (!m_resources.empty()) {
        NGE_LOG_WARN("Frame resource tracker shutdown with {} tracked resources still alive",
                     m_resources.size());
    }
    m_resources.clear();
    m_history.clear();
}

void FrameResourceTracker::OnResourceCreated(u64 handle, FrameResourceType type, u64 sizeBytes,
                                               const std::string& debugName, bool isTransient) {
    if (!m_config.enabled) return;

    std::lock_guard lock(m_mutex);

    if (m_resources.size() >= m_config.maxTrackedResources) {
        NGE_LOG_WARN("Frame resource tracker: max tracked resources reached ({})", m_config.maxTrackedResources);
        return;
    }

    FrameTrackedResource res;
    res.handle = handle;
    res.type = type;
    res.sizeBytes = sizeBytes;
    res.frameCreated = 0; // Will be set properly on next EndFrame
    res.frameLastUsed = 0;
    res.debugName = debugName;
    res.isTransient = isTransient;

    m_resources[handle] = std::move(res);

    m_allocsThisFrame++;
    m_bytesAllocThisFrame += sizeBytes;
    m_currentBytesAlive += sizeBytes;

    if (m_currentBytesAlive > m_peakBytesAlive) {
        m_peakBytesAlive = m_currentBytesAlive;
    }

    u32 count = static_cast<u32>(m_resources.size());
    if (count > m_peakResourceCount) {
        m_peakResourceCount = count;
    }
}

void FrameResourceTracker::OnResourceDestroyed(u64 handle) {
    if (!m_config.enabled) return;

    std::lock_guard lock(m_mutex);

    auto it = m_resources.find(handle);
    if (it == m_resources.end()) return;

    m_deallocsThisFrame++;
    m_bytesFreedThisFrame += it->second.sizeBytes;
    m_currentBytesAlive -= std::min(m_currentBytesAlive, it->second.sizeBytes);

    m_resources.erase(it);
}

void FrameResourceTracker::OnResourceUsed(u64 handle) {
    if (!m_config.enabled) return;

    std::lock_guard lock(m_mutex);

    auto it = m_resources.find(handle);
    if (it != m_resources.end()) {
        it->second.frameLastUsed = UINT32_MAX; // Will be set on EndFrame
    }
}

void FrameResourceTracker::EndFrame(u32 frameIndex) {
    std::lock_guard lock(m_mutex);

    // Update frame indices for newly created/used resources
    for (auto& [handle, res] : m_resources) {
        if (res.frameCreated == 0) res.frameCreated = frameIndex;
        if (res.frameLastUsed == UINT32_MAX) res.frameLastUsed = frameIndex;
    }

    // Record snapshot
    FrameResourceSnapshot snapshot;
    snapshot.frameIndex = frameIndex;
    snapshot.totalAllocations = m_allocsThisFrame;
    snapshot.totalDeallocations = m_deallocsThisFrame;
    snapshot.totalBytesAllocated = m_bytesAllocThisFrame;
    snapshot.totalBytesFreed = m_bytesFreedThisFrame;
    snapshot.netBytesAlive = m_currentBytesAlive;
    snapshot.peakResourceCount = static_cast<u32>(m_resources.size());

    m_history.push_back(snapshot);

    // Cap history length
    if (m_history.size() > 600) {
        m_history.erase(m_history.begin());
    }

    // Periodic leak check (inlined to avoid recursive mutex lock)
    if (m_config.leakCheckInterval > 0 && frameIndex % m_config.leakCheckInterval == 0) {
        u32 leakCount = 0;
        for (const auto& [handle, res] : m_resources) {
            if (res.isTransient) continue;
            if (frameIndex - res.frameCreated >= m_config.leakCheckInterval &&
                frameIndex - res.frameLastUsed >= m_config.leakCheckInterval) {
                ++leakCount;
                NGE_LOG_WARN("  Leak: '{}' (type={}, {}B, created frame {})",
                             res.debugName, static_cast<int>(res.type),
                             res.sizeBytes, res.frameCreated);
            }
        }
        if (leakCount > 0) {
            NGE_LOG_WARN("Frame {}: {} potential resource leaks detected", frameIndex, leakCount);
        }
    }

    // Reset per-frame counters
    m_allocsThisFrame = 0;
    m_deallocsThisFrame = 0;
    m_bytesAllocThisFrame = 0;
    m_bytesFreedThisFrame = 0;
}

std::vector<FrameTrackedResource> FrameResourceTracker::GetPotentialLeaks(u32 currentFrame, u32 minAge) const {
    std::lock_guard lock(m_mutex);
    std::vector<FrameTrackedResource> leaks;

    for (const auto& [handle, res] : m_resources) {
        if (res.isTransient) continue; // Transients are managed differently
        if (currentFrame - res.frameCreated >= minAge &&
            currentFrame - res.frameLastUsed >= minAge) {
            leaks.push_back(res);
        }
    }

    return leaks;
}

std::vector<FrameTrackedResource> FrameResourceTracker::GetStaleResources(u32 currentFrame) const {
    std::lock_guard lock(m_mutex);
    std::vector<FrameTrackedResource> stale;

    for (const auto& [handle, res] : m_resources) {
        if (res.isTransient) continue;
        if (currentFrame > res.frameLastUsed &&
            currentFrame - res.frameLastUsed > m_config.staleResourceFrames) {
            stale.push_back(res);
        }
    }

    return stale;
}

const FrameTrackedResource* FrameResourceTracker::GetResource(u64 handle) const {
    std::lock_guard lock(m_mutex);
    auto it = m_resources.find(handle);
    if (it != m_resources.end()) return &it->second;
    return nullptr;
}

u32 FrameResourceTracker::GetCountByType(FrameResourceType type) const {
    std::lock_guard lock(m_mutex);
    u32 count = 0;
    for (const auto& [handle, res] : m_resources) {
        if (res.type == type) count++;
    }
    return count;
}

void FrameResourceTracker::Reset() {
    std::lock_guard lock(m_mutex);
    m_resources.clear();
    m_history.clear();
    m_allocsThisFrame = 0;
    m_deallocsThisFrame = 0;
    m_bytesAllocThisFrame = 0;
    m_bytesFreedThisFrame = 0;
    m_currentBytesAlive = 0;
}

FrameResourceTrackerStats FrameResourceTracker::GetStats() const {
    std::lock_guard lock(m_mutex);
    FrameResourceTrackerStats stats{};
    stats.currentResourceCount = static_cast<u32>(m_resources.size());
    stats.currentBytesAlive = m_currentBytesAlive;
    stats.allocationsThisFrame = m_allocsThisFrame;
    stats.deallocationsThisFrame = m_deallocsThisFrame;
    stats.peakBytesAlive = m_peakBytesAlive;
    stats.peakResourceCount = m_peakResourceCount;

    // Count stale (approximate without current frame)
    u32 staleCount = 0;
    u32 leakCount = 0;
    if (!m_history.empty()) {
        u32 currentFrame = m_history.back().frameIndex;
        for (const auto& [handle, res] : m_resources) {
            if (res.isTransient) continue;
            if (currentFrame > res.frameLastUsed &&
                currentFrame - res.frameLastUsed > m_config.staleResourceFrames) {
                staleCount++;
            }
            if (currentFrame > res.frameCreated &&
                currentFrame - res.frameCreated > m_config.leakCheckInterval &&
                currentFrame - res.frameLastUsed > m_config.leakCheckInterval) {
                leakCount++;
            }
        }
    }
    stats.staleResources = staleCount;
    stats.potentialLeaks = leakCount;

    return stats;
}

} // namespace nge::rhi
