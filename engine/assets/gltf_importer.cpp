#include "engine/assets/gltf_importer.h"
#include "engine/core/logging/log.h"
#include "engine/scene/transform/transform.h"
#include "engine/scene/mesh_renderer.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <filesystem>

#ifdef NGE_HAS_CGLTF
#include <cgltf.h>
#endif

#ifdef NGE_HAS_STB
#include <stb_image.h>
#endif

namespace nge::assets {

namespace {

// Upload data to a GPU-only buffer via a staging buffer and command list.
void UploadBufferViaStaging(rhi::IDevice* device, rhi::BufferHandle gpuBuffer,
                             const void* data, usize size) {
    // Create staging buffer (CPU-visible)
    rhi::BufferDesc stagingDesc;
    stagingDesc.size = size;
    stagingDesc.usage = rhi::BufferUsage::TransferSrc;
    stagingDesc.memoryUsage = rhi::MemoryUsage::CPU_To_GPU;
    stagingDesc.debugName = "staging_upload";
    auto staging = device->CreateBuffer(stagingDesc);
    if (!staging.IsValid()) {
        NGE_LOG_ERROR("Failed to create staging buffer for GPU upload");
        return;
    }

    // Map, copy, unmap
    void* mapped = device->MapBuffer(staging);
    if (mapped) {
        std::memcpy(mapped, data, size);
        device->UnmapBuffer(staging);
    }

    // Record copy command and submit immediately
    auto* cmd = device->GetCommandList();
    cmd->Begin();
    cmd->BufferBarrier(staging, rhi::ResourceState::Undefined, rhi::ResourceState::TransferSrc);
    cmd->BufferBarrier(gpuBuffer, rhi::ResourceState::Undefined, rhi::ResourceState::TransferDst);
    cmd->CopyBuffer(staging, 0, gpuBuffer, 0, size);
    cmd->BufferBarrier(gpuBuffer, rhi::ResourceState::TransferDst, rhi::ResourceState::VertexBuffer);
    cmd->End();
    device->SubmitCommandList(cmd);

    // Destroy staging buffer (safe after submit since GPU will have a copy)
    device->DestroyBuffer(staging);
}

// Upload pixel data to a GPU-only texture via a staging buffer.
void UploadTextureViaStaging(rhi::IDevice* device, rhi::TextureHandle gpuTexture,
                              const void* data, usize size, u32 /*width*/, u32 /*height*/) {
    // Create staging buffer
    rhi::BufferDesc stagingDesc;
    stagingDesc.size = size;
    stagingDesc.usage = rhi::BufferUsage::TransferSrc;
    stagingDesc.memoryUsage = rhi::MemoryUsage::CPU_To_GPU;
    stagingDesc.debugName = "staging_texture_upload";
    auto staging = device->CreateBuffer(stagingDesc);
    if (!staging.IsValid()) {
        NGE_LOG_ERROR("Failed to create staging buffer for texture upload");
        return;
    }

    // Map, copy, unmap
    void* mapped = device->MapBuffer(staging);
    if (mapped) {
        std::memcpy(mapped, data, size);
        device->UnmapBuffer(staging);
    }

    // Record copy command and submit
    auto* cmd = device->GetCommandList();
    cmd->Begin();
    cmd->BufferBarrier(staging, rhi::ResourceState::Undefined, rhi::ResourceState::TransferSrc);
    cmd->TextureBarrier(gpuTexture, rhi::ResourceState::Undefined, rhi::ResourceState::TransferDst);
    cmd->CopyBufferToTexture(staging, gpuTexture, 0, 0);
    cmd->TextureBarrier(gpuTexture, rhi::ResourceState::TransferDst, rhi::ResourceState::ShaderRead);
    cmd->End();
    device->SubmitCommandList(cmd);

    device->DestroyBuffer(staging);
}

} // anonymous namespace

GLTFImportResult GLTFImporter::Import(const std::string& path, [[maybe_unused]] const GLTFImportOptions& options) {
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

    std::string basePath = std::filesystem::path(path).parent_path().string();

#ifdef NGE_HAS_CGLTF
    cgltf_options cgltfOptions = {};
    cgltf_data* data = nullptr;
    cgltf_result res = cgltf_parse_file(&cgltfOptions, path.c_str(), &data);
    if (res != cgltf_result_success) {
        result.error = "cgltf_parse_file failed with code " + std::to_string(res);
        NGE_LOG_ERROR("glTF parse failed: {}", result.error);
        return result;
    }

    res = cgltf_load_buffers(&cgltfOptions, data, path.c_str());
    if (res != cgltf_result_success) {
        result.error = "cgltf_load_buffers failed with code " + std::to_string(res);
        NGE_LOG_ERROR("glTF buffer load failed: {}", result.error);
        cgltf_free(data);
        return result;
    }

    ParseMeshes(data, result, options);
    ParseMaterials(data, result);
    ParseTextures(data, result, basePath);
    ParseNodes(data, result);
    if (options.importAnimations) ParseAnimations(data, result);

    cgltf_free(data);

    result.success = true;
#else
    result.error = "Engine built without cgltf support (NGE_HAS_CGLTF not defined)";
    NGE_LOG_ERROR("{}", result.error);
#endif

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
            UploadTextureViaStaging(device, handle, tex.pixels.data(),
                tex.pixels.size(), tex.width, tex.height);
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
        if (!vertices.empty()) {
            UploadBufferViaStaging(device, vb, vertices.data(),
                vertices.size() * sizeof(Vertex));
        }
        vertexBuffers.push_back(vb);

        rhi::BufferDesc ibDesc;
        ibDesc.size = static_cast<u32>(mesh.indices.size() * sizeof(u32));
        ibDesc.usage = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDst;
        ibDesc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        ibDesc.debugName = mesh.name.c_str();
        auto ib = device->CreateBuffer(ibDesc);
        if (!mesh.indices.empty()) {
            UploadBufferViaStaging(device, ib, mesh.indices.data(),
                mesh.indices.size() * sizeof(u32));
        }
        indexBuffers.push_back(ib);
    }

    // Create entity hierarchy
    std::vector<ecs::Entity> entities(result.nodes.size());

    for (u32 i = 0; i < static_cast<u32>(result.nodes.size()); ++i) {
        const auto& node = result.nodes[i];
        entities[i] = world.CreateEntity();

        // Attach Transform component with TRS from glTF node
        scene::Transform transform;
        transform.SetPositionRotation(node.translation, {0, 1, 0}, 0);
        // Apply scale (simplified: bake into motor)
        if (node.scale.x != 1.0f || node.scale.y != 1.0f || node.scale.z != 1.0f) {
            // TODO: Full TRS composition with PGA scale support
        }
        if (node.parentIndex >= 0) {
            transform.parent = entities[node.parentIndex];
        }
        transform.dirty = true;
        world.AddComponent(entities[i], transform);

        // Attach MeshRenderer component
        if (node.meshIndex >= 0 && node.meshIndex < static_cast<i32>(result.meshes.size())) {
            scene::MeshRenderer mr;
            mr.meshId = renderer::INVALID_MESH_ID; // Will be set when registered with MeshRegistry
            mr.submeshIndex = 0;
            if (!materialIds.empty()) {
                u32 matIdx = result.meshes[node.meshIndex].primitives.empty()
                    ? 0 : result.meshes[node.meshIndex].primitives[0].materialIndex;
                if (matIdx < materialIds.size()) {
                    mr.materialId = materialIds[matIdx];
                }
            }
            mr.visible = true;
            mr.castShadow = true;
            mr.receiveShadow = true;
            world.AddComponent(entities[i], mr);
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

void GLTFImporter::ParseMeshes(const void* gltfData, GLTFImportResult& result,
                                 const GLTFImportOptions& options) {
#ifdef NGE_HAS_CGLTF
    const cgltf_data* data = static_cast<const cgltf_data*>(gltfData);

    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
        const cgltf_mesh& mesh = data->meshes[mi];
        GLTFMeshData meshData;
        meshData.name = mesh.name ? mesh.name : ("mesh_" + std::to_string(mi));

        for (cgltf_size pi = 0; pi < mesh.primitives_count; ++pi) {
            const cgltf_primitive& prim = mesh.primitives[pi];
            u32 primIndexOffset = static_cast<u32>(meshData.indices.size());
            u32 primVertexOffset = static_cast<u32>(meshData.positions.size());

            // Extract indices
            if (prim.indices) {
                const cgltf_accessor* accessor = prim.indices;
                cgltf_size indexCount = accessor->count;
                meshData.indices.reserve(meshData.indices.size() + indexCount);

                const cgltf_buffer_view* bv = accessor->buffer_view;
                const u8* bufferData = static_cast<const u8*>(bv->buffer->data) + bv->offset + accessor->offset;

                for (cgltf_size i = 0; i < indexCount; ++i) {
                    u32 idx = 0;
                    switch (accessor->component_type) {
                        case cgltf_component_type_r_8u:    idx = static_cast<const u8*>(static_cast<const void*>(bufferData))[i]; break;
                        case cgltf_component_type_r_16u:   idx = static_cast<const u16*>(static_cast<const void*>(bufferData))[i]; break;
                        case cgltf_component_type_r_32u:   idx = static_cast<const u32*>(static_cast<const void*>(bufferData))[i]; break;
                        default: break;
                    }
                    meshData.indices.push_back(primVertexOffset + idx);
                }
            } else if (prim.attributes_count > 0) {
                // Non-indexed: generate sequential indices
                cgltf_size vertCount = prim.attributes[0].data->count;
                for (cgltf_size i = 0; i < vertCount; ++i) {
                    meshData.indices.push_back(primVertexOffset + static_cast<u32>(i));
                }
            }

            // Extract vertex attributes
            cgltf_size vertCount = 0;
            for (cgltf_size ai = 0; ai < prim.attributes_count; ++ai) {
                const cgltf_attribute& attr = prim.attributes[ai];
                const cgltf_accessor* accessor = attr.data;
                vertCount = accessor->count;

                const cgltf_buffer_view* bv = accessor->buffer_view;
                const u8* basePtr = static_cast<const u8*>(bv->buffer->data) + bv->offset + accessor->offset;
                cgltf_size stride = accessor->stride;

                switch (attr.type) {
                    case cgltf_attribute_type_position: {
                        meshData.positions.resize(primVertexOffset + vertCount);
                        for (cgltf_size i = 0; i < vertCount; ++i) {
                            const f32* p = reinterpret_cast<const f32*>(basePtr + i * stride);
                            meshData.positions[primVertexOffset + i] = {p[0], p[1], p[2]};
                        }
                        break;
                    }
                    case cgltf_attribute_type_normal: {
                        meshData.normals.resize(primVertexOffset + vertCount);
                        for (cgltf_size i = 0; i < vertCount; ++i) {
                            const f32* n = reinterpret_cast<const f32*>(basePtr + i * stride);
                            meshData.normals[primVertexOffset + i] = {n[0], n[1], n[2]};
                        }
                        break;
                    }
                    case cgltf_attribute_type_tangent: {
                        meshData.tangents.resize(primVertexOffset + vertCount);
                        for (cgltf_size i = 0; i < vertCount; ++i) {
                            const f32* t = reinterpret_cast<const f32*>(basePtr + i * stride);
                            meshData.tangents[primVertexOffset + i] = {t[0], t[1], t[2], t[3]};
                        }
                        break;
                    }
                    case cgltf_attribute_type_texcoord: {
                        if (attr.index == 0) {
                            meshData.texcoords0.resize(primVertexOffset + vertCount);
                            for (cgltf_size i = 0; i < vertCount; ++i) {
                                const f32* uv = reinterpret_cast<const f32*>(basePtr + i * stride);
                                meshData.texcoords0[primVertexOffset + i] = {uv[0], uv[1]};
                            }
                        } else if (attr.index == 1) {
                            meshData.texcoords1.resize(primVertexOffset + vertCount);
                            for (cgltf_size i = 0; i < vertCount; ++i) {
                                const f32* uv = reinterpret_cast<const f32*>(basePtr + i * stride);
                                meshData.texcoords1[primVertexOffset + i] = {uv[0], uv[1]};
                            }
                        }
                        break;
                    }
                    case cgltf_attribute_type_color: {
                        meshData.colors.resize(primVertexOffset + vertCount);
                        for (cgltf_size i = 0; i < vertCount; ++i) {
                            const f32* c = reinterpret_cast<const f32*>(basePtr + i * stride);
                            cgltf_size comps = cgltf_num_components(accessor->type);
                            meshData.colors[primVertexOffset + i] = {
                                c[0], comps > 1 ? c[1] : 1.0f,
                                comps > 2 ? c[2] : 1.0f, comps > 3 ? c[3] : 1.0f
                            };
                        }
                        break;
                    }
                    case cgltf_attribute_type_joints: {
                        meshData.joints.resize(primVertexOffset + vertCount);
                        for (cgltf_size i = 0; i < vertCount; ++i) {
                            const f32* j = reinterpret_cast<const f32*>(basePtr + i * stride);
                            meshData.joints[primVertexOffset + i] = {j[0], j[1], j[2], j[3]};
                        }
                        break;
                    }
                    case cgltf_attribute_type_weights: {
                        meshData.weights.resize(primVertexOffset + vertCount);
                        for (cgltf_size i = 0; i < vertCount; ++i) {
                            const f32* w = reinterpret_cast<const f32*>(basePtr + i * stride);
                            meshData.weights[primVertexOffset + i] = {w[0], w[1], w[2], w[3]};
                        }
                        break;
                    }
                    default: break;
                }
            }

            // Ensure positions exist (required)
            if (meshData.positions.size() < primVertexOffset + vertCount) {
                meshData.positions.resize(primVertexOffset + vertCount, {0, 0, 0});
            }
            // Fill missing normals with defaults
            if (meshData.normals.size() < primVertexOffset + vertCount) {
                meshData.normals.resize(primVertexOffset + vertCount, {0, 1, 0});
            }
            // Fill missing texcoords with defaults
            if (meshData.texcoords0.size() < primVertexOffset + vertCount) {
                meshData.texcoords0.resize(primVertexOffset + vertCount, {0, 0});
            }

            // Record primitive
            GLTFMeshData::Primitive primData;
            primData.indexOffset = primIndexOffset;
            primData.indexCount = static_cast<u32>(meshData.indices.size() - primIndexOffset);
            primData.materialIndex = prim.material ? static_cast<u32>(prim.material - data->materials) : 0;
            meshData.primitives.push_back(primData);
        }

        // Compute tangents if missing and requested
        if (options.generateTangents && meshData.tangents.empty() &&
            !meshData.positions.empty() && !meshData.normals.empty() && !meshData.texcoords0.empty()) {
            ComputeTangents(meshData);
        }

        ComputeBounds(meshData);
        result.meshes.push_back(std::move(meshData));
    }
#else
    (void)gltfData; (void)result; (void)options;
#endif
}

void GLTFImporter::ParseMaterials(const void* gltfData, GLTFImportResult& result) {
#ifdef NGE_HAS_CGLTF
    const cgltf_data* data = static_cast<const cgltf_data*>(gltfData);

    for (cgltf_size mi = 0; mi < data->materials_count; ++mi) {
        const cgltf_material& mat = data->materials[mi];
        GLTFMaterialData matData;
        matData.name = mat.name ? mat.name : ("material_" + std::to_string(mi));

        if (mat.has_pbr_metallic_roughness) {
            const auto& pbr = mat.pbr_metallic_roughness;
            matData.baseColorFactor = {
                pbr.base_color_factor[0], pbr.base_color_factor[1],
                pbr.base_color_factor[2], pbr.base_color_factor[3]
            };
            matData.metallicFactor = pbr.metallic_factor;
            matData.roughnessFactor = pbr.roughness_factor;

            if (pbr.base_color_texture.texture) {
                matData.albedoTexture = static_cast<i32>(
                    pbr.base_color_texture.texture - data->textures);
            }
            if (pbr.metallic_roughness_texture.texture) {
                matData.metallicRoughnessTexture = static_cast<i32>(
                    pbr.metallic_roughness_texture.texture - data->textures);
            }
        }

        if (mat.normal_texture.texture) {
            matData.normalTexture = static_cast<i32>(
                mat.normal_texture.texture - data->textures);
        }
        if (mat.emissive_texture.texture) {
            matData.emissiveTexture = static_cast<i32>(
                mat.emissive_texture.texture - data->textures);
        }
        if (mat.occlusion_texture.texture) {
            matData.occlusionTexture = static_cast<i32>(
                mat.occlusion_texture.texture - data->textures);
        }

        matData.emissiveFactor = {
            mat.emissive_factor[0], mat.emissive_factor[1], mat.emissive_factor[2]
        };
        matData.emissiveStrength = (mat.emissive_factor[0] > 0 || mat.emissive_factor[1] > 0 || mat.emissive_factor[2] > 0) ? 1.0f : 0.0f;

        matData.doubleSided = (mat.double_sided != 0);
        matData.alphaCutoff = mat.alpha_cutoff;

        if (mat.alpha_mode == cgltf_alpha_mode_blend) {
            matData.alphaBlend = true;
        } else if (mat.alpha_mode == cgltf_alpha_mode_mask) {
            matData.alphaTest = true;
        }

        result.materials.push_back(matData);
    }
#else
    (void)gltfData; (void)result;
#endif
}

void GLTFImporter::ParseTextures(const void* gltfData, GLTFImportResult& result,
                                   const std::string& basePath) {
#ifdef NGE_HAS_CGLTF
    const cgltf_data* data = static_cast<const cgltf_data*>(gltfData);

    for (cgltf_size ti = 0; ti < data->textures_count; ++ti) {
        const cgltf_texture& tex = data->textures[ti];
        GLTFTextureData texData;

        if (tex.image) {
            const cgltf_image& img = *tex.image;
            texData.name = img.name ? img.name : ("texture_" + std::to_string(ti));

            if (img.uri) {
                texData.uri = img.uri;
            }

#ifdef NGE_HAS_STB
            // Load image from file or embedded data
            int width = 0, height = 0, channels = 0;
            stbi_uc* pixels = nullptr;

            if (img.buffer_view && img.buffer_view->buffer && img.buffer_view->buffer->data) {
                // Embedded image via buffer view
                const auto* bv = img.buffer_view;
                const stbi_uc* embeddedData = static_cast<const stbi_uc*>(bv->buffer->data) + bv->offset;
                pixels = stbi_load_from_memory(embeddedData, static_cast<int>(bv->size),
                    &width, &height, &channels, 4);
            } else if (img.uri) {
                // External file
                std::string fullPath = basePath + "/" + img.uri;
                pixels = stbi_load(fullPath.c_str(), &width, &height, &channels, 4);
            }

            if (pixels) {
                texData.width = static_cast<u32>(width);
                texData.height = static_cast<u32>(height);
                texData.channels = 4;
                texData.pixels.resize(static_cast<usize>(width) * height * 4);
                std::memcpy(texData.pixels.data(), pixels, texData.pixels.size());
                stbi_image_free(pixels);
            } else {
                NGE_LOG_WARN("Failed to load texture '{}'", texData.name);
            }
#endif
        }

        result.textures.push_back(std::move(texData));
    }
#else
    (void)gltfData; (void)result; (void)basePath;
#endif
}

void GLTFImporter::ParseNodes(const void* gltfData, GLTFImportResult& result) {
#ifdef NGE_HAS_CGLTF
    const cgltf_data* data = static_cast<const cgltf_data*>(gltfData);

    // Build node list
    for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node& node = data->nodes[ni];
        GLTFNodeData nodeData;
        nodeData.name = node.name ? node.name : ("node_" + std::to_string(ni));

        // Determine parent index
        if (node.parent) {
            nodeData.parentIndex = static_cast<i32>(node.parent - data->nodes);
        }

        // Extract transform (TRS or matrix)
        if (node.has_translation) {
            nodeData.translation = {node.translation[0], node.translation[1], node.translation[2]};
        }
        if (node.has_rotation) {
            nodeData.rotation = {node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]};
        }
        if (node.has_scale) {
            nodeData.scale = {node.scale[0], node.scale[1], node.scale[2]};
        }
        if (node.has_matrix) {
            // Decompose matrix to TRS (simplified: extract translation)
            nodeData.translation = {node.matrix[12], node.matrix[13], node.matrix[14]};
            // TODO: Full matrix decomposition if needed
        }

        // Mesh/camera/light indices
        if (node.mesh) {
            nodeData.meshIndex = static_cast<i32>(node.mesh - data->meshes);
        }
        if (node.camera) {
            nodeData.cameraIndex = static_cast<i32>(node.camera - data->cameras);
        }
        if (node.light) {
            nodeData.lightIndex = static_cast<i32>(node.light - data->lights);
        }

        // Children
        for (cgltf_size ci = 0; ci < node.children_count; ++ci) {
            nodeData.children.push_back(static_cast<u32>(node.children[ci] - data->nodes));
        }

        result.nodes.push_back(nodeData);
    }

    // Determine root nodes from the default scene
    if (data->scene && data->scene->nodes_count > 0) {
        for (cgltf_size ri = 0; ri < data->scene->nodes_count; ++ri) {
            result.rootNodes.push_back(static_cast<u32>(data->scene->nodes[ri] - data->nodes));
        }
    } else if (data->scenes_count > 0 && data->scenes[0].nodes_count > 0) {
        for (cgltf_size ri = 0; ri < data->scenes[0].nodes_count; ++ri) {
            result.rootNodes.push_back(static_cast<u32>(data->scenes[0].nodes[ri] - data->nodes));
        }
    } else if (data->nodes_count > 0) {
        // No scene — all nodes without parents are roots
        for (cgltf_size ni = 0; ni < data->nodes_count; ++ni) {
            if (!data->nodes[ni].parent) {
                result.rootNodes.push_back(static_cast<u32>(ni));
            }
        }
    }
#else
    (void)gltfData; (void)result;
#endif
}

void GLTFImporter::ParseAnimations(const void* gltfData, GLTFImportResult& result) {
#ifdef NGE_HAS_CGLTF
    const cgltf_data* data = static_cast<const cgltf_data*>(gltfData);

    for (cgltf_size ai = 0; ai < data->animations_count; ++ai) {
        const cgltf_animation& anim = data->animations[ai];
        GLTFAnimationData animData;
        animData.name = anim.name ? anim.name : ("anim_" + std::to_string(ai));

        for (cgltf_size ci = 0; ci < anim.channels_count; ++ci) {
            const cgltf_animation_channel& ch = anim.channels[ci];
            GLTFAnimationChannel channel;
            channel.nodeIndex = ch.target_node ? static_cast<u32>(ch.target_node - data->nodes) : 0;

            switch (ch.target_path) {
                case cgltf_animation_path_type_translation: channel.path = GLTFAnimationChannel::Path::Translation; break;
                case cgltf_animation_path_type_rotation:    channel.path = GLTFAnimationChannel::Path::Rotation; break;
                case cgltf_animation_path_type_scale:       channel.path = GLTFAnimationChannel::Path::Scale; break;
                case cgltf_animation_path_type_weights:     channel.path = GLTFAnimationChannel::Path::Weights; break;
                default: continue;
            }

            switch (ch.sampler->interpolation) {
                case cgltf_interpolation_type_linear:       channel.interpolation = GLTFAnimationChannel::Interpolation::Linear; break;
                case cgltf_interpolation_type_step:         channel.interpolation = GLTFAnimationChannel::Interpolation::Step; break;
                case cgltf_interpolation_type_cubic_spline: channel.interpolation = GLTFAnimationChannel::Interpolation::CubicSpline; break;
                default: channel.interpolation = GLTFAnimationChannel::Interpolation::Linear; break;
            }

            // Extract keyframe timestamps from the input accessor
            const cgltf_accessor* inputAcc = ch.sampler->input;
            if (inputAcc && inputAcc->buffer_view && inputAcc->buffer_view->buffer) {
                const auto* bv = inputAcc->buffer_view;
                const u8* basePtr = static_cast<const u8*>(bv->buffer->data) + bv->offset + inputAcc->offset;
                cgltf_size stride = inputAcc->stride;
                channel.timestamps.resize(inputAcc->count);
                for (cgltf_size i = 0; i < inputAcc->count; ++i) {
                    channel.timestamps[i] = *reinterpret_cast<const f32*>(basePtr + i * stride);
                }
            }

            // Extract keyframe values from the output accessor
            const cgltf_accessor* outputAcc = ch.sampler->output;
            if (outputAcc && outputAcc->buffer_view && outputAcc->buffer_view->buffer) {
                const auto* bv = outputAcc->buffer_view;
                const u8* basePtr = static_cast<const u8*>(bv->buffer->data) + bv->offset + outputAcc->offset;
                cgltf_size stride = outputAcc->stride;
                cgltf_size comps = cgltf_num_components(outputAcc->type);
                channel.values.resize(outputAcc->count * comps);
                for (cgltf_size i = 0; i < outputAcc->count; ++i) {
                    const f32* v = reinterpret_cast<const f32*>(basePtr + i * stride);
                    for (cgltf_size c = 0; c < comps; ++c) {
                        channel.values[i * comps + c] = v[c];
                    }
                }
            }

            // Track animation duration
            if (!channel.timestamps.empty()) {
                animData.duration = std::max(animData.duration, channel.timestamps.back());
            }

            animData.channels.push_back(std::move(channel));
        }

        result.animations.push_back(std::move(animData));
    }
#else
    (void)gltfData; (void)result;
#endif
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
