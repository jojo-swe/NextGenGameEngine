#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/rhi/common/rhi_device.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace nge::renderer {

// ─── Material System ─────────────────────────────────────────────────────
// PBR material instances with bindless texture references.
// Materials are GPU data-driven — each material is a small uniform block
// plus texture descriptor indices for the bindless heap.

using MaterialId = u32;
inline constexpr MaterialId INVALID_MATERIAL = UINT32_MAX;

// ─── Texture Slot ────────────────────────────────────────────────────────

enum class TextureSlot : u8 {
    Albedo = 0,
    Normal,
    MetallicRoughness,  // R = metallic, G = roughness
    Emissive,
    AmbientOcclusion,
    Height,             // For parallax occlusion mapping
    DetailAlbedo,
    DetailNormal,
    Count
};

// ─── Material Flags ──────────────────────────────────────────────────────

enum class MaterialFlags : u32 {
    None             = 0,
    AlphaTest        = 1 << 0,
    AlphaBlend       = 1 << 1,
    DoubleSided      = 1 << 2,
    Emissive         = 1 << 3,
    HasNormalMap      = 1 << 4,
    HasHeightMap      = 1 << 5,
    HasDetailTextures = 1 << 6,
    Unlit            = 1 << 7,
    Subsurface       = 1 << 8,
    ClearCoat        = 1 << 9,
    Anisotropic      = 1 << 10,
};

inline MaterialFlags operator|(MaterialFlags a, MaterialFlags b) {
    return static_cast<MaterialFlags>(static_cast<u32>(a) | static_cast<u32>(b));
}
inline bool operator&(MaterialFlags a, MaterialFlags b) {
    return (static_cast<u32>(a) & static_cast<u32>(b)) != 0;
}

// ─── GPU Material Data (matches shader cbuffer) ──────────────────────────

struct alignas(16) GPUMaterialData {
    math::Vec4 baseColorFactor;       // RGBA base color multiplier
    f32        metallicFactor;
    f32        roughnessFactor;
    f32        emissiveStrength;
    f32        alphaCutoff;

    f32        normalScale;
    f32        heightScale;           // Parallax occlusion mapping scale
    f32        detailTiling;          // UV tiling for detail textures
    f32        clearCoatFactor;

    f32        clearCoatRoughness;
    f32        anisotropy;
    f32        subsurfaceRadius;
    u32        flags;                 // MaterialFlags bitmask

    // Bindless texture indices (UINT32_MAX = no texture)
    u32        albedoTexIdx;
    u32        normalTexIdx;
    u32        metallicRoughnessTexIdx;
    u32        emissiveTexIdx;

    u32        aoTexIdx;
    u32        heightTexIdx;
    u32        detailAlbedoTexIdx;
    u32        detailNormalTexIdx;
};

static_assert(sizeof(GPUMaterialData) == 96, "GPUMaterialData must be 96 bytes");

// ─── Material Instance ───────────────────────────────────────────────────

struct MaterialInstance {
    MaterialId    id = INVALID_MATERIAL;
    std::string   name;
    GPUMaterialData gpuData;

    // CPU-side texture handles (for resource management)
    rhi::TextureHandle textures[static_cast<u32>(TextureSlot::Count)];

    // Shader permutation key
    u32 permutationKey = 0;

    bool dirty = true; // Needs GPU upload
};

// ─── Material Manager ────────────────────────────────────────────────────

class MaterialManager {
public:
    bool Init(rhi::IDevice* device, u32 maxMaterials = 4096);
    void Shutdown();

    // Create / destroy
    MaterialId CreateMaterial(const std::string& name);
    MaterialId CreateMaterialFromGLTF(const std::string& name, const GPUMaterialData& data);
    void DestroyMaterial(MaterialId id);

    // Modify
    MaterialInstance* GetMaterial(MaterialId id);
    const MaterialInstance* GetMaterial(MaterialId id) const;

    void SetBaseColor(MaterialId id, const math::Vec4& color);
    void SetMetallicRoughness(MaterialId id, f32 metallic, f32 roughness);
    void SetEmissive(MaterialId id, f32 strength);
    void SetTexture(MaterialId id, TextureSlot slot, rhi::TextureHandle texture, u32 bindlessIdx);
    void SetFlags(MaterialId id, MaterialFlags flags);
    void SetAlphaCutoff(MaterialId id, f32 cutoff);

    // Query
    MaterialId FindByName(const std::string& name) const;
    u32 GetMaterialCount() const { return static_cast<u32>(m_materials.size()); }

    // GPU upload (call once per frame for dirty materials)
    void UploadDirtyMaterials();

    // Get GPU buffer for shader binding
    rhi::BufferHandle GetMaterialBuffer() const { return m_gpuBuffer; }

    // Default materials
    MaterialId GetDefaultOpaque() const { return m_defaultOpaque; }
    MaterialId GetDefaultTransparent() const { return m_defaultTransparent; }

private:
    void CreateDefaults();

    rhi::IDevice* m_device = nullptr;

    std::unordered_map<MaterialId, MaterialInstance> m_materials;
    std::unordered_map<std::string, MaterialId> m_nameToId;
    MaterialId m_nextId = 0;
    u32 m_maxMaterials = 0;

    rhi::BufferHandle m_gpuBuffer;   // Structured buffer of GPUMaterialData
    rhi::BufferHandle m_stagingBuffer;

    MaterialId m_defaultOpaque = INVALID_MATERIAL;
    MaterialId m_defaultTransparent = INVALID_MATERIAL;
};

} // namespace nge::renderer
