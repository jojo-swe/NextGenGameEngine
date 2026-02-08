#include "engine/renderer/streaming/lod_streaming_manager.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::renderer {

bool LODStreamingManager::Init(rhi::IDevice* device, const LODStreamingConfig& config) {
    m_device = device;
    m_config = config;
    m_gpuMemoryUsed = 0;

    NGE_LOG_INFO("LOD streaming manager initialized: {} MB budget, {} MB/frame upload",
                 config.gpuBudgetBytes / (1024 * 1024),
                 config.maxUploadPerFrame / (1024 * 1024));
    return true;
}

void LODStreamingManager::Shutdown() {
    m_clusters.clear();
    while (!m_loadQueue.empty()) m_loadQueue.pop();
}

void LODStreamingManager::BeginFrame(u32 frameNumber) {
    m_frameNumber = frameNumber;
    m_loadedThisFrame = 0;
    m_evictedThisFrame = 0;
    m_bytesUploadedThisFrame = 0;
}

void LODStreamingManager::ProcessFeedback(const std::vector<f32>& screenErrors,
                                             const std::vector<MeshletClusterKey>& clusterKeys) {
    std::lock_guard lock(m_mutex);

    for (u32 i = 0; i < static_cast<u32>(clusterKeys.size()); ++i) {
        const auto& key = clusterKeys[i];
        f32 error = i < screenErrors.size() ? screenErrors[i] : 0.0f;

        auto it = m_clusters.find(key);
        if (it != m_clusters.end()) {
            // Update last used frame for resident clusters
            it->second.lastUsedFrame = m_frameNumber;
        }

        // If screen-space error exceeds threshold, request finer LOD
        if (error > m_config.screenErrorThreshold + m_config.lodBias) {
            // Request next finer LOD level
            MeshletClusterKey finerKey = key;
            if (finerKey.lodLevel > 0) {
                finerKey.lodLevel--;
                LODStreamPriority priority = error > m_config.screenErrorThreshold * 2.0f
                    ? LODStreamPriority::Critical
                    : LODStreamPriority::High;
                RequestLoad(finerKey, priority);
            }
        }
    }
}

void LODStreamingManager::RequestLoad(const MeshletClusterKey& key, LODStreamPriority priority) {
    // Skip if already resident or loading
    auto it = m_clusters.find(key);
    if (it != m_clusters.end() && (it->second.resident || it->second.loading)) {
        return;
    }

    LoadRequest req;
    req.key = key;
    req.priority = priority;
    m_loadQueue.push(req);
}

void LODStreamingManager::Update(rhi::ICommandList* cmd) {
    std::lock_guard lock(m_mutex);

    // Evict unused clusters if over budget
    while (m_gpuMemoryUsed > m_config.gpuBudgetBytes * 9 / 10) {
        EvictLRU();
    }

    // Process load queue within upload budget
    ProcessLoadQueue(cmd);
}

void LODStreamingManager::ProcessLoadQueue(rhi::ICommandList* cmd) {
    u32 loadsThisFrame = 0;

    while (!m_loadQueue.empty() &&
           loadsThisFrame < m_config.maxPendingLoads &&
           m_bytesUploadedThisFrame < m_config.maxUploadPerFrame) {

        auto req = m_loadQueue.top();
        m_loadQueue.pop();

        // Skip if already resident
        auto it = m_clusters.find(req.key);
        if (it != m_clusters.end() && it->second.resident) continue;

        // Ensure budget
        u64 clusterSize = EstimateClusterSize(req.key);
        if (m_gpuMemoryUsed + clusterSize > m_config.gpuBudgetBytes) {
            EvictLRU();
            if (m_gpuMemoryUsed + clusterSize > m_config.gpuBudgetBytes) {
                continue; // Still no room, skip
            }
        }

        // TODO: Async I/O read from disk
        // auto data = AsyncReadMeshletData(req.key);

        // TODO: Upload to GPU via staging manager
        // u32 vertexOffset = AllocateFromGlobalVertexBuffer(data.vertexSize);
        // u32 indexOffset = AllocateFromGlobalIndexBuffer(data.indexSize);
        // cmd->CopyBuffer(staging, vertexOffset, globalVertexBuf, vertexOffset, data.vertexSize);

        // Register as resident
        StreamedCluster cluster;
        cluster.key = req.key;
        cluster.gpuVertexOffset = 0;  // Would be set by allocator
        cluster.gpuIndexOffset = 0;
        cluster.vertexCount = 0;
        cluster.indexCount = 0;
        cluster.lastUsedFrame = m_frameNumber;
        cluster.priority = req.priority;
        cluster.resident = true;
        cluster.loading = false;

        m_clusters[req.key] = cluster;
        m_gpuMemoryUsed += clusterSize;
        m_bytesUploadedThisFrame += clusterSize;
        m_loadedThisFrame++;
        loadsThisFrame++;
    }

    (void)cmd;
}

void LODStreamingManager::EvictLRU() {
    // Find the least recently used cluster
    MeshletClusterKey lruKey{};
    u64 oldestFrame = UINT64_MAX;
    bool found = false;

    for (const auto& [key, cluster] : m_clusters) {
        if (!cluster.resident || cluster.loading) continue;
        if (cluster.lastUsedFrame < oldestFrame) {
            oldestFrame = cluster.lastUsedFrame;
            lruKey = key;
            found = true;
        }
    }

    if (!found) return;

    // Only evict if unused for evictionLatency frames
    if (m_frameNumber - oldestFrame < m_config.evictionLatency) return;

    Evict(lruKey);
}

void LODStreamingManager::Evict(const MeshletClusterKey& key) {
    auto it = m_clusters.find(key);
    if (it == m_clusters.end() || !it->second.resident) return;

    u64 size = EstimateClusterSize(key);
    m_gpuMemoryUsed = m_gpuMemoryUsed > size ? m_gpuMemoryUsed - size : 0;
    m_clusters.erase(it);
    m_evictedThisFrame++;
}

bool LODStreamingManager::IsResident(const MeshletClusterKey& key) const {
    std::lock_guard lock(m_mutex);
    auto it = m_clusters.find(key);
    return it != m_clusters.end() && it->second.resident;
}

const StreamedCluster* LODStreamingManager::GetCluster(const MeshletClusterKey& key) const {
    std::lock_guard lock(m_mutex);
    auto it = m_clusters.find(key);
    if (it != m_clusters.end() && it->second.resident) return &it->second;
    return nullptr;
}

LODStreamingStats LODStreamingManager::GetStats() const {
    std::lock_guard lock(m_mutex);
    LODStreamingStats stats;
    stats.residentClusters = 0;
    stats.pendingLoads = static_cast<u32>(m_loadQueue.size());
    stats.evictedThisFrame = m_evictedThisFrame;
    stats.loadedThisFrame = m_loadedThisFrame;
    stats.gpuMemoryUsed = m_gpuMemoryUsed;
    stats.gpuBudget = m_config.gpuBudgetBytes;
    stats.bytesUploadedThisFrame = m_bytesUploadedThisFrame;

    for (const auto& [key, cluster] : m_clusters) {
        if (cluster.resident) stats.residentClusters++;
    }

    stats.occupancy = m_config.gpuBudgetBytes > 0
        ? static_cast<f32>(m_gpuMemoryUsed) / static_cast<f32>(m_config.gpuBudgetBytes)
        : 0.0f;

    return stats;
}

u64 LODStreamingManager::EstimateClusterSize(const MeshletClusterKey& key) const {
    // Approximate: 64 vertices × 32 bytes + 128 triangles × 3 indices × 4 bytes
    // Finer LODs have more data, coarser have less
    u64 baseSize = 64 * 32 + 128 * 12; // ~3.5 KB per cluster
    u32 lodMultiplier = std::max(1u, 4u >> key.lodLevel); // Finer LODs = larger
    return baseSize * lodMultiplier;
    (void)key;
}

} // namespace nge::renderer
