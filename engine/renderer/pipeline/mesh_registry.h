#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/rhi/common/rhi_device.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace nge::renderer {

// ─── Mesh Registry ───────────────────────────────────────────────────────
// Central storage for GPU mesh data. Meshes are uploaded once and
// referenced by MeshId throughout the frame. Supports meshlet-based
// and traditional indexed rendering.

using MeshId = u32;
inline constexpr MeshId INVALID_MESH_ID = UINT32_MAX;

struct MeshLOD {
    u32 indexOffset;
    u32 indexCount;
    u32 vertexOffset;
    u32 vertexCount;
    u32 meshletOffset;    // Offset into global meshlet buffer
    u32 meshletCount;
    f32 screenSizeThreshold; // Switch to this LOD below this screen fraction
};

struct GPUMeshEntry {
    MeshId       id = INVALID_MESH_ID;
    std::string  name;

    // Bounding volume (object space)
    math::Vec3   aabbMin;
    math::Vec3   aabbMax;
    f32          boundingSphereRadius;

    // LOD chain
    std::vector<MeshLOD> lods;

    // GPU buffer regions
    rhi::BufferHandle vertexBuffer;
    rhi::BufferHandle indexBuffer;
    u32               vertexStride;
    u32               totalVertices;
    u32               totalIndices;

    // Flags
    bool hasSkinning = false;
    bool hasMeshlets = false;
};

class MeshRegistry {
public:
    bool Init(rhi::IDevice* device, u32 maxMeshes = 8192);
    void Shutdown();

    // Register a mesh (uploads to GPU, returns handle)
    MeshId Register(const std::string& name, const GPUMeshEntry& entry);

    // Unregister (deferred destruction via deletion queue)
    void Unregister(MeshId id);

    // Lookup
    const GPUMeshEntry* Get(MeshId id) const;
    GPUMeshEntry* Get(MeshId id);
    MeshId FindByName(const std::string& name) const;

    // Get all mesh entries (for batch operations)
    const std::unordered_map<MeshId, GPUMeshEntry>& GetAll() const { return m_meshes; }

    // Stats
    u32 GetMeshCount() const { return static_cast<u32>(m_meshes.size()); }
    u32 GetTotalVertices() const;
    u32 GetTotalIndices() const;
    u64 GetTotalGPUMemory() const;

    // Global vertex/index buffers (for bindless rendering)
    // All meshes are sub-allocated from these large buffers.
    rhi::BufferHandle GetGlobalVertexBuffer() const { return m_globalVertexBuffer; }
    rhi::BufferHandle GetGlobalIndexBuffer() const { return m_globalIndexBuffer; }

private:
    rhi::IDevice* m_device = nullptr;
    u32 m_maxMeshes = 0;
    MeshId m_nextId = 0;

    std::unordered_map<MeshId, GPUMeshEntry> m_meshes;
    std::unordered_map<std::string, MeshId> m_nameToId;

    // Global GPU buffers
    rhi::BufferHandle m_globalVertexBuffer;
    rhi::BufferHandle m_globalIndexBuffer;
    u32 m_vertexBufferOffset = 0;  // Current allocation offset
    u32 m_indexBufferOffset = 0;
    u32 m_vertexBufferCapacity = 0;
    u32 m_indexBufferCapacity = 0;
};

} // namespace nge::renderer
