#include "engine/renderer/terrain/terrain_system.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"
#include <cmath>
#include <algorithm>
#include <random>

namespace nge::renderer {

bool TerrainSystem::Init(rhi::IDevice* device, const TerrainConfig& config) {
    m_device = device;
    m_config = config;

    m_heightData.resize(config.heightmapRes * config.heightmapRes, 0.0f);

    CreateResources();

    NGE_LOG_INFO("Terrain system initialized: {}m world, {}px heightmap, {} LOD levels, {} clipmap levels",
                 config.worldSize, config.heightmapRes, config.lodLevels, config.clipmapLevels);
    return true;
}

void TerrainSystem::Shutdown() {
    DestroyResources();
    m_heightData.clear();
    m_layers.clear();
    m_clipmapLevels.clear();
}

void TerrainSystem::CreateResources() {
    // Heightmap texture (R32F)
    {
        rhi::TextureDesc desc;
        desc.width = m_config.heightmapRes;
        desc.height = m_config.heightmapRes;
        desc.format = rhi::Format::R32_FLOAT;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage;
        desc.debugName = "Terrain_Heightmap";
        m_heightmapTex = m_device->CreateTexture(desc);
    }

    // Splatmap (RGBA8 — 4 layer weights per texel)
    {
        rhi::TextureDesc desc;
        desc.width = m_config.heightmapRes;
        desc.height = m_config.heightmapRes;
        desc.format = rhi::Format::RGBA8_UNORM;
        desc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage;
        desc.debugName = "Terrain_Splatmap";
        m_splatmapTex = m_device->CreateTexture(desc);
    }

    // Constants buffer
    {
        rhi::BufferDesc desc;
        desc.size = sizeof(GPUTerrainConstants);
        desc.usage = rhi::BufferUsage::Uniform | rhi::BufferUsage::TransferDst;
        desc.memoryUsage = rhi::MemoryUsage::CPU_To_GPU;
        desc.debugName = "Terrain_Constants";
        m_constantsBuffer = m_device->CreateBuffer(desc);
    }

    // Shared patch vertex buffer (unit grid)
    {
        u32 verts = (m_config.patchSize + 1) * (m_config.patchSize + 1);
        rhi::BufferDesc desc;
        desc.size = verts * sizeof(math::Vec2); // XZ positions in [0,1]
        desc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::TransferDst;
        desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        desc.debugName = "Terrain_PatchVertices";
        m_vertexBuffer = m_device->CreateBuffer(desc);

        // TODO: Upload grid vertex positions
    }

    // Shared patch index buffer
    {
        u32 quads = m_config.patchSize * m_config.patchSize;
        u32 indices = quads * 6; // 2 triangles per quad
        rhi::BufferDesc desc;
        desc.size = indices * sizeof(u32);
        desc.usage = rhi::BufferUsage::Index | rhi::BufferUsage::TransferDst;
        desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        desc.debugName = "Terrain_PatchIndices";
        m_indexBuffer = m_device->CreateBuffer(desc);

        // TODO: Upload grid index data
    }

    // Initialize clipmap levels
    m_clipmapLevels.resize(m_config.clipmapLevels);
    for (u32 i = 0; i < m_config.clipmapLevels; ++i) {
        auto& level = m_clipmapLevels[i];
        level.level = i;
        level.scale = std::pow(2.0f, static_cast<f32>(i));
        level.patchCount = 0;

        rhi::BufferDesc desc;
        desc.size = sizeof(GPUTerrainPatch) * 1024; // Preallocate for max patches
        desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst;
        desc.memoryUsage = rhi::MemoryUsage::CPU_To_GPU;
        desc.debugName = "Terrain_ClipmapPatches";
        level.patchBuffer = m_device->CreateBuffer(desc);
    }
}

void TerrainSystem::DestroyResources() {
    if (!m_device) return;

    auto destroyTex = [&](rhi::TextureHandle& h) {
        if (h.IsValid()) { m_device->DestroyTexture(h); h = {}; }
    };
    auto destroyBuf = [&](rhi::BufferHandle& h) {
        if (h.IsValid()) { m_device->DestroyBuffer(h); h = {}; }
    };

    destroyTex(m_heightmapTex);
    destroyTex(m_splatmapTex);
    destroyBuf(m_constantsBuffer);
    destroyBuf(m_vertexBuffer);
    destroyBuf(m_indexBuffer);

    for (auto& level : m_clipmapLevels) {
        destroyBuf(level.patchBuffer);
    }
}

bool TerrainSystem::LoadHeightmap(const std::string& path) {
    NGE_LOG_INFO("Loading heightmap: {}", path);
    // TODO: Load R16/R32F raw file or PNG via stb_image
    // For now, generate flat
    GenerateFlat(0.0f);
    return true;
}

void TerrainSystem::GenerateFlat(f32 height) {
    f32 normalized = height / m_config.heightScale;
    std::fill(m_heightData.begin(), m_heightData.end(), normalized);
    // TODO: Upload to GPU texture
}

void TerrainSystem::GenerateProcedural(u32 seed, f32 roughness, u32 octaves) {
    u32 res = m_config.heightmapRes;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<f32> dist(-1.0f, 1.0f);

    // Diamond-square-like fractal noise
    for (u32 y = 0; y < res; ++y) {
        for (u32 x = 0; x < res; ++x) {
            f32 height = 0;
            f32 amplitude = 1.0f;
            f32 frequency = 1.0f / static_cast<f32>(res);

            for (u32 oct = 0; oct < octaves; ++oct) {
                // Simple value noise approximation
                f32 fx = static_cast<f32>(x) * frequency;
                f32 fy = static_cast<f32>(y) * frequency;

                // Hash-based pseudo-noise
                u32 ix = static_cast<u32>(fx) & 0xFF;
                u32 iy = static_cast<u32>(fy) & 0xFF;
                f32 tx = fx - std::floor(fx);
                f32 ty = fy - std::floor(fy);

                // Smoothstep interpolation
                tx = tx * tx * (3.0f - 2.0f * tx);
                ty = ty * ty * (3.0f - 2.0f * ty);

                // Simple hash
                auto hash = [&](u32 a, u32 b) -> f32 {
                    u32 h = (a * 374761393u + b * 668265263u + seed) ^ (oct * 1013904223u);
                    h = (h ^ (h >> 13)) * 1274126177u;
                    return static_cast<f32>(h & 0xFFFF) / 32768.0f - 1.0f;
                };

                f32 v00 = hash(ix, iy);
                f32 v10 = hash(ix + 1, iy);
                f32 v01 = hash(ix, iy + 1);
                f32 v11 = hash(ix + 1, iy + 1);

                f32 v0 = v00 + (v10 - v00) * tx;
                f32 v1 = v01 + (v11 - v01) * tx;
                f32 noise = v0 + (v1 - v0) * ty;

                height += noise * amplitude;
                amplitude *= roughness;
                frequency *= 2.0f;
            }

            // Normalize to [0, 1]
            height = height * 0.5f + 0.5f;
            height = math::Clamp(height, 0.0f, 1.0f);
            m_heightData[y * res + x] = height;
        }
    }

    NGE_LOG_INFO("Generated procedural heightmap: seed={}, roughness={:.2f}, octaves={}",
                 seed, roughness, octaves);

    // TODO: Upload to GPU texture
}

u32 TerrainSystem::AddLayer(const TerrainLayer& layer) {
    if (m_layers.size() >= TerrainLayer::MAX_LAYERS) {
        NGE_LOG_WARN("Terrain: max layers ({}) reached", TerrainLayer::MAX_LAYERS);
        return UINT32_MAX;
    }
    u32 id = static_cast<u32>(m_layers.size());
    m_layers.push_back(layer);
    NGE_LOG_INFO("Added terrain layer {}: '{}'", id, layer.name);
    return id;
}

void TerrainSystem::Update(const math::Vec3& cameraPos, const math::Mat4& viewProj) {
    RebuildClipmap(cameraPos);
    UploadConstants(cameraPos, viewProj);
}

void TerrainSystem::RebuildClipmap(const math::Vec3& cameraPos) {
    m_visiblePatchCount = 0;
    m_triangleCount = 0;

    math::Vec2 camXZ = {cameraPos.x, cameraPos.z};

    for (auto& level : m_clipmapLevels) {
        level.patches.clear();

        f32 patchWorldSize = m_config.worldSize / static_cast<f32>(m_config.patchSize) * level.scale;
        math::Vec2 snapped = SnapToGrid(camXZ, patchWorldSize);

        // Generate a ring of patches around the camera
        // Inner levels are denser, outer levels are sparser
        u32 ringSize = (level.level == 0) ? 8 : 4; // More patches at finest level

        for (i32 py = -static_cast<i32>(ringSize); py <= static_cast<i32>(ringSize); ++py) {
            for (i32 px = -static_cast<i32>(ringSize); px <= static_cast<i32>(ringSize); ++px) {
                // Skip inner region (covered by finer level) except for level 0
                if (level.level > 0) {
                    i32 innerSize = static_cast<i32>(ringSize) / 2;
                    if (std::abs(px) <= innerSize && std::abs(py) <= innerSize) continue;
                }

                GPUTerrainPatch patch;
                patch.worldOffset = {
                    snapped.x + static_cast<f32>(px) * patchWorldSize,
                    snapped.y + static_cast<f32>(py) * patchWorldSize
                };
                patch.scale = level.scale;
                patch.lodLevel = level.level;

                // Bounds check: skip patches outside terrain
                f32 halfWorld = m_config.worldSize * 0.5f;
                if (patch.worldOffset.x < -halfWorld || patch.worldOffset.x > halfWorld) continue;
                if (patch.worldOffset.y < -halfWorld || patch.worldOffset.y > halfWorld) continue;

                level.patches.push_back(patch);
            }
        }

        level.patchCount = static_cast<u32>(level.patches.size());
        m_visiblePatchCount += level.patchCount;
        m_triangleCount += level.patchCount * m_config.patchSize * m_config.patchSize * 2;

        // TODO: Upload patches to GPU buffer
    }
}

void TerrainSystem::UploadConstants(const math::Vec3& cameraPos, const math::Mat4& viewProj) {
    GPUTerrainConstants constants;
    constants.worldOrigin = {0, 0, 0, m_config.worldSize};
    constants.heightParams = {
        m_config.heightScale,
        1.0f / static_cast<f32>(m_config.heightmapRes),
        m_config.morphRange,
        0
    };
    constants.lodParams = {
        m_config.lodDistanceScale,
        static_cast<f32>(m_config.patchSize),
        static_cast<f32>(m_config.lodLevels),
        0
    };
    constants.cameraPos = {cameraPos.x, cameraPos.y, cameraPos.z, 0};
    constants.viewProj = viewProj;
    constants.clipmapLevel = 0;
    constants.patchCountPerEdge = m_config.patchSize;
    constants.layerCount = static_cast<u32>(m_layers.size());

    // TODO: Map and upload to m_constantsBuffer
}

void TerrainSystem::Render(rhi::ICommandList* cmd, const math::Mat4& /*viewProj*/,
                            const math::Vec3& /*cameraPos*/) {
    if (m_visiblePatchCount == 0) return;

    cmd->BeginDebugLabel("Terrain", 0.3f, 0.7f, 0.3f);

    if (m_terrainPipeline.IsValid()) {
        cmd->BindGraphicsPipeline(m_terrainPipeline);
        // Bind heightmap, splatmap, layer textures, constants
        // For each clipmap level, bind patch buffer and draw instanced
        for (const auto& level : m_clipmapLevels) {
            if (level.patchCount == 0) continue;
            // Push constants: clipmap level
            // cmd->DrawIndexedInstanced(indicesPerPatch, level.patchCount, 0, 0, 0);
        }
    }

    cmd->EndDebugLabel();
}

f32 TerrainSystem::GetHeightAt(f32 worldX, f32 worldZ) const {
    // Convert world XZ to heightmap UV
    f32 halfWorld = m_config.worldSize * 0.5f;
    f32 u = (worldX + halfWorld) / m_config.worldSize;
    f32 v = (worldZ + halfWorld) / m_config.worldSize;

    if (u < 0 || u > 1 || v < 0 || v > 1) return 0;

    // Bilinear sample
    f32 fx = u * static_cast<f32>(m_config.heightmapRes - 1);
    f32 fy = v * static_cast<f32>(m_config.heightmapRes - 1);
    u32 ix = static_cast<u32>(fx);
    u32 iy = static_cast<u32>(fy);
    f32 tx = fx - static_cast<f32>(ix);
    f32 ty = fy - static_cast<f32>(iy);

    u32 res = m_config.heightmapRes;
    ix = math::Min(ix, res - 2);
    iy = math::Min(iy, res - 2);

    f32 h00 = m_heightData[iy * res + ix];
    f32 h10 = m_heightData[iy * res + ix + 1];
    f32 h01 = m_heightData[(iy + 1) * res + ix];
    f32 h11 = m_heightData[(iy + 1) * res + ix + 1];

    f32 h0 = h00 + (h10 - h00) * tx;
    f32 h1 = h01 + (h11 - h01) * tx;
    f32 h = h0 + (h1 - h0) * ty;

    return h * m_config.heightScale;
}

math::Vec3 TerrainSystem::GetNormalAt(f32 worldX, f32 worldZ) const {
    f32 texelWorld = m_config.worldSize / static_cast<f32>(m_config.heightmapRes);

    f32 hL = GetHeightAt(worldX - texelWorld, worldZ);
    f32 hR = GetHeightAt(worldX + texelWorld, worldZ);
    f32 hD = GetHeightAt(worldX, worldZ - texelWorld);
    f32 hU = GetHeightAt(worldX, worldZ + texelWorld);

    math::Vec3 normal = {hL - hR, 2.0f * texelWorld, hD - hU};
    f32 len = normal.Length();
    if (len > 0.0001f) {
        normal = normal * (1.0f / len);
    } else {
        normal = {0, 1, 0};
    }
    return normal;
}

math::Vec2 TerrainSystem::SnapToGrid(const math::Vec2& pos, f32 gridSize) const {
    return {
        std::floor(pos.x / gridSize) * gridSize,
        std::floor(pos.y / gridSize) * gridSize
    };
}

} // namespace nge::renderer
