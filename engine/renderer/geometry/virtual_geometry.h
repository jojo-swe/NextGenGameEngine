#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/assets/mesh/mesh_data.h"
#include <vector>
#include <unordered_map>
#include <queue>
#include <mutex>

namespace nge::renderer {

// ─── Virtual Geometry System (Nanite-like) ───────────────────────────────
// Hierarchical cluster LOD with GPU-driven selection and streaming.
//
// Architecture:
//   Mesh → Clusters (meshlets) → Cluster Groups → LOD Hierarchy
//   Each level is a simplified version of children.
//   GPU traverses hierarchy, selects clusters based on screen-space error.
//   CPU streams required geometry pages from disk asynchronously.

// ─── Cluster (GPU-side meshlet with LOD info) ───────────────────────────

struct Cluster {
    u32  vertexOffset;
    u32  indexOffset;
    u32  vertexCount;
    u32  triangleCount;

    // Bounding
    math::Vec3 boundCenter;
    f32        boundRadius;

    // Backface cone
    math::Vec3 coneAxis;
    f32        coneCutoff;

    // LOD hierarchy
    u32  parentCluster;       // UINT32_MAX if root
    u32  childOffset;         // First child index
    u32  childCount;          // Number of children (0 = leaf)
    f32  lodError;            // Screen-space error at which this cluster should be used
    f32  parentError;         // Parent's error (for transition)

    // Streaming
    u32  pageIndex;           // Which geometry page contains this cluster's data
    bool isResident;          // Data is in GPU memory
};

// ─── Geometry Page ───────────────────────────────────────────────────────
// Fixed-size chunk (~64KB) of geometry data streamed from disk.

struct GeometryPage {
    static constexpr usize PAGE_SIZE = 65536; // 64KB

    u32  pageId;
    u32  clusterOffset;       // First cluster using this page
    u32  clusterCount;
    u32  vertexOffset;        // Offset into global vertex buffer
    u32  vertexCount;
    u32  indexOffset;          // Offset into global index buffer
    u32  indexCount;

    enum class State : u8 {
        NotLoaded,
        Loading,
        Resident,
        PendingEviction,
    };
    State state = State::NotLoaded;

    u64 lastUsedFrame = 0;    // For LRU eviction
};

// ─── LOD Selection Result ────────────────────────────────────────────────

struct LODSelectionResult {
    std::vector<u32> visibleClusters;   // Cluster indices to render
    std::vector<u32> requiredPages;     // Pages needed but not resident
    u32 totalTriangles = 0;
};

// ─── Virtual Geometry Manager ────────────────────────────────────────────

class VirtualGeometryManager {
public:
    VirtualGeometryManager() = default;
    ~VirtualGeometryManager() = default;

    bool Init(rhi::IDevice* device, usize maxGPUMemory = 256 * 1024 * 1024); // 256MB default pool
    void Shutdown();

    // Register a mesh's cluster hierarchy
    u32 RegisterMesh(const assets::MeshData& meshData);

    // CPU-side LOD selection (can also be done on GPU via compute)
    LODSelectionResult SelectLOD(u32 meshId, const math::Vec3& cameraPos,
                                  const math::Mat4& viewProj, f32 screenHeight);

    // Streaming: request pages, process completed loads
    void RequestPages(const std::vector<u32>& pageIds);
    void ProcessCompletedStreams();
    void EvictLRU(usize targetFreeBytes);

    // GPU buffer access
    rhi::BufferHandle GetClusterBuffer() const { return m_clusterBuffer; }
    rhi::BufferHandle GetVertexBuffer() const { return m_vertexBuffer; }
    rhi::BufferHandle GetIndexBuffer() const { return m_indexBuffer; }

    // Stats
    u32 GetResidentPageCount() const;
    u32 GetTotalPageCount() const;
    usize GetResidentMemory() const;

private:
    // Screen-space error for a cluster from a given viewpoint
    f32 ComputeScreenError(const Cluster& cluster, const math::Vec3& cameraPos, f32 screenHeight) const;

    // Traverse cluster hierarchy for LOD selection
    void TraverseHierarchy(u32 rootCluster, const math::Vec3& cameraPos,
                           const math::Mat4& viewProj, f32 screenHeight,
                           f32 errorThreshold, LODSelectionResult& result);

    rhi::IDevice* m_device = nullptr;

    // Cluster data
    std::vector<Cluster>      m_clusters;
    std::vector<GeometryPage> m_pages;

    // GPU buffers
    rhi::BufferHandle m_clusterBuffer;
    rhi::BufferHandle m_vertexBuffer;   // Global vertex pool
    rhi::BufferHandle m_indexBuffer;    // Global index pool

    // Page pool
    usize m_maxGPUMemory   = 0;
    usize m_usedGPUMemory  = 0;
    u64   m_currentFrame   = 0;

    // Streaming queue
    struct StreamRequest {
        u32 pageId;
        // In production: async file read handle
    };
    std::queue<StreamRequest> m_pendingStreams;
    std::mutex                m_streamMutex;

    // Mesh registry
    struct MeshEntry {
        u32 rootCluster;
        u32 clusterCount;
        u32 pageOffset;
        u32 pageCount;
    };
    std::vector<MeshEntry> m_meshes;
};

} // namespace nge::renderer
