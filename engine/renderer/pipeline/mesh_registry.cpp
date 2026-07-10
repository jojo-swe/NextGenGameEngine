#include "engine/renderer/pipeline/mesh_registry.h"
#include "engine/core/logging/log.h"

namespace nge::renderer {

bool MeshRegistry::Init(rhi::IDevice* device, u32 maxMeshes) {
    m_device = device;
    m_maxMeshes = maxMeshes;
    m_nextId = 0;

    // Global vertex buffer (256 MB default)
    m_vertexBufferCapacity = 256 * 1024 * 1024;
    // Global index buffer (128 MB default)
    m_indexBufferCapacity = 128 * 1024 * 1024;

    if (!device) {
        NGE_LOG_WARN("MeshRegistry::Init: null device — CPU-only mode, no GPU buffers created (tests only)");
    }

    if (device) {
        {
            rhi::BufferDesc desc;
            desc.size = m_vertexBufferCapacity;
            desc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst;
            desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
            desc.debugName = "GlobalVertexBuffer";
            m_globalVertexBuffer = device->CreateBuffer(desc);
        }

        {
            rhi::BufferDesc desc;
            desc.size = m_indexBufferCapacity;
            desc.usage = rhi::BufferUsage::Index | rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst;
            desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
            desc.debugName = "GlobalIndexBuffer";
            m_globalIndexBuffer = device->CreateBuffer(desc);
        }
    }

    m_vertexBufferOffset = 0;
    m_indexBufferOffset = 0;

    NGE_LOG_INFO("Mesh registry initialized: max {} meshes, {} MB vertex, {} MB index",
                 maxMeshes, m_vertexBufferCapacity / (1024 * 1024),
                 m_indexBufferCapacity / (1024 * 1024));
    return true;
}

void MeshRegistry::Shutdown() {
    if (!m_device) return;

    // Destroy per-mesh buffers (if any meshes have their own)
    for (auto& [id, entry] : m_meshes) {
        if (entry.vertexBuffer.IsValid() && entry.vertexBuffer != m_globalVertexBuffer) {
            m_device->DestroyBuffer(entry.vertexBuffer);
        }
        if (entry.indexBuffer.IsValid() && entry.indexBuffer != m_globalIndexBuffer) {
            m_device->DestroyBuffer(entry.indexBuffer);
        }
    }

    if (m_globalVertexBuffer.IsValid()) {
        m_device->DestroyBuffer(m_globalVertexBuffer);
        m_globalVertexBuffer = {};
    }
    if (m_globalIndexBuffer.IsValid()) {
        m_device->DestroyBuffer(m_globalIndexBuffer);
        m_globalIndexBuffer = {};
    }

    m_meshes.clear();
    m_nameToId.clear();
}

MeshId MeshRegistry::Register(const std::string& name, const GPUMeshEntry& entry) {
    if (m_meshes.size() >= m_maxMeshes) {
        NGE_LOG_ERROR("Mesh registry full ({} max)", m_maxMeshes);
        return INVALID_MESH_ID;
    }

    // Check for duplicate name
    auto nameIt = m_nameToId.find(name);
    if (nameIt != m_nameToId.end()) {
        NGE_LOG_WARN("Mesh '{}' already registered (id={}), returning existing", name, nameIt->second);
        return nameIt->second;
    }

    MeshId id = m_nextId++;
    GPUMeshEntry newEntry = entry;
    newEntry.id = id;
    newEntry.name = name;

    // Sub-allocate from global buffers if mesh doesn't have its own
    if (!newEntry.vertexBuffer.IsValid() && newEntry.totalVertices > 0) {
        u32 vertexSize = newEntry.totalVertices * newEntry.vertexStride;
        if (m_vertexBufferOffset + vertexSize <= m_vertexBufferCapacity) {
            newEntry.vertexBuffer = m_globalVertexBuffer;
            // Store offset in LODs
            for (auto& lod : newEntry.lods) {
                lod.vertexOffset += m_vertexBufferOffset / newEntry.vertexStride;
            }
            m_vertexBufferOffset += vertexSize;
            // Align to 16 bytes
            m_vertexBufferOffset = (m_vertexBufferOffset + 15) & ~15u;
        } else {
            NGE_LOG_WARN("Global vertex buffer full, mesh '{}' needs {} KB", name, vertexSize / 1024);
        }
    }

    if (!newEntry.indexBuffer.IsValid() && newEntry.totalIndices > 0) {
        u32 indexSize = newEntry.totalIndices * sizeof(u32);
        if (m_indexBufferOffset + indexSize <= m_indexBufferCapacity) {
            newEntry.indexBuffer = m_globalIndexBuffer;
            for (auto& lod : newEntry.lods) {
                lod.indexOffset += m_indexBufferOffset / sizeof(u32);
            }
            m_indexBufferOffset += indexSize;
            m_indexBufferOffset = (m_indexBufferOffset + 15) & ~15u;
        } else {
            NGE_LOG_WARN("Global index buffer full, mesh '{}' needs {} KB", name, indexSize / 1024);
        }
    }

    m_meshes[id] = std::move(newEntry);
    m_nameToId[name] = id;

    NGE_LOG_DEBUG("Registered mesh '{}' (id={}, {} verts, {} indices, {} LODs)",
                  name, id, entry.totalVertices, entry.totalIndices, entry.lods.size());
    return id;
}

void MeshRegistry::Unregister(MeshId id) {
    auto it = m_meshes.find(id);
    if (it == m_meshes.end()) return;

    m_nameToId.erase(it->second.name);

    // Note: GPU memory from global buffer is NOT reclaimed (linear allocator).
    // For mesh hot-swap, use a more sophisticated allocator.
    if (m_device) {
        if (it->second.vertexBuffer.IsValid() && it->second.vertexBuffer != m_globalVertexBuffer) {
            m_device->DestroyBuffer(it->second.vertexBuffer);
        }
        if (it->second.indexBuffer.IsValid() && it->second.indexBuffer != m_globalIndexBuffer) {
            m_device->DestroyBuffer(it->second.indexBuffer);
        }
    }

    m_meshes.erase(it);
}

const GPUMeshEntry* MeshRegistry::Get(MeshId id) const {
    auto it = m_meshes.find(id);
    return it != m_meshes.end() ? &it->second : nullptr;
}

GPUMeshEntry* MeshRegistry::Get(MeshId id) {
    auto it = m_meshes.find(id);
    return it != m_meshes.end() ? &it->second : nullptr;
}

MeshId MeshRegistry::FindByName(const std::string& name) const {
    auto it = m_nameToId.find(name);
    return it != m_nameToId.end() ? it->second : INVALID_MESH_ID;
}

u32 MeshRegistry::GetTotalVertices() const {
    u32 total = 0;
    for (const auto& [id, entry] : m_meshes) {
        total += entry.totalVertices;
    }
    return total;
}

u32 MeshRegistry::GetTotalIndices() const {
    u32 total = 0;
    for (const auto& [id, entry] : m_meshes) {
        total += entry.totalIndices;
    }
    return total;
}

u64 MeshRegistry::GetTotalGPUMemory() const {
    return static_cast<u64>(m_vertexBufferOffset) + static_cast<u64>(m_indexBufferOffset);
}

} // namespace nge::renderer
