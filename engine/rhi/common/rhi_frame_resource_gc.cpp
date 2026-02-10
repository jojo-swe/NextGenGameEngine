#include "engine/rhi/common/rhi_frame_resource_gc.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool FrameResourceGC::Init(const FrameResourceGCConfig& config) {
    m_config = config;
    m_totalQueued = 0;
    m_totalDestroyed = 0;
    m_peakPending = 0;

    m_pendingEntries.reserve(config.maxPendingEntries);

    NGE_LOG_INFO("Frame resource GC initialized: maxPending={}, deferFrames={}, log={}",
                 config.maxPendingEntries, config.framesToDefer, config.logDestructions);
    return true;
}

void FrameResourceGC::Shutdown() {
    FlushAll();
    m_pendingEntries.clear();
}

bool FrameResourceGC::QueueDestroy(u64 handle, GCResourceType type, u32 currentFrame,
                                     std::function<void(u64)> destructor,
                                     const std::string& name) {
    std::lock_guard lock(m_mutex);

    if (m_pendingEntries.size() >= m_config.maxPendingEntries) {
        NGE_LOG_ERROR("Frame resource GC: max pending entries reached ({})", m_config.maxPendingEntries);
        return false;
    }

    GCEntry entry;
    entry.resourceHandle = handle;
    entry.type = type;
    entry.frameQueued = currentFrame;
    entry.debugName = name;
    entry.destructor = std::move(destructor);

    m_pendingEntries.push_back(std::move(entry));
    m_totalQueued++;

    if (m_pendingEntries.size() > m_peakPending) {
        m_peakPending = m_pendingEntries.size();
    }

    return true;
}

u32 FrameResourceGC::Collect(u32 completedFrame) {
    std::lock_guard lock(m_mutex);

    u32 destroyed = 0;

    auto it = m_pendingEntries.begin();
    while (it != m_pendingEntries.end()) {
        // Destroy if enough frames have passed
        if (completedFrame >= it->frameQueued + m_config.framesToDefer) {
            if (m_config.logDestructions) {
                NGE_LOG_DEBUG("GC destroying: handle={}, type={}, name='{}', queued frame={}",
                              it->resourceHandle, static_cast<u8>(it->type), it->debugName, it->frameQueued);
            }

            if (it->destructor) {
                it->destructor(it->resourceHandle);
            }

            destroyed++;
            m_totalDestroyed++;
            it = m_pendingEntries.erase(it);
        } else {
            ++it;
        }
    }

    return destroyed;
}

u32 FrameResourceGC::FlushAll() {
    std::lock_guard lock(m_mutex);

    u32 destroyed = 0;

    for (auto& entry : m_pendingEntries) {
        if (entry.destructor) {
            entry.destructor(entry.resourceHandle);
        }
        destroyed++;
        m_totalDestroyed++;
    }

    m_pendingEntries.clear();
    return destroyed;
}

u32 FrameResourceGC::GetPendingCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_pendingEntries.size());
}

u32 FrameResourceGC::GetPendingCountByType(GCResourceType type) const {
    std::lock_guard lock(m_mutex);

    u32 count = 0;
    for (const auto& entry : m_pendingEntries) {
        if (entry.type == type) count++;
    }
    return count;
}

bool FrameResourceGC::IsPending(u64 handle) const {
    std::lock_guard lock(m_mutex);

    for (const auto& entry : m_pendingEntries) {
        if (entry.resourceHandle == handle) return true;
    }
    return false;
}

void FrameResourceGC::Reset() {
    std::lock_guard lock(m_mutex);
    m_pendingEntries.clear();
    m_totalQueued = 0;
    m_totalDestroyed = 0;
    m_peakPending = 0;
}

FrameResourceGCStats FrameResourceGC::GetStats() const {
    std::lock_guard lock(m_mutex);

    FrameResourceGCStats stats{};
    stats.totalQueued = m_totalQueued;
    stats.totalDestroyed = m_totalDestroyed;
    stats.pendingEntries = static_cast<u32>(m_pendingEntries.size());
    stats.peakPending = m_peakPending;

    for (u32 i = 0; i < 10; ++i) {
        stats.pendingByType[i] = 0;
    }

    for (const auto& entry : m_pendingEntries) {
        u32 typeIdx = static_cast<u32>(entry.type);
        if (typeIdx < 10) {
            stats.pendingByType[typeIdx]++;
        }
    }

    return stats;
}

} // namespace nge::rhi
