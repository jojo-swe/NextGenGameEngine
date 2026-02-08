#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/rhi/common/rhi_types.h"
#include <vector>

namespace nge::renderer {

// ─── Light Types ─────────────────────────────────────────────────────────

enum class LightType : u32 {
    Point       = 0,
    Spot        = 1,
    Directional = 2,
    Area        = 3, // Rectangular or disk area light
    Emissive    = 4, // Emissive triangle (mesh light)
};

// ─── GPU Light Data (64 bytes, cache-aligned) ────────────────────────────
struct alignas(16) GPULightData {
    math::Vec3 position;
    f32        radius;           // Influence radius (point/spot), or area size
    math::Vec3 color;            // Color × intensity
    f32        pad0;
    math::Vec3 direction;        // Spot/directional
    f32        cosInnerAngle;    // Spot inner cone cos
    f32        cosOuterAngle;    // Spot outer cone cos
    LightType  type;
    f32        area;             // Area light surface area
    u32        shadowMapIndex;   // Index into shadow map array (UINT32_MAX = no shadow)
};

static_assert(sizeof(GPULightData) == 64, "GPULightData must be 64 bytes");

// ─── CPU-side Light ──────────────────────────────────────────────────────

struct Light {
    LightType   type       = LightType::Point;
    math::Vec3  position   = {0, 0, 0};
    math::Vec3  direction  = {0, -1, 0};
    math::Vec3  color      = {1, 1, 1};
    f32         intensity  = 1.0f;
    f32         radius     = 10.0f;      // Attenuation radius
    f32         innerAngle = 0.3f;       // Spot inner cone (radians)
    f32         outerAngle = 0.5f;       // Spot outer cone (radians)
    f32         areaWidth  = 1.0f;       // Area light dimensions
    f32         areaHeight = 1.0f;
    bool        castShadow = true;
    bool        isActive   = true;

    GPULightData ToGPU() const {
        GPULightData gpu{};
        gpu.position       = position;
        gpu.radius         = radius;
        gpu.color          = color * intensity;
        gpu.direction      = direction;
        gpu.cosInnerAngle  = math::Cos(innerAngle);
        gpu.cosOuterAngle  = math::Cos(outerAngle);
        gpu.type           = type;
        gpu.area           = areaWidth * areaHeight;
        gpu.shadowMapIndex = UINT32_MAX;
        return gpu;
    }
};

// ─── Light Manager ───────────────────────────────────────────────────────

class LightManager {
public:
    bool Init(rhi::IDevice* device, u32 maxLights = 65536);
    void Shutdown();

    u32  AddLight(const Light& light);
    void RemoveLight(u32 id);
    Light* GetLight(u32 id);

    // Set the primary directional light (sun)
    void SetSunLight(const Light& sun);
    const Light& GetSunLight() const { return m_sunLight; }

    // Upload active lights to GPU buffer
    void UploadToGPU();

    // GPU access
    rhi::BufferHandle GetLightBuffer() const { return m_gpuBuffer; }
    u32 GetActiveLightCount() const { return m_activeLightCount; }

private:
    rhi::IDevice*      m_device = nullptr;
    std::vector<Light> m_lights;
    Light              m_sunLight;
    rhi::BufferHandle  m_gpuBuffer;
    u32                m_activeLightCount = 0;
    bool               m_dirty = false;
};

} // namespace nge::renderer
