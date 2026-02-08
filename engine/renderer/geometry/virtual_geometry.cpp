#include "engine/renderer/geometry/virtual_geometry.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"
#include <algorithm>
#include <cmath>

namespace nge::renderer {

bool VirtualGeometryManager::Init(rhi::IDevice* device, usize maxGPUMemory) {
    m_device = device;
    m_maxGPUMemory = maxGPUMemory;
    m_usedGPUMemory = 0;
    m_currentFrame = 0;

    // Create global GPU buffers for the geometry pool
    rhi::BufferDesc clusterDesc;
    clusterDesc.size       = sizeof(Cluster) * 1024 * 1024; // 1M clusters max
    clusterDesc.usage      = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst;
    clusterDesc.memoryUsage = rhi::MemoryUsage::GPU_Only;
    clusterDesc.debugName  = "VG_ClusterBuffer";
    m_clusterBuffer = device->CreateBuffer(clusterDesc);

    rhi::BufferDesc vertexDesc;
    vertexDesc.size        = maxGPUMemory / 2; // Half of pool for vertices
    vertexDesc.usage       = rhi::BufferUsage::Storage | rhi::BufferUsage::Vertex | rhi::BufferUsage::TransferDst;
    vertexDesc.memoryUsage = rhi::MemoryUsage::GPU_Only;
    vertexDesc.debugName   = "VG_VertexPool";
    m_vertexBuffer = device->CreateBuffer(vertexDesc);

    rhi::BufferDesc indexDesc;
    indexDesc.size         = maxGPUMemory / 4; // Quarter for indices
    indexDesc.usage        = rhi::BufferUsage::Storage | rhi::BufferUsage::Index | rhi::BufferUsage::TransferDst;
    indexDesc.memoryUsage  = rhi::MemoryUsage::GPU_Only;
    indexDesc.debugName    = "VG_IndexPool";
    m_indexBuffer = device->CreateBuffer(indexDesc);

    NGE_LOG_INFO("Virtual geometry system initialized: {}MB pool", maxGPUMemory / (1024 * 1024));
    return true;
}

void VirtualGeometryManager::Shutdown() {
    if (m_device) {
        m_device->DestroyBuffer(m_clusterBuffer);
        m_device->DestroyBuffer(m_vertexBuffer);
        m_device->DestroyBuffer(m_indexBuffer);
    }
    m_clusters.clear();
    m_pages.clear();
    m_meshes.clear();
}

u32 VirtualGeometryManager::RegisterMesh(const assets::MeshData& meshData) {
    MeshEntry entry;
    entry.rootCluster  = static_cast<u32>(m_clusters.size());
    entry.clusterCount = static_cast<u32>(meshData.meshlets.size());
    entry.pageOffset   = static_cast<u32>(m_pages.size());

    // Convert meshlets to clusters
    for (const auto& meshlet : meshData.meshlets) {
        Cluster cluster;
        cluster.vertexOffset   = meshlet.vertexOffset;
        cluster.indexOffset    = meshlet.triangleOffset;
        cluster.vertexCount    = meshlet.vertexCount;
        cluster.triangleCount  = meshlet.triangleCount;
        cluster.boundCenter    = meshlet.center;
        cluster.boundRadius    = meshlet.radius;
        cluster.coneAxis       = meshlet.coneAxis;
        cluster.coneCutoff     = meshlet.coneCutoff;
        cluster.parentCluster  = UINT32_MAX; // Leaf — no parent yet
        cluster.childOffset    = 0;
        cluster.childCount     = 0;
        cluster.lodError       = 0.0f;       // Leaf = highest detail
        cluster.parentError    = 1e6f;
        cluster.pageIndex      = entry.pageOffset; // All in one page for now
        cluster.isResident     = false;
        m_clusters.push_back(cluster);
    }

    // Create geometry page(s) for this mesh
    GeometryPage page;
    page.pageId       = static_cast<u32>(m_pages.size());
    page.clusterOffset = entry.rootCluster;
    page.clusterCount  = entry.clusterCount;
    page.vertexOffset  = 0; // TODO: allocate from pool
    page.vertexCount   = static_cast<u32>(meshData.vertices.size());
    page.indexOffset    = 0;
    page.indexCount     = static_cast<u32>(meshData.indices.size());
    page.state          = GeometryPage::State::NotLoaded;
    m_pages.push_back(page);

    entry.pageCount = 1;

    u32 meshId = static_cast<u32>(m_meshes.size());
    m_meshes.push_back(entry);

    // TODO: Build LOD hierarchy (cluster grouping + simplification)
    // For now, all clusters are leaf-level (LOD 0)

    NGE_LOG_INFO("Registered mesh {}: {} clusters, {} pages",
                 meshId, entry.clusterCount, entry.pageCount);
    return meshId;
}

f32 VirtualGeometryManager::ComputeScreenError(const Cluster& cluster,
                                                 const math::Vec3& cameraPos,
                                                 f32 screenHeight) const {
    // Screen-space error = (lodError * screenHeight) / (2 * distance * tan(fov/2))
    // Simplified: error_pixels = (object_error * screenHeight) / (2 * distance)
    math::Vec3 delta = cluster.boundCenter - cameraPos;
    f32 distance = math::Max(delta.Length() - cluster.boundRadius, 0.001f);
    return (cluster.lodError * screenHeight) / (2.0f * distance);
}

LODSelectionResult VirtualGeometryManager::SelectLOD(u32 meshId,
                                                       const math::Vec3& cameraPos,
                                                       const math::Mat4& viewProj,
                                                       f32 screenHeight) {
    LODSelectionResult result;
    if (meshId >= m_meshes.size()) return result;

    const auto& mesh = m_meshes[meshId];
    f32 errorThreshold = 1.0f; // 1 pixel error threshold

    TraverseHierarchy(mesh.rootCluster, cameraPos, viewProj, screenHeight,
                      errorThreshold, result);

    return result;
}

void VirtualGeometryManager::TraverseHierarchy(u32 rootCluster,
                                                 const math::Vec3& cameraPos,
                                                 const math::Mat4& /*viewProj*/,
                                                 f32 screenHeight,
                                                 f32 errorThreshold,
                                                 LODSelectionResult& result) {
    // For now, all clusters are leaves — just select all
    // In production: traverse DAG, compare screen error at each level
    const auto& mesh = m_meshes[0]; // TODO: proper mesh lookup

    for (u32 i = rootCluster; i < rootCluster + mesh.clusterCount; ++i) {
        const auto& cluster = m_clusters[i];

        // Check if cluster's page is resident
        if (!cluster.isResident) {
            result.requiredPages.push_back(cluster.pageIndex);
            continue;
        }

        // LOD selection: if this cluster has children, check if we need more detail
        if (cluster.childCount > 0) {
            f32 screenError = ComputeScreenError(cluster, cameraPos, screenHeight);
            if (screenError > errorThreshold) {
                // Need more detail — traverse children
                for (u32 c = 0; c < cluster.childCount; ++c) {
                    TraverseHierarchy(cluster.childOffset + c, cameraPos, {},
                                     screenHeight, errorThreshold, result);
                }
                continue;
            }
        }

        // Use this cluster
        result.visibleClusters.push_back(i);
        result.totalTriangles += cluster.triangleCount;
    }
}

void VirtualGeometryManager::RequestPages(const std::vector<u32>& pageIds) {
    std::lock_guard<std::mutex> lock(m_streamMutex);
    for (u32 pageId : pageIds) {
        if (pageId >= m_pages.size()) continue;
        auto& page = m_pages[pageId];
        if (page.state == GeometryPage::State::NotLoaded) {
            page.state = GeometryPage::State::Loading;
            m_pendingStreams.push({pageId});
        }
    }
}

void VirtualGeometryManager::ProcessCompletedStreams() {
    std::lock_guard<std::mutex> lock(m_streamMutex);

    // In production: check async file read completions
    // For now: immediately mark all pending as resident (data already in CPU memory)
    while (!m_pendingStreams.empty()) {
        auto req = m_pendingStreams.front();
        m_pendingStreams.pop();

        auto& page = m_pages[req.pageId];
        page.state = GeometryPage::State::Resident;
        page.lastUsedFrame = m_currentFrame;

        // Mark clusters as resident
        for (u32 i = page.clusterOffset; i < page.clusterOffset + page.clusterCount; ++i) {
            m_clusters[i].isResident = true;
        }

        m_usedGPUMemory += GeometryPage::PAGE_SIZE;
    }

    m_currentFrame++;
}

void VirtualGeometryManager::EvictLRU(usize targetFreeBytes) {
    if (m_usedGPUMemory <= m_maxGPUMemory - targetFreeBytes) return;

    // Sort pages by last used frame (oldest first)
    std::vector<u32> candidates;
    for (u32 i = 0; i < static_cast<u32>(m_pages.size()); ++i) {
        if (m_pages[i].state == GeometryPage::State::Resident) {
            candidates.push_back(i);
        }
    }

    std::sort(candidates.begin(), candidates.end(), [&](u32 a, u32 b) {
        return m_pages[a].lastUsedFrame < m_pages[b].lastUsedFrame;
    });

    for (u32 pageId : candidates) {
        if (m_usedGPUMemory <= m_maxGPUMemory - targetFreeBytes) break;

        auto& page = m_pages[pageId];
        page.state = GeometryPage::State::NotLoaded;

        for (u32 i = page.clusterOffset; i < page.clusterOffset + page.clusterCount; ++i) {
            m_clusters[i].isResident = false;
        }

        m_usedGPUMemory -= GeometryPage::PAGE_SIZE;
    }
}

u32 VirtualGeometryManager::GetResidentPageCount() const {
    u32 count = 0;
    for (const auto& page : m_pages) {
        if (page.state == GeometryPage::State::Resident) count++;
    }
    return count;
}

u32 VirtualGeometryManager::GetTotalPageCount() const {
    return static_cast<u32>(m_pages.size());
}

usize VirtualGeometryManager::GetResidentMemory() const {
    return m_usedGPUMemory;
}

} // namespace nge::renderer
