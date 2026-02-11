#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/rhi/common/rhi_types.h"
#include "engine/rhi/common/rhi_device.h"
#include <string>
#include <vector>

namespace nge::renderer {

// ─── Material Flags ──────────────────────────────────────────────────────
enum class MaterialFlags : u32 {
    None          = 0,
    DoubleSided   = 1 << 0,
    AlphaBlend    = 1 << 1,
    AlphaTest     = 1 << 2,
    Emissive      = 1 << 3,
    ClearCoat     = 1 << 4,
    Subsurface    = 1 << 5,
    Anisotropic   = 1 << 6,
    Sheen         = 1 << 7,
    ThinFilm      = 1 << 8,
    Hair          = 1 << 9,
    Cloth         = 1 << 10,
    Unlit         = 1 << 11,
};

inline MaterialFlags operator|(MaterialFlags a, MaterialFlags b) {
    return static_cast<MaterialFlags>(static_cast<u32>(a) | static_cast<u32>(b));
}
inline bool operator&(MaterialFlags a, MaterialFlags b) {
    return (static_cast<u32>(a) & static_cast<u32>(b)) != 0;
}

// ─── GPU Material Data (uploaded to storage buffer) ──────────────────────
// Packed to 64 bytes for cache-friendly access in shaders.
struct alignas(16) GPUMaterialData {
    math::Vec4 baseColorFactor;       // 16 bytes
    f32        metallicFactor;         // 4
    f32        roughnessFactor;        // 4
    f32        emissiveStrength;       // 4
    f32        alphaCutoff;            // 4
    // Bindless texture indices (UINT32_MAX = no texture)
    u32        baseColorTexIdx;        // 4
    u32        normalTexIdx;           // 4
    u32        metallicRoughnessTexIdx;// 4
    u32        emissiveTexIdx;         // 4
    u32        occlusionTexIdx;        // 4
    u32        flags;                  // 4
    f32        clearCoatFactor;        // 4
    f32        clearCoatRoughness;     // 4
};                                     // Total: 64 bytes

static_assert(sizeof(GPUMaterialData) == 64, "GPUMaterialData must be 64 bytes");

// ─── Material ────────────────────────────────────────────────────────────
// CPU-side material representation. References textures by handle.

struct Material {
    std::string name;
    u32         materialId = UINT32_MAX;

    // PBR parameters
    math::Vec4  baseColorFactor = {1, 1, 1, 1};
    f32         metallicFactor  = 0.0f;
    f32         roughnessFactor = 1.0f;
    f32         emissiveStrength = 0.0f;
    f32         alphaCutoff     = 0.5f;

    // Extended parameters
    f32         clearCoatFactor    = 0.0f;
    f32         clearCoatRoughness = 0.0f;
    f32         subsurfaceRadius   = 0.0f;
    f32         anisotropy         = 0.0f;
    f32         sheenRoughness     = 0.0f;
    math::Vec3  sheenColor         = {0, 0, 0};

    // Texture handles
    rhi::TextureHandle baseColorTexture;
    rhi::TextureHandle normalTexture;
    rhi::TextureHandle metallicRoughnessTexture;
    rhi::TextureHandle emissiveTexture;
    rhi::TextureHandle occlusionTexture;

    MaterialFlags flags = MaterialFlags::None;

    // Convert to GPU-uploadable format
    GPUMaterialData ToGPU(rhi::IDevice* device) const {
        GPUMaterialData gpu{};
        gpu.baseColorFactor       = baseColorFactor;
        gpu.metallicFactor        = metallicFactor;
        gpu.roughnessFactor       = roughnessFactor;
        gpu.emissiveStrength      = emissiveStrength;
        gpu.alphaCutoff           = alphaCutoff;
        gpu.baseColorTexIdx       = baseColorTexture.IsValid() ? device->GetBindlessTextureIndex(baseColorTexture) : UINT32_MAX;
        gpu.normalTexIdx          = normalTexture.IsValid() ? device->GetBindlessTextureIndex(normalTexture) : UINT32_MAX;
        gpu.metallicRoughnessTexIdx = metallicRoughnessTexture.IsValid() ? device->GetBindlessTextureIndex(metallicRoughnessTexture) : UINT32_MAX;
        gpu.emissiveTexIdx        = emissiveTexture.IsValid() ? device->GetBindlessTextureIndex(emissiveTexture) : UINT32_MAX;
        gpu.occlusionTexIdx       = occlusionTexture.IsValid() ? device->GetBindlessTextureIndex(occlusionTexture) : UINT32_MAX;
        gpu.flags                 = static_cast<u32>(flags);
        gpu.clearCoatFactor       = clearCoatFactor;
        gpu.clearCoatRoughness    = clearCoatRoughness;
        return gpu;
    }
};

// ─── Material Manager ────────────────────────────────────────────────────
// Owns all materials, uploads to GPU storage buffer for bindless access.

class MaterialManager {
public:
    bool Init(rhi::IDevice* device, u32 maxMaterials = 4096);
    void Shutdown();

    // Create/get material
    u32 CreateMaterial(const Material& mat);
    Material* GetMaterial(u32 id);
    const Material* GetMaterial(u32 id) const;

    // Upload all dirty materials to GPU buffer
    void UploadToGPU();

    // GPU buffer for shader access
    rhi::BufferHandle GetMaterialBuffer() const { return m_gpuBuffer; }
    u32 GetMaterialCount() const { return static_cast<u32>(m_materials.size()); }

private:
    rhi::IDevice*          m_device = nullptr;
    std::vector<Material>  m_materials;
    rhi::BufferHandle      m_gpuBuffer;
    bool                   m_dirty = false;
};

} // namespace nge::renderer
