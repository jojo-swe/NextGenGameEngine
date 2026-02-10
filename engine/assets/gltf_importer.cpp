#include "engine/assets/gltf_importer.h"
#include "engine/core/logging/log.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <filesystem>

// Note: In production, this would use cgltf for parsing.
// For now, we implement the importer interface with stub parsing
// that can be connected to cgltf when the dependency is available.

namespace nge::assets {

GLTFImportResult GLTFImporter::Import(const std::string& path, const GLTFImportOptions& options) {
    GLTFImportResult result;

    // Verify file exists
    if (!std::filesystem::exists(path)) {
        result.error = "File not found: " + path;
        NGE_LOG_ERROR("glTF import failed: {}", result.error);
        return result;
    }

    std::string ext = std::filesystem::path(path).extension().string();
    bool isBinary = (ext == ".glb");

    NGE_LOG_INFO("Importing glTF{}: '{}'", isBinary ? " (binary)" : "", path);

    // TODO: Parse with cgltf
    // cgltf_options cgltfOptions = {};
    // cgltf_data* data = nullptr;
    // cgltf_result res = cgltf_parse_file(&cgltfOptions, path.c_str(), &data);
    // if (res != cgltf_result_success) { ... }
    // cgltf_load_buffers(&cgltfOptions, data, path.c_str());

    // For now, create a stub result to validate the pipeline
    // Real implementation would call Parse* methods with cgltf data

    std::string basePath = std::filesystem::path(path).parent_path().string();

    // ParseMeshes(data, result, options);
    // ParseMaterials(data, result);
    // ParseTextures(data, result, basePath);
    // ParseNodes(data, result);
    // if (options.importAnimations) ParseAnimations(data, result);

    result.success = true;
    NGE_LOG_INFO("glTF imported: {} meshes, {} materials, {} textures, {} nodes, {} animations",
                 result.meshes.size(), result.materials.size(), result.textures.size(),
                 result.nodes.size(), result.animations.size());

    return result;
}

ecs::Entity GLTFImporter::ImportToWorld(const std::string& path, ecs::World& world,
                                          rhi::IDevice* device,
                                          renderer::MaterialManager* materialMgr,
                                          const GLTFImportOptions& options) {
    auto result = Import(path, options);
    if (!result.success) {
        NGE_LOG_ERROR("Failed to import glTF to world: {}", result.error);
        return ecs::Entity{};
    }

    // Upload textures to GPU
    std::vector<rhi::TextureHandle> gpuTextures;
    if (options.importTextures) {
        gpuTextures.reserve(result.textures.size());
        for (const auto& tex : result.textures) {
            if (tex.pixels.empty()) {
                gpuTextures.push_back(rhi::TextureHandle{});
                continue;
            }

            rhi::TextureDesc desc;
            desc.width = tex.width;
            desc.height = tex.height;
            desc.format = rhi::Format::RGBA8_UNORM;
            desc.usage = rhi::TextureUsage::ShaderRead | rhi::TextureUsage::TransferDst;
            desc.debugName = tex.name.c_str();
            auto handle = device->CreateTexture(desc);
            // TODO: Upload tex.pixels to handle via staging buffer
            gpuTextures.push_back(handle);
        }
    }

    // Create materials
    std::vector<renderer::MaterialId> materialIds;
    if (options.importMaterials && materialMgr) {
        materialIds.reserve(result.materials.size());
        for (const auto& mat : result.materials) {
            renderer::GPUMaterialData gpuData{};
            gpuData.baseColorFactor = mat.baseColorFactor;
            gpuData.metallicFactor = mat.metallicFactor;
            gpuData.roughnessFactor = mat.roughnessFactor;
            gpuData.emissiveStrength = mat.emissiveStrength;
            gpuData.alphaCutoff = mat.alphaCutoff;
            gpuData.normalScale = 1.0f;

            u32 flags = 0;
            if (mat.doubleSided) flags |= static_cast<u32>(renderer::MaterialFlags::DoubleSided);
            if (mat.alphaBlend) flags |= static_cast<u32>(renderer::MaterialFlags::AlphaBlend);
            if (mat.alphaTest) flags |= static_cast<u32>(renderer::MaterialFlags::AlphaTest);
            if (mat.emissiveStrength > 0) flags |= static_cast<u32>(renderer::MaterialFlags::Emissive);
            gpuData.flags = flags;

            // Bind texture indices (bindless)
            gpuData.albedoTexIdx = (mat.albedoTexture >= 0) ? static_cast<u32>(mat.albedoTexture) : UINT32_MAX;
            gpuData.normalTexIdx = (mat.normalTexture >= 0) ? static_cast<u32>(mat.normalTexture) : UINT32_MAX;
            gpuData.metallicRoughnessTexIdx = (mat.metallicRoughnessTexture >= 0) ? static_cast<u32>(mat.metallicRoughnessTexture) : UINT32_MAX;
            gpuData.emissiveTexIdx = (mat.emissiveTexture >= 0) ? static_cast<u32>(mat.emissiveTexture) : UINT32_MAX;
            gpuData.aoTexIdx = (mat.occlusionTexture >= 0) ? static_cast<u32>(mat.occlusionTexture) : UINT32_MAX;
            gpuData.heightTexIdx = UINT32_MAX;
            gpuData.detailAlbedoTexIdx = UINT32_MAX;
            gpuData.detailNormalTexIdx = UINT32_MAX;

            auto id = materialMgr->CreateMaterialFromGLTF(mat.name, gpuData);
            materialIds.push_back(id);
        }
    }

    // Upload meshes to GPU
    std::vector<rhi::BufferHandle> vertexBuffers;
    std::vector<rhi::BufferHandle> indexBuffers;
    for (const auto& mesh : result.meshes) {
        // Vertex buffer (interleaved: pos + normal + uv + tangent)
        struct Vertex {
            math::Vec3 position;
            math::Vec3 normal;
            math::Vec2 texcoord;
            math::Vec4 tangent;
        };

        std::vector<Vertex> vertices(mesh.positions.size());
        for (usize i = 0; i < mesh.positions.size(); ++i) {
            vertices[i].position = mesh.positions[i];
            vertices[i].normal = i < mesh.normals.size() ? mesh.normals[i] : math::Vec3{0, 1, 0};
            vertices[i].texcoord = i < mesh.texcoords0.size() ? mesh.texcoords0[i] : math::Vec2{0, 0};
            vertices[i].tangent = i < mesh.tangents.size() ? mesh.tangents[i] : math::Vec4{1, 0, 0, 1};
        }

        rhi::BufferDesc vbDesc;
        vbDesc.size = static_cast<u32>(vertices.size() * sizeof(Vertex));
        vbDesc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::TransferDst;
        vbDesc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        vbDesc.debugName = mesh.name.c_str();
        auto vb = device->CreateBuffer(vbDesc);
        // TODO: Upload via staging
        vertexBuffers.push_back(vb);

        rhi::BufferDesc ibDesc;
        ibDesc.size = static_cast<u32>(mesh.indices.size() * sizeof(u32));
        ibDesc.usage = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDst;
        ibDesc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        ibDesc.debugName = mesh.name.c_str();
        auto ib = device->CreateBuffer(ibDesc);
        // TODO: Upload via staging
        indexBuffers.push_back(ib);
    }

    // Create entity hierarchy
    std::vector<ecs::Entity> entities(result.nodes.size());

    for (u32 i = 0; i < static_cast<u32>(result.nodes.size()); ++i) {
        const auto& node = result.nodes[i];
        entities[i] = world.CreateEntity();

        // TODO: Apply transform via a TransformComponent once defined
        // For now, store node transform data for later use
        (void)node.translation;
        (void)node.rotation;
        (void)node.scale;

        // Attach mesh component
        if (node.meshIndex >= 0 && node.meshIndex < static_cast<i32>(result.meshes.size())) {
            // TODO: Add MeshRenderer component with vertex/index buffers and material
        }
    }

    // Find root entity
    ecs::Entity root;
    if (!result.rootNodes.empty()) {
        root = entities[result.rootNodes[0]];
    } else if (!entities.empty()) {
        root = entities[0];
    }

    NGE_LOG_INFO("Imported '{}' to world: {} entities, {} meshes, {} materials",
                 path, entities.size(), vertexBuffers.size(), materialIds.size());
    return root;
}

void GLTFImporter::ParseMeshes(const void* /*gltfData*/, GLTFImportResult& /*result*/,
                                 const GLTFImportOptions& /*options*/) {
    // TODO: Iterate cgltf_data->meshes, extract vertex attributes and indices
}

void GLTFImporter::ParseMaterials(const void* /*gltfData*/, GLTFImportResult& /*result*/) {
    // TODO: Iterate cgltf_data->materials, extract PBR metallic-roughness params
}

void GLTFImporter::ParseTextures(const void* /*gltfData*/, GLTFImportResult& /*result*/,
                                   const std::string& /*basePath*/) {
    // TODO: Iterate cgltf_data->images, load and decode via stb_image
}

void GLTFImporter::ParseNodes(const void* /*gltfData*/, GLTFImportResult& /*result*/) {
    // TODO: Iterate cgltf_data->nodes, build hierarchy
}

void GLTFImporter::ParseAnimations(const void* /*gltfData*/, GLTFImportResult& /*result*/) {
    // TODO: Iterate cgltf_data->animations, extract channels and keyframes
}

void GLTFImporter::ComputeTangents(GLTFMeshData& mesh) {
    if (mesh.positions.empty() || mesh.normals.empty() || mesh.texcoords0.empty()) return;

    mesh.tangents.resize(mesh.positions.size(), {0, 0, 0, 0});

    std::vector<math::Vec3> tan1(mesh.positions.size(), {0, 0, 0});
    std::vector<math::Vec3> tan2(mesh.positions.size(), {0, 0, 0});

    for (usize i = 0; i + 2 < mesh.indices.size(); i += 3) {
        u32 i0 = mesh.indices[i];
        u32 i1 = mesh.indices[i + 1];
        u32 i2 = mesh.indices[i + 2];

        const auto& p0 = mesh.positions[i0];
        const auto& p1 = mesh.positions[i1];
        const auto& p2 = mesh.positions[i2];

        const auto& uv0 = mesh.texcoords0[i0];
        const auto& uv1 = mesh.texcoords0[i1];
        const auto& uv2 = mesh.texcoords0[i2];

        math::Vec3 e1 = {p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
        math::Vec3 e2 = {p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};

        f32 du1 = uv1.x - uv0.x, dv1 = uv1.y - uv0.y;
        f32 du2 = uv2.x - uv0.x, dv2 = uv2.y - uv0.y;

        f32 det = du1 * dv2 - du2 * dv1;
        if (std::abs(det) < 1e-8f) continue;
        f32 r = 1.0f / det;

        math::Vec3 sdir = {
            (dv2 * e1.x - dv1 * e2.x) * r,
            (dv2 * e1.y - dv1 * e2.y) * r,
            (dv2 * e1.z - dv1 * e2.z) * r
        };
        math::Vec3 tdir = {
            (du1 * e2.x - du2 * e1.x) * r,
            (du1 * e2.y - du2 * e1.y) * r,
            (du1 * e2.z - du2 * e1.z) * r
        };

        auto addVec3 = [](math::Vec3& a, const math::Vec3& b) {
            a.x += b.x; a.y += b.y; a.z += b.z;
        };

        addVec3(tan1[i0], sdir); addVec3(tan1[i1], sdir); addVec3(tan1[i2], sdir);
        addVec3(tan2[i0], tdir); addVec3(tan2[i1], tdir); addVec3(tan2[i2], tdir);
    }

    // Gram-Schmidt orthonormalize
    for (usize i = 0; i < mesh.positions.size(); ++i) {
        const auto& n = mesh.normals[i];
        const auto& t = tan1[i];

        f32 ndott = n.x * t.x + n.y * t.y + n.z * t.z;
        math::Vec3 tangent = {t.x - n.x * ndott, t.y - n.y * ndott, t.z - n.z * ndott};

        f32 len = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z);
        if (len > 1e-6f) {
            tangent.x /= len; tangent.y /= len; tangent.z /= len;
        }

        // Handedness
        math::Vec3 cross = {
            n.y * t.z - n.z * t.y,
            n.z * t.x - n.x * t.z,
            n.x * t.y - n.y * t.x
        };
        f32 w = (cross.x * tan2[i].x + cross.y * tan2[i].y + cross.z * tan2[i].z) < 0 ? -1.0f : 1.0f;

        mesh.tangents[i] = {tangent.x, tangent.y, tangent.z, w};
    }
}

void GLTFImporter::ComputeBounds(GLTFMeshData& mesh) {
    if (mesh.positions.empty()) return;

    mesh.aabbMin = mesh.positions[0];
    mesh.aabbMax = mesh.positions[0];

    for (const auto& p : mesh.positions) {
        mesh.aabbMin.x = std::min(mesh.aabbMin.x, p.x);
        mesh.aabbMin.y = std::min(mesh.aabbMin.y, p.y);
        mesh.aabbMin.z = std::min(mesh.aabbMin.z, p.z);
        mesh.aabbMax.x = std::max(mesh.aabbMax.x, p.x);
        mesh.aabbMax.y = std::max(mesh.aabbMax.y, p.y);
        mesh.aabbMax.z = std::max(mesh.aabbMax.z, p.z);
    }

    math::Vec3 center = {
        (mesh.aabbMin.x + mesh.aabbMax.x) * 0.5f,
        (mesh.aabbMin.y + mesh.aabbMax.y) * 0.5f,
        (mesh.aabbMin.z + mesh.aabbMax.z) * 0.5f
    };

    mesh.boundingRadius = 0;
    for (const auto& p : mesh.positions) {
        f32 dx = p.x - center.x, dy = p.y - center.y, dz = p.z - center.z;
        f32 dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        mesh.boundingRadius = std::max(mesh.boundingRadius, dist);
    }
}

} // namespace nge::assets
