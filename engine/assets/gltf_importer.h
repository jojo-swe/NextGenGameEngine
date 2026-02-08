#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/core/ecs/world.h"
#include "engine/renderer/materials/material_system.h"
#include "engine/rhi/common/rhi_device.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace nge::assets {

// ─── glTF Scene Importer ─────────────────────────────────────────────────
// Loads glTF 2.0 files (.gltf / .glb) into the engine's ECS world.
// Imports meshes, materials, textures, node hierarchy, and animations.
// Uses cgltf for parsing.

// ─── Import Options ──────────────────────────────────────────────────────

struct GLTFImportOptions {
    bool importMaterials = true;
    bool importTextures = true;
    bool importAnimations = true;
    bool importLights = true;
    bool importCameras = true;
    bool generateMeshlets = true;    // Auto-generate meshlets from meshes
    bool generateTangents = true;    // Compute tangents if missing
    bool flipWindingOrder = false;
    bool flipUVs = false;            // Flip V coordinate
    f32  globalScale = 1.0f;
    math::Vec3 globalOffset = {0, 0, 0};
};

// ─── Import Result ───────────────────────────────────────────────────────

struct GLTFMeshData {
    std::string name;
    std::vector<math::Vec3> positions;
    std::vector<math::Vec3> normals;
    std::vector<math::Vec4> tangents;
    std::vector<math::Vec2> texcoords0;
    std::vector<math::Vec2> texcoords1;
    std::vector<math::Vec4> colors;
    std::vector<math::Vec4> joints;   // Bone indices (as float4)
    std::vector<math::Vec4> weights;  // Bone weights
    std::vector<u32>        indices;

    // Sub-meshes (one per material)
    struct Primitive {
        u32 indexOffset;
        u32 indexCount;
        u32 materialIndex;
    };
    std::vector<Primitive> primitives;

    // Bounding volume
    math::Vec3 aabbMin;
    math::Vec3 aabbMax;
    f32        boundingRadius;
};

struct GLTFTextureData {
    std::string name;
    std::string uri;        // Relative path or embedded data URI
    u32         width = 0;
    u32         height = 0;
    u32         channels = 0;
    std::vector<u8> pixels; // Decoded pixel data (RGBA8)
};

struct GLTFMaterialData {
    std::string name;
    math::Vec4  baseColorFactor = {1, 1, 1, 1};
    f32         metallicFactor = 0.0f;
    f32         roughnessFactor = 1.0f;
    f32         emissiveStrength = 0.0f;
    math::Vec3  emissiveFactor = {0, 0, 0};
    f32         alphaCutoff = 0.5f;
    bool        doubleSided = false;
    bool        alphaBlend = false;
    bool        alphaTest = false;

    // Texture indices (-1 = none)
    i32 albedoTexture = -1;
    i32 normalTexture = -1;
    i32 metallicRoughnessTexture = -1;
    i32 emissiveTexture = -1;
    i32 occlusionTexture = -1;
};

struct GLTFNodeData {
    std::string name;
    i32         parentIndex = -1;
    math::Vec3  translation = {0, 0, 0};
    math::Vec4  rotation = {0, 0, 0, 1}; // Quaternion (x, y, z, w)
    math::Vec3  scale = {1, 1, 1};
    i32         meshIndex = -1;
    i32         cameraIndex = -1;
    i32         lightIndex = -1;
    std::vector<u32> children;
};

struct GLTFAnimationChannel {
    u32         nodeIndex;
    enum class Path : u8 { Translation, Rotation, Scale, Weights } path;
    enum class Interpolation : u8 { Linear, Step, CubicSpline } interpolation;
    std::vector<f32> timestamps;
    std::vector<f32> values; // Flattened keyframe values
};

struct GLTFAnimationData {
    std::string name;
    f32         duration = 0;
    std::vector<GLTFAnimationChannel> channels;
};

struct GLTFImportResult {
    bool success = false;
    std::string error;

    std::vector<GLTFMeshData>      meshes;
    std::vector<GLTFTextureData>   textures;
    std::vector<GLTFMaterialData>  materials;
    std::vector<GLTFNodeData>      nodes;
    std::vector<GLTFAnimationData> animations;

    // Root node indices
    std::vector<u32> rootNodes;
};

// ─── Importer ────────────────────────────────────────────────────────────

class GLTFImporter {
public:
    // Parse a glTF file and return raw data
    GLTFImportResult Import(const std::string& path, const GLTFImportOptions& options = {});

    // Import directly into an ECS world (creates entities, uploads GPU resources)
    ecs::Entity ImportToWorld(const std::string& path, ecs::World& world,
                               rhi::IDevice* device, renderer::MaterialManager* materialMgr,
                               const GLTFImportOptions& options = {});

private:
    void ParseMeshes(const void* gltfData, GLTFImportResult& result, const GLTFImportOptions& options);
    void ParseMaterials(const void* gltfData, GLTFImportResult& result);
    void ParseTextures(const void* gltfData, GLTFImportResult& result, const std::string& basePath);
    void ParseNodes(const void* gltfData, GLTFImportResult& result);
    void ParseAnimations(const void* gltfData, GLTFImportResult& result);
    void ComputeTangents(GLTFMeshData& mesh);
    void ComputeBounds(GLTFMeshData& mesh);
};

} // namespace nge::assets
