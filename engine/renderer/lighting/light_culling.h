#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>

namespace nge::renderer {

// ─── Light Culling Data Structures ───────────────────────────────────────
// CPU-side light collection + GPU cluster assignment for clustered shading.
//
// Pipeline:
//   1. CPU: Collect active lights from ECS → upload to GPU structured buffer
//   2. GPU: Assign lights to 3D clusters (16×9×24 default)
//   3. GPU: Shade fragments by reading per-cluster light lists

enum class LightType : u8 {
    Directional = 0,
    Point,
    Spot,
    AreaRect,
    AreaDisk,
};

struct alignas(16) GPULightData {
    math::Vec4 positionAndRange;    // xyz = world position, w = range
    math::Vec4 directionAndAngle;   // xyz = direction, w = cos(outerAngle)
    math::Vec4 colorAndIntensity;   // xyz = color, w = intensity (lux/candela)
    math::Vec4 params;              // x = innerAngle cos, y = type, z = shadow map index, w = flags
};

struct LightInfo {
    LightType  type = LightType::Point;
    math::Vec3 position;
    math::Vec3 direction;
    math::Vec3 color;
    f32        intensity = 1.0f;
    f32        range = 10.0f;
    f32        innerAngle = 0.0f;   // Radians (spot light)
    f32        outerAngle = 0.7854f;// π/4
    f32        areaWidth = 1.0f;    // Area light dimensions
    f32        areaHeight = 1.0f;
    i32        shadowMapIndex = -1; // -1 = no shadow
    bool       castShadow = false;
    bool       enabled = true;
};

struct ClusterGridConfig {
    u32 tilesX = 16;
    u32 tilesY = 9;
    u32 slices = 24;        // Depth slices (logarithmic)
    f32 nearPlane = 0.1f;
    f32 farPlane = 1000.0f;
    u32 maxLightsPerCluster = 256;
};

class LightCullingSystem {
public:
    bool Init(rhi::IDevice* device, const ClusterGridConfig& config = {});
    void Shutdown();

    // Per-frame: collect lights
    void BeginFrame();
    u32 AddLight(const LightInfo& light);
    void SetDirectionalLight(const LightInfo& light);

    // Upload light data to GPU and dispatch cluster assignment
    void Upload(rhi::ICommandList* cmd);
    void AssignClusters(rhi::ICommandList* cmd, rhi::TextureHandle depthBuffer);

    // GPU buffers for shader binding
    rhi::BufferHandle GetLightBuffer() const { return m_lightBuffer; }
    rhi::BufferHandle GetClusterBuffer() const { return m_clusterBuffer; }
    rhi::BufferHandle GetLightGridBuffer() const { return m_lightGridBuffer; }
    rhi::BufferHandle GetLightIndexBuffer() const { return m_lightIndexBuffer; }

    // Query
    u32 GetLightCount() const { return static_cast<u32>(m_lights.size()); }
    u32 GetPointLightCount() const { return m_pointLightCount; }
    u32 GetSpotLightCount() const { return m_spotLightCount; }
    bool HasDirectionalLight() const { return m_hasDirectional; }
    const GPULightData& GetDirectionalLight() const { return m_directionalLight; }

    u32 GetTotalClusters() const;
    const ClusterGridConfig& GetConfig() const { return m_config; }

private:
    GPULightData ConvertLight(const LightInfo& info) const;

    rhi::IDevice* m_device = nullptr;
    ClusterGridConfig m_config;

    // CPU-side light list
    std::vector<GPULightData> m_lights;
    GPULightData m_directionalLight;
    bool m_hasDirectional = false;
    u32 m_pointLightCount = 0;
    u32 m_spotLightCount = 0;

    // GPU buffers
    rhi::BufferHandle m_lightBuffer;       // StructuredBuffer<GPULightData>
    rhi::BufferHandle m_clusterBuffer;     // AABB per cluster
    rhi::BufferHandle m_lightGridBuffer;   // Per-cluster: (offset, count)
    rhi::BufferHandle m_lightIndexBuffer;  // Compacted light index list
    rhi::BufferHandle m_stagingBuffer;

    // Compute pipelines
    rhi::PipelineHandle m_clusterBuildPipeline;
    rhi::PipelineHandle m_lightAssignPipeline;

    static constexpr u32 MAX_LIGHTS = 4096;
};

} // namespace nge::renderer
