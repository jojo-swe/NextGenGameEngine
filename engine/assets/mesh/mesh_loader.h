#pragma once

#include "engine/assets/mesh/mesh_data.h"
#include <string>

namespace nge::assets {

// ─── Mesh Loader ─────────────────────────────────────────────────────────
// Loads mesh data from glTF 2.0 files using cgltf.
// After loading, optionally builds meshlets and LOD levels.

class MeshLoader {
public:
    // Load a glTF file. Returns true on success.
    static bool LoadGLTF(const std::string& path, MeshData& outMesh);

    // Post-load processing
    static void ComputeBounds(MeshData& mesh);
    static void GenerateTangents(MeshData& mesh);

private:
    static void ProcessNode(const void* cgltfData, const void* node, MeshData& outMesh);
    static void ProcessPrimitive(const void* cgltfData, const void* primitive, MeshData& outMesh, u32 materialIndex);
};

// ─── Meshlet Builder ─────────────────────────────────────────────────────
// Clusters triangles into meshlets for mesh shader rendering.
// Uses meshoptimizer under the hood.

class MeshletBuilder {
public:
    static constexpr u32 MAX_MESHLET_VERTICES  = 64;
    static constexpr u32 MAX_MESHLET_TRIANGLES = 124;

    // Build meshlets from a submesh's index/vertex data.
    // Populates mesh.meshlets, meshletVertices, meshletTriangles.
    static void Build(MeshData& mesh);

    // Compute per-meshlet bounding spheres and backface cones.
    static void ComputeMeshletBounds(MeshData& mesh);
};

} // namespace nge::assets
