#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <memory>

namespace nge::renderer {

// ─── Terrain System ──────────────────────────────────────────────────────
// CDLOD (Continuous Distance-Dependent Level of Detail) terrain with:
//   - GPU-driven LOD selection and morphing
//   - Virtual texturing for material splatting
//   - Heightfield stored as R16 or R32F texture
//   - Clipmap-based rendering for infinite terrain illusion
//   - Tessellation or mesh shader patches

// ─── Terrain Configuration ───────────────────────────────────────────────

struct TerrainConfig {
    u32         heightmapRes    = 4096;  // Heightmap resolution (square)
    f32         worldSize       = 4096.0f; // World-space extent (meters)
    f32         heightScale     = 512.0f;  // Max terrain height
    u32         patchSize       = 64;    // Vertices per patch edge
    u32         lodLevels       = 8;     // Number of LOD levels
    f32         lodDistanceScale = 2.0f; // Distance multiplier per LOD
    u32         clipmapLevels   = 6;     // Geometry clipmap levels
    u32         virtualTexRes   = 16384; // Virtual texture resolution
    u32         virtualPageSize = 256;   // Virtual texture page size
    f32         morphRange      = 0.3f;  // LOD morph blend range (0-1)
    bool        enableTessellation = false;
    f32         tessellationFactor = 16.0f;
};

// ─── Terrain Layer (material splat) ──────────────────────────────────────

struct TerrainLayer {
    std::string    name;
    rhi::TextureHandle albedoTex;
    rhi::TextureHandle normalTex;
    rhi::TextureHandle armTex;      // AO + Roughness + Metallic
    f32            tilingScale = 10.0f;
    f32            slopeMin    = 0.0f;  // Slope-based auto-splatting
    f32            slopeMax    = 1.0f;
    f32            heightMin   = 0.0f;
    f32            heightMax   = 1.0f;

    static constexpr u32 MAX_LAYERS = 16;
};

// ─── GPU Terrain Data ────────────────────────────────────────────────────

struct alignas(16) GPUTerrainConstants {
    math::Vec4 worldOrigin;     // xyz = terrain origin, w = worldSize
    math::Vec4 heightParams;    // x = heightScale, y = texelSize, z = morphRange, w = unused
    math::Vec4 lodParams;       // x = lodDistanceScale, y = patchSize, z = lodLevels, w = unused
    math::Vec4 cameraPos;
    math::Mat4 viewProj;
    u32        clipmapLevel;
    u32        patchCountPerEdge;
    u32        layerCount;
    u32        pad;
};

struct GPUTerrainPatch {
    math::Vec2 worldOffset;   // XZ offset in world space
    f32        scale;         // Patch scale (doubles per LOD level)
    u32        lodLevel;
};

// ─── Terrain Clipmap Level ───────────────────────────────────────────────

struct ClipmapLevel {
    u32                         level;
    f32                         scale;       // World scale for this level
    math::Vec2                  center;      // Snapped center in world XZ
    std::vector<GPUTerrainPatch> patches;
    rhi::BufferHandle           patchBuffer; // GPU buffer of patches
    u32                         patchCount;
};

// ─── Terrain System ──────────────────────────────────────────────────────

class TerrainSystem {
public:
    bool Init(rhi::IDevice* device, const TerrainConfig& config = {});
    void Shutdown();

    // Load heightmap from file (R16 or R32F raw, or PNG)
    bool LoadHeightmap(const std::string& path);

    // Generate flat heightmap for testing
    void GenerateFlat(f32 height = 0.0f);

    // Generate procedural heightmap (fractal noise)
    void GenerateProcedural(u32 seed = 42, f32 roughness = 0.6f, u32 octaves = 8);

    // Add a material layer
    u32 AddLayer(const TerrainLayer& layer);

    // Per-frame update: rebuild clipmap patches around camera
    void Update(const math::Vec3& cameraPos, const math::Mat4& viewProj);

    // Render terrain
    void Render(rhi::ICommandList* cmd, const math::Mat4& viewProj,
                const math::Vec3& cameraPos);

    // Query height at world XZ position
    f32 GetHeightAt(f32 worldX, f32 worldZ) const;

    // Query normal at world XZ position
    math::Vec3 GetNormalAt(f32 worldX, f32 worldZ) const;

    // GPU resources
    rhi::TextureHandle GetHeightmapTexture() const { return m_heightmapTex; }
    rhi::TextureHandle GetSplatmapTexture() const { return m_splatmapTex; }
    rhi::BufferHandle  GetConstantsBuffer() const { return m_constantsBuffer; }

    const TerrainConfig& GetConfig() const { return m_config; }

    // Stats
    u32 GetVisiblePatchCount() const { return m_visiblePatchCount; }
    u32 GetTriangleCount() const { return m_triangleCount; }

private:
    void CreateResources();
    void DestroyResources();
    void RebuildClipmap(const math::Vec3& cameraPos);
    void UploadConstants(const math::Vec3& cameraPos, const math::Mat4& viewProj);

    // Snap position to texel grid at given LOD level
    math::Vec2 SnapToGrid(const math::Vec2& pos, f32 gridSize) const;

    rhi::IDevice*   m_device = nullptr;
    TerrainConfig   m_config;

    // Heightmap data (CPU)
    std::vector<f32> m_heightData;

    // GPU resources
    rhi::TextureHandle m_heightmapTex;   // R32F or R16
    rhi::TextureHandle m_splatmapTex;    // RGBA8 weight map per 4 layers
    rhi::BufferHandle  m_constantsBuffer;
    rhi::BufferHandle  m_indexBuffer;     // Shared patch index buffer
    rhi::BufferHandle  m_vertexBuffer;    // Shared patch vertex buffer (grid positions)

    // Clipmap
    std::vector<ClipmapLevel> m_clipmapLevels;

    // Layers
    std::vector<TerrainLayer> m_layers;

    // Pipelines
    rhi::PipelineHandle m_terrainPipeline;
    rhi::PipelineHandle m_terrainWirePipeline; // Debug wireframe

    // Stats
    u32 m_visiblePatchCount = 0;
    u32 m_triangleCount = 0;
};

} // namespace nge::renderer
