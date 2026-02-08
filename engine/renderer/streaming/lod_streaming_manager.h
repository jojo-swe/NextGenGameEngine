#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/renderer/pipeline/mesh_registry.h"
#include <vector>
#include <queue>
#include <unordered_map>
#include <mutex>

namespace nge::renderer {

// ─── Virtual Geometry LOD Streaming Manager ──────────────────────────────
// Manages runtime loading and eviction of meshlet LOD levels based on
// screen-space projected size. Streams geometry data from disk to GPU
// on demand, similar to Nanite's virtual geometry approach.
//
// Pipeline:
//   1. GPU feedback: screen-space error per meshlet cluster
//   2. CPU decision: which LODs to load/evict
//   3. Async I/O: stream meshlet data from disk
//   4. GPU upload: stage meshlet vertices/indices to global buffer

enum class LODStreamPriority : u8 {
    Critical,   // Visible and popping — load ASAP
    High,       // Visible but acceptable current LOD
    Medium,     // Recently visible or near camera
    Low,        // Far away, preloading
};

struct MeshletClusterKey {
    MeshId meshId;
    u32    lodLevel;
    u32    clusterIndex;

    bool operator==(const MeshletClusterKey& other) const {
        return meshId == other.meshId && lodLevel == other.lodLevel &&
               clusterIndex == other.clusterIndex;
    }
};

struct MeshletClusterKeyHash {
    size_t operator()(const MeshletClusterKey& k) const {
        size_t h = std::hash<u32>{}(k.meshId);
        h ^= std::hash<u32>{}(k.lodLevel) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<u32>{}(k.clusterIndex) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct StreamedCluster {
    MeshletClusterKey key;
    u32               gpuVertexOffset;
    u32               gpuIndexOffset;
    u32               vertexCount;
    u32               indexCount;
    u64               lastUsedFrame;
    LODStreamPriority priority;
    bool              resident;     // Currently in GPU memory
    bool              loading;      // I/O in progress
};

struct LODStreamingConfig {
    u64 gpuBudgetBytes = 512 * 1024 * 1024;  // 512 MB for geometry
    u64 maxUploadPerFrame = 8 * 1024 * 1024;  // 8 MB upload budget/frame
    u32 maxPendingLoads = 64;
    u32 evictionLatency = 120;                 // Frames before evicting unused
    f32 lodBias = 0.0f;                        // Global LOD bias (-1 = higher quality)
    f32 screenErrorThreshold = 1.0f;           // Pixels of acceptable error
};

struct LODStreamingStats {
    u32 residentClusters;
    u32 pendingLoads;
    u32 evictedThisFrame;
    u32 loadedThisFrame;
    u64 gpuMemoryUsed;
    u64 gpuBudget;
    f32 occupancy;
    u64 bytesUploadedThisFrame;
};

class LODStreamingManager {
public:
    bool Init(rhi::IDevice* device, const LODStreamingConfig& config = {});
    void Shutdown();

    // Per-frame lifecycle
    void BeginFrame(u32 frameNumber);

    // Process GPU feedback buffer (screen-space error per cluster)
    void ProcessFeedback(const std::vector<f32>& screenErrors,
                           const std::vector<MeshletClusterKey>& clusterKeys);

    // Request a specific LOD cluster to be loaded
    void RequestLoad(const MeshletClusterKey& key, LODStreamPriority priority);

    // Execute pending loads/evictions (call after feedback processing)
    void Update(rhi::ICommandList* cmd);

    // Check if a cluster is resident
    bool IsResident(const MeshletClusterKey& key) const;

    // Get GPU offsets for a resident cluster
    const StreamedCluster* GetCluster(const MeshletClusterKey& key) const;

    // Force eviction
    void Evict(const MeshletClusterKey& key);

    // Stats
    LODStreamingStats GetStats() const;
    const LODStreamingConfig& GetConfig() const { return m_config; }

private:
    struct LoadRequest {
        MeshletClusterKey key;
        LODStreamPriority priority;
        bool operator>(const LoadRequest& other) const {
            return static_cast<u8>(priority) > static_cast<u8>(other.priority);
        }
    };

    void ProcessLoadQueue(rhi::ICommandList* cmd);
    void EvictLRU();
    u64 EstimateClusterSize(const MeshletClusterKey& key) const;

    rhi::IDevice* m_device = nullptr;
    LODStreamingConfig m_config;
    u32 m_frameNumber = 0;

    // Resident clusters
    std::unordered_map<MeshletClusterKey, StreamedCluster, MeshletClusterKeyHash> m_clusters;

    // Priority queue of pending loads
    std::priority_queue<LoadRequest, std::vector<LoadRequest>, std::greater<LoadRequest>> m_loadQueue;

    // GPU memory tracking
    u64 m_gpuMemoryUsed = 0;

    // Per-frame stats
    u32 m_loadedThisFrame = 0;
    u32 m_evictedThisFrame = 0;
    u64 m_bytesUploadedThisFrame = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::renderer
