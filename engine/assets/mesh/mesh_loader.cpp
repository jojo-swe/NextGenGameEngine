#include "engine/assets/mesh/mesh_loader.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <numeric>

// cgltf — single-header glTF loader
// In production, included via vcpkg. For now, forward-declare the interface.
// The actual #include will be: #include <cgltf.h>
// #define CGLTF_IMPLEMENTATION is in a separate .c file or done once.

// Stub implementation until cgltf is available via vcpkg build
// This provides the interface; actual parsing will work when dependencies are installed.

namespace nge::assets {

// ─── glTF Loading ────────────────────────────────────────────────────────

bool MeshLoader::LoadGLTF(const std::string& path, MeshData& outMesh) {
    NGE_LOG_INFO("Loading mesh: {}", path);

    // TODO: Implement with cgltf when vcpkg dependencies are built.
    // The flow is:
    //   1. cgltf_parse_file() → parse glTF/GLB
    //   2. cgltf_load_buffers() → load binary data
    //   3. Iterate scenes → nodes → meshes → primitives
    //   4. Extract vertex attributes (position, normal, texcoord, tangent)
    //   5. Extract indices
    //   6. Extract materials
    //   7. cgltf_free()

    // For now, generate a test cube
    outMesh.name = path;

    // Cube vertices (24 = 6 faces × 4 vertices)
    const f32 s = 0.5f;
    outMesh.vertices = {
        // Front face
        {{-s, -s,  s}, {0,0,1}, {0,0}, {1,0,0,1}},
        {{ s, -s,  s}, {0,0,1}, {1,0}, {1,0,0,1}},
        {{ s,  s,  s}, {0,0,1}, {1,1}, {1,0,0,1}},
        {{-s,  s,  s}, {0,0,1}, {0,1}, {1,0,0,1}},
        // Back face
        {{ s, -s, -s}, {0,0,-1}, {0,0}, {-1,0,0,1}},
        {{-s, -s, -s}, {0,0,-1}, {1,0}, {-1,0,0,1}},
        {{-s,  s, -s}, {0,0,-1}, {1,1}, {-1,0,0,1}},
        {{ s,  s, -s}, {0,0,-1}, {0,1}, {-1,0,0,1}},
        // Right face
        {{ s, -s,  s}, {1,0,0}, {0,0}, {0,0,1,1}},
        {{ s, -s, -s}, {1,0,0}, {1,0}, {0,0,1,1}},
        {{ s,  s, -s}, {1,0,0}, {1,1}, {0,0,1,1}},
        {{ s,  s,  s}, {1,0,0}, {0,1}, {0,0,1,1}},
        // Left face
        {{-s, -s, -s}, {-1,0,0}, {0,0}, {0,0,-1,1}},
        {{-s, -s,  s}, {-1,0,0}, {1,0}, {0,0,-1,1}},
        {{-s,  s,  s}, {-1,0,0}, {1,1}, {0,0,-1,1}},
        {{-s,  s, -s}, {-1,0,0}, {0,1}, {0,0,-1,1}},
        // Top face
        {{-s,  s,  s}, {0,1,0}, {0,0}, {1,0,0,1}},
        {{ s,  s,  s}, {0,1,0}, {1,0}, {1,0,0,1}},
        {{ s,  s, -s}, {0,1,0}, {1,1}, {1,0,0,1}},
        {{-s,  s, -s}, {0,1,0}, {0,1}, {1,0,0,1}},
        // Bottom face
        {{-s, -s, -s}, {0,-1,0}, {0,0}, {1,0,0,1}},
        {{ s, -s, -s}, {0,-1,0}, {1,0}, {1,0,0,1}},
        {{ s, -s,  s}, {0,-1,0}, {1,1}, {1,0,0,1}},
        {{-s, -s,  s}, {0,-1,0}, {0,1}, {1,0,0,1}},
    };

    outMesh.indices = {
         0, 1, 2,  0, 2, 3,   // Front
         4, 5, 6,  4, 6, 7,   // Back
         8, 9,10,  8,10,11,   // Right
        12,13,14, 12,14,15,   // Left
        16,17,18, 16,18,19,   // Top
        20,21,22, 20,22,23,   // Bottom
    };

    SubMesh sub;
    sub.vertexOffset = 0;
    sub.vertexCount  = static_cast<u32>(outMesh.vertices.size());
    sub.indexOffset   = 0;
    sub.indexCount    = static_cast<u32>(outMesh.indices.size());
    sub.materialIndex = 0;
    outMesh.subMeshes.push_back(sub);

    MaterialData mat;
    mat.name = "DefaultMaterial";
    mat.baseColorFactor = {0.8f, 0.8f, 0.8f, 1.0f};
    mat.roughnessFactor = 0.5f;
    mat.metallicFactor  = 0.0f;
    outMesh.materials.push_back(mat);

    ComputeBounds(outMesh);

    NGE_LOG_INFO("Loaded mesh '{}': {} verts, {} tris, {} submeshes",
                 outMesh.name, outMesh.GetTotalVertices(), outMesh.GetTotalTriangles(),
                 outMesh.subMeshes.size());
    return true;
}

void MeshLoader::ComputeBounds(MeshData& mesh) {
    if (mesh.vertices.empty()) return;

    math::Vec3 minP = mesh.vertices[0].position;
    math::Vec3 maxP = mesh.vertices[0].position;

    for (const auto& v : mesh.vertices) {
        minP.x = math::Min(minP.x, v.position.x);
        minP.y = math::Min(minP.y, v.position.y);
        minP.z = math::Min(minP.z, v.position.z);
        maxP.x = math::Max(maxP.x, v.position.x);
        maxP.y = math::Max(maxP.y, v.position.y);
        maxP.z = math::Max(maxP.z, v.position.z);
    }

    mesh.aabbMin = minP;
    mesh.aabbMax = maxP;
    mesh.boundCenter = (minP + maxP) * 0.5f;

    f32 maxDist = 0;
    for (const auto& v : mesh.vertices) {
        math::Vec3 d = v.position - mesh.boundCenter;
        f32 dist = d.LengthSq();
        if (dist > maxDist) maxDist = dist;
    }
    mesh.boundRadius = math::Sqrt(maxDist);

    // Update submesh bounds
    for (auto& sub : mesh.subMeshes) {
        math::Vec3 subMin = mesh.vertices[sub.vertexOffset].position;
        math::Vec3 subMax = subMin;

        for (u32 i = 0; i < sub.vertexCount; ++i) {
            const auto& p = mesh.vertices[sub.vertexOffset + i].position;
            subMin.x = math::Min(subMin.x, p.x);
            subMin.y = math::Min(subMin.y, p.y);
            subMin.z = math::Min(subMin.z, p.z);
            subMax.x = math::Max(subMax.x, p.x);
            subMax.y = math::Max(subMax.y, p.y);
            subMax.z = math::Max(subMax.z, p.z);
        }

        sub.aabbMin = subMin;
        sub.aabbMax = subMax;
        sub.boundCenter = (subMin + subMax) * 0.5f;

        f32 subMaxDist = 0;
        for (u32 i = 0; i < sub.vertexCount; ++i) {
            math::Vec3 d = mesh.vertices[sub.vertexOffset + i].position - sub.boundCenter;
            f32 dist = d.LengthSq();
            if (dist > subMaxDist) subMaxDist = dist;
        }
        sub.boundRadius = math::Sqrt(subMaxDist);
    }
}

void MeshLoader::GenerateTangents(MeshData& mesh) {
    // MikkTSpace tangent generation
    // For each triangle, compute tangent and bitangent from UV gradients
    // This is a simplified version; production would use mikktspace library.

    std::vector<math::Vec3> tangents(mesh.vertices.size(), {0,0,0});
    std::vector<math::Vec3> bitangents(mesh.vertices.size(), {0,0,0});

    for (usize i = 0; i < mesh.indices.size(); i += 3) {
        u32 i0 = mesh.indices[i + 0];
        u32 i1 = mesh.indices[i + 1];
        u32 i2 = mesh.indices[i + 2];

        const auto& v0 = mesh.vertices[i0];
        const auto& v1 = mesh.vertices[i1];
        const auto& v2 = mesh.vertices[i2];

        math::Vec3 edge1 = v1.position - v0.position;
        math::Vec3 edge2 = v2.position - v0.position;
        math::Vec2 duv1 = v1.texcoord0 - v0.texcoord0;
        math::Vec2 duv2 = v2.texcoord0 - v0.texcoord0;

        f32 denom = duv1.x * duv2.y - duv2.x * duv1.y;
        if (math::Abs(denom) < math::EPSILON) continue;
        f32 r = 1.0f / denom;

        math::Vec3 t = (edge1 * duv2.y - edge2 * duv1.y) * r;
        math::Vec3 b = (edge2 * duv1.x - edge1 * duv2.x) * r;

        tangents[i0] = tangents[i0] + t;
        tangents[i1] = tangents[i1] + t;
        tangents[i2] = tangents[i2] + t;
        bitangents[i0] = bitangents[i0] + b;
        bitangents[i1] = bitangents[i1] + b;
        bitangents[i2] = bitangents[i2] + b;
    }

    // Orthonormalize and compute handedness
    for (usize i = 0; i < mesh.vertices.size(); ++i) {
        const math::Vec3& n = mesh.vertices[i].normal;
        math::Vec3& t = tangents[i];

        // Gram-Schmidt orthonormalize
        t = (t - n * n.Dot(t)).Normalized();

        // Handedness
        f32 w = (n.Cross(t).Dot(bitangents[i]) < 0.0f) ? -1.0f : 1.0f;
        mesh.vertices[i].tangent = {t.x, t.y, t.z, w};
    }
}

// ─── Meshlet Builder ─────────────────────────────────────────────────────

void MeshletBuilder::Build(MeshData& mesh) {
    mesh.meshlets.clear();
    mesh.meshletVertices.clear();
    mesh.meshletTriangles.clear();

    for (auto& sub : mesh.subMeshes) {
        u32 meshletStart = static_cast<u32>(mesh.meshlets.size());

        // Simple greedy meshlet construction
        // Production: use meshoptimizer's meshopt_buildMeshlets()
        const u32 triCount = sub.indexCount / 3;
        u32 triIdx = 0;

        while (triIdx < triCount) {
            Meshlet meshlet{};
            meshlet.vertexOffset   = static_cast<u32>(mesh.meshletVertices.size());
            meshlet.triangleOffset = static_cast<u32>(mesh.meshletTriangles.size());

            // Map from global vertex index to local meshlet vertex index
            std::unordered_map<u32, u32> vertexMap;
            u32 localVertCount = 0;
            u32 localTriCount  = 0;

            while (triIdx < triCount && localTriCount < MAX_MESHLET_TRIANGLES) {
                u32 globalIndices[3];
                for (int k = 0; k < 3; ++k) {
                    globalIndices[k] = mesh.indices[sub.indexOffset + triIdx * 3 + k];
                }

                // Check if adding this triangle would exceed vertex limit
                u32 newVerts = 0;
                for (int k = 0; k < 3; ++k) {
                    if (vertexMap.find(globalIndices[k]) == vertexMap.end()) newVerts++;
                }

                if (localVertCount + newVerts > MAX_MESHLET_VERTICES) break;

                // Add vertices
                u8 localTri[3];
                for (int k = 0; k < 3; ++k) {
                    u32 gi = globalIndices[k];
                    auto it = vertexMap.find(gi);
                    if (it == vertexMap.end()) {
                        u32 localIdx = localVertCount++;
                        vertexMap[gi] = localIdx;
                        mesh.meshletVertices.push_back(gi);
                        localTri[k] = static_cast<u8>(localIdx);
                    } else {
                        localTri[k] = static_cast<u8>(it->second);
                    }
                }

                mesh.meshletTriangles.push_back(localTri[0]);
                mesh.meshletTriangles.push_back(localTri[1]);
                mesh.meshletTriangles.push_back(localTri[2]);

                localTriCount++;
                triIdx++;
            }

            meshlet.vertexCount   = localVertCount;
            meshlet.triangleCount = localTriCount;
            mesh.meshlets.push_back(meshlet);
        }

        sub.meshletOffset = meshletStart;
        sub.meshletCount  = static_cast<u32>(mesh.meshlets.size()) - meshletStart;
    }

    ComputeMeshletBounds(mesh);

    NGE_LOG_INFO("Built {} meshlets ({} verts, {} tris in buffer)",
                 mesh.meshlets.size(), mesh.meshletVertices.size(),
                 mesh.meshletTriangles.size() / 3);
}

void MeshletBuilder::ComputeMeshletBounds(MeshData& mesh) {
    for (auto& meshlet : mesh.meshlets) {
        // Bounding sphere
        math::Vec3 center{0, 0, 0};
        for (u32 i = 0; i < meshlet.vertexCount; ++i) {
            u32 vi = mesh.meshletVertices[meshlet.vertexOffset + i];
            center = center + mesh.vertices[vi].position;
        }
        if (meshlet.vertexCount > 0) {
            center = center * (1.0f / static_cast<f32>(meshlet.vertexCount));
        }

        f32 maxDist = 0;
        for (u32 i = 0; i < meshlet.vertexCount; ++i) {
            u32 vi = mesh.meshletVertices[meshlet.vertexOffset + i];
            math::Vec3 d = mesh.vertices[vi].position - center;
            f32 dist = d.LengthSq();
            if (dist > maxDist) maxDist = dist;
        }

        meshlet.center = center;
        meshlet.radius = math::Sqrt(maxDist);

        // Normal cone for backface culling
        // Average all triangle normals to get cone axis
        math::Vec3 avgNormal{0, 0, 0};
        for (u32 t = 0; t < meshlet.triangleCount; ++t) {
            u32 base = meshlet.triangleOffset + t * 3;
            u32 i0 = mesh.meshletVertices[meshlet.vertexOffset + mesh.meshletTriangles[base + 0]];
            u32 i1 = mesh.meshletVertices[meshlet.vertexOffset + mesh.meshletTriangles[base + 1]];
            u32 i2 = mesh.meshletVertices[meshlet.vertexOffset + mesh.meshletTriangles[base + 2]];

            math::Vec3 e1 = mesh.vertices[i1].position - mesh.vertices[i0].position;
            math::Vec3 e2 = mesh.vertices[i2].position - mesh.vertices[i0].position;
            math::Vec3 triNormal = e1.Cross(e2);
            f32 len = triNormal.Length();
            if (len > math::EPSILON) triNormal = triNormal * (1.0f / len);
            avgNormal = avgNormal + triNormal;
        }

        f32 avgLen = avgNormal.Length();
        if (avgLen > math::EPSILON) {
            meshlet.coneAxis = avgNormal * (1.0f / avgLen);
        } else {
            meshlet.coneAxis = {0, 1, 0};
        }

        // Find maximum deviation from the cone axis
        f32 minDot = 1.0f;
        for (u32 t = 0; t < meshlet.triangleCount; ++t) {
            u32 base = meshlet.triangleOffset + t * 3;
            u32 i0 = mesh.meshletVertices[meshlet.vertexOffset + mesh.meshletTriangles[base + 0]];
            u32 i1 = mesh.meshletVertices[meshlet.vertexOffset + mesh.meshletTriangles[base + 1]];
            u32 i2 = mesh.meshletVertices[meshlet.vertexOffset + mesh.meshletTriangles[base + 2]];

            math::Vec3 e1 = mesh.vertices[i1].position - mesh.vertices[i0].position;
            math::Vec3 e2 = mesh.vertices[i2].position - mesh.vertices[i0].position;
            math::Vec3 n = e1.Cross(e2);
            f32 len = n.Length();
            if (len > math::EPSILON) {
                n = n * (1.0f / len);
                f32 d = n.Dot(meshlet.coneAxis);
                if (d < minDot) minDot = d;
            }
        }

        meshlet.coneCutoff = minDot;
    }
}

void MeshLoader::ProcessNode(const void* /*cgltfData*/, const void* /*node*/, MeshData& /*outMesh*/) {
    // cgltf node traversal — implemented when cgltf is available
}

void MeshLoader::ProcessPrimitive(const void* /*cgltfData*/, const void* /*primitive*/,
                                   MeshData& /*outMesh*/, u32 /*materialIndex*/) {
    // cgltf primitive extraction — implemented when cgltf is available
}

} // namespace nge::assets
