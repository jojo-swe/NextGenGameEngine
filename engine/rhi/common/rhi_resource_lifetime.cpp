#include "engine/rhi/common/rhi_resource_lifetime.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool ResourceLifetimeManager::Init(IDevice* device, const Config& config) {
    m_device = device;
    m_config = config;
    m_nextId = 1;

    m_resources.reserve(config.maxDeferredDeletes);
    m_pendingDeletion.reserve(config.maxDeferredDeletes);

    NGE_LOG_INFO("Resource lifetime manager initialized: {} frame latency, max {} deferred",
                 config.framesToKeepAlive, config.maxDeferredDeletes);
    return true;
}

void ResourceLifetimeManager::Shutdown() {
    FlushAll();
    m_resources.clear();
    m_pendingDeletion.clear();
    m_freeSlots.clear();
}

u64 ResourceLifetimeManager::Register(u64 handle, GPUResourceType type, const std::string& debugName) {
    std::lock_guard lock(m_mutex);

    GPUResourceEntry entry;
    entry.handle = handle;
    entry.type = type;
    entry.refCount = 1;
    entry.retireFrame = UINT64_MAX;
    entry.debugName = debugName;

    u64 resourceId;
    if (!m_freeSlots.empty()) {
        resourceId = m_freeSlots.back();
        m_freeSlots.pop_back();
        m_resources[resourceId] = std::move(entry);
    } else {
        resourceId = m_resources.size();
        m_resources.push_back(std::move(entry));
    }

    return resourceId;
}

void ResourceLifetimeManager::AddRef(u64 resourceId) {
    std::lock_guard lock(m_mutex);
    if (resourceId < m_resources.size()) {
        m_resources[resourceId].refCount++;
    }
}

void ResourceLifetimeManager::Release(u64 resourceId) {
    std::lock_guard lock(m_mutex);
    if (resourceId >= m_resources.size()) return;

    auto& entry = m_resources[resourceId];
    if (entry.refCount == 0) return;

    entry.refCount--;
    if (entry.refCount == 0) {
        // Defer deletion — GPU may still be using this resource
        entry.retireFrame = UINT64_MAX; // Will be set by ProcessDeletions caller
        m_pendingDeletion.push_back(resourceId);
    }
}

void ResourceLifetimeManager::SetDestroyCallback(ResourceDestroyFn callback) {
    m_destroyFn = std::move(callback);
}

u32 ResourceLifetimeManager::ProcessDeletions(u64 currentFrame) {
    std::lock_guard lock(m_mutex);
    u32 destroyed = 0;

    for (auto it = m_pendingDeletion.begin(); it != m_pendingDeletion.end(); ) {
        u64 id = *it;
        auto& entry = m_resources[id];

        // Set retire frame on first encounter
        if (entry.retireFrame == UINT64_MAX) {
            entry.retireFrame = currentFrame;
        }

        // Destroy if enough frames have passed
        if (currentFrame >= entry.retireFrame + m_config.framesToKeepAlive) {
            if (m_destroyFn) {
                m_destroyFn(entry.handle, entry.type);
            }

            // Clear entry and add to free list
            entry.handle = 0;
            entry.debugName.clear();
            m_freeSlots.push_back(id);

            it = m_pendingDeletion.erase(it);
            destroyed++;
        } else {
            ++it;
        }
    }

    return destroyed;
}

void ResourceLifetimeManager::FlushAll() {
    std::lock_guard lock(m_mutex);

    for (u64 id : m_pendingDeletion) {
        auto& entry = m_resources[id];
        if (m_destroyFn && entry.handle != 0) {
            m_destroyFn(entry.handle, entry.type);
        }
        entry.handle = 0;
        entry.debugName.clear();
        m_freeSlots.push_back(id);
    }
    m_pendingDeletion.clear();
}

u32 ResourceLifetimeManager::GetRefCount(u64 resourceId) const {
    std::lock_guard lock(m_mutex);
    if (resourceId < m_resources.size()) {
        return m_resources[resourceId].refCount;
    }
    return 0;
}

u32 ResourceLifetimeManager::GetActiveCount() const {
    std::lock_guard lock(m_mutex);
    u32 count = 0;
    for (const auto& entry : m_resources) {
        if (entry.refCount > 0) count++;
    }
    return count;
}

u32 ResourceLifetimeManager::GetPendingDeletionCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_pendingDeletion.size());
}

} // namespace nge::rhi
