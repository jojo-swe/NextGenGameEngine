#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/core/containers/array.h"
#include <vector>
#include <string>

namespace nge::assets {

// ─── Vertex Layout ───────────────────────────────────────────────────────
struct Vertex {
    math::Vec3 position;
    math::Vec3 normal;
    math::Vec2 texcoord0;
    math::Vec4 tangent;    // xyz = tangent, w = handedness
};

// ─── Meshlet ─────────────────────────────────────────────────────────────
// A small cluster of triangles (max 64 vertices, 124 triangles).
// Used for GPU-driven mesh shader rendering and virtual geometry.
struct Meshlet {
    u32 vertexOffset;       // Into meshlet vertex index buffer
    u32 triangleOffset;     // Into meshlet triangle buffer
    u32 vertexCount;        // Max 64
    u32 triangleCount;      // Max 124

    // Culling data
    math::Vec3 center;      // Bounding sphere center (object space)
    f32        radius;      // Bounding sphere radius

    // Backface cone for meshlet-level backface culling
    math::Vec3 coneAxis;    // Normal cone axis
    f32        coneCutoff;  // cos(half-angle) — if dot(viewDir, axis) > cutoff → all backfacing
};

// ─── SubMesh ─────────────────────────────────────────────────────────────
struct SubMesh {
    u32 vertexOffset  = 0;
    u32 vertexCount   = 0;
    u32 indexOffset    = 0;
    u32 indexCount     = 0;
    u32 meshletOffset = 0;
    u32 meshletCount  = 0;
    u32 materialIndex = 0;

    math::Vec3 aabbMin;
    math::Vec3 aabbMax;
    math::Vec3 boundCenter;
    f32        boundRadius = 0;
};

// ─── Material Data ───────────────────────────────────────────────────────
struct MaterialData {
    std::string name;
    math::Vec4  baseColorFactor = {1, 1, 1, 1};
    f32         metallicFactor  = 0.0f;
    f32         roughnessFactor = 1.0f;
    f32         emissiveStrength = 0.0f;
    f32         alphaCutoff     = 0.5f;

    std::string baseColorTexture;
    std::string normalTexture;
    std::string metallicRoughnessTexture;
    std::string emissiveTexture;
    std::string occlusionTexture;

    bool doubleSided = false;
    bool alphaBlend  = false;
};

// ─── MeshData ────────────────────────────────────────────────────────────
// CPU-side mesh data loaded from disk. Ready for GPU upload.
struct MeshData {
    std::string name;

    // Geometry
    std::vector<Vertex>   vertices;
    std::vector<u32>      indices;

    // Submeshes
    std::vector<SubMesh>  subMeshes;

    // Meshlets (built from submeshes)
    std::vector<Meshlet>  meshlets;
    std::vector<u32>      meshletVertices;   // Vertex indices local to each meshlet
    std::vector<u8>       meshletTriangles;  // Triangle indices (3 bytes per tri, local)

    // Materials
    std::vector<MaterialData> materials;

    // LOD data (for virtual geometry)
    struct LODLevel {
        u32 indexOffset = 0;
        u32 indexCount  = 0;
        f32 screenSizeThreshold = 0; // Switch when object covers fewer pixels
    };
    std::vector<LODLevel> lodLevels;

    // Bounding volume
    math::Vec3 aabbMin;
    math::Vec3 aabbMax;
    math::Vec3 boundCenter;
    f32        boundRadius = 0;

    // Stats
    u32 GetTotalTriangles() const { return static_cast<u32>(indices.size()) / 3; }
    u32 GetTotalVertices() const { return static_cast<u32>(vertices.size()); }
    u32 GetTotalMeshlets() const { return static_cast<u32>(meshlets.size()); }
};

} // namespace nge::assets
