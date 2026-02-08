#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── Sampler Pool ────────────────────────────────────────────────────────
// Immutable sampler cache with deduplication. Samplers are identified by
// their full filter/address/mip configuration. Identical sampler requests
// return the same handle. Used by the material system and bindless table.

enum class FilterMode : u8 {
    Nearest,
    Linear,
    Anisotropic,
};

enum class AddressMode : u8 {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
};

enum class CompareOp : u8 {
    Never,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Equal,
    NotEqual,
    Always,
};

struct SamplerDesc {
    FilterMode  minFilter = FilterMode::Linear;
    FilterMode  magFilter = FilterMode::Linear;
    FilterMode  mipFilter = FilterMode::Linear;
    AddressMode addressU = AddressMode::Repeat;
    AddressMode addressV = AddressMode::Repeat;
    AddressMode addressW = AddressMode::Repeat;
    f32         mipLodBias = 0.0f;
    u32         maxAnisotropy = 16;
    bool        enableCompare = false;
    CompareOp   compareOp = CompareOp::Never;
    f32         minLod = 0.0f;
    f32         maxLod = 1000.0f;
    bool        unnormalizedCoordinates = false;

    bool operator==(const SamplerDesc& other) const;
};

struct SamplerDescHash {
    size_t operator()(const SamplerDesc& desc) const;
};

class SamplerPool {
public:
    bool Init(IDevice* device);
    void Shutdown();

    // Get or create a sampler matching the description
    SamplerHandle GetOrCreate(const SamplerDesc& desc);

    // Common presets
    SamplerHandle GetLinearClamp();
    SamplerHandle GetLinearRepeat();
    SamplerHandle GetNearestClamp();
    SamplerHandle GetNearestRepeat();
    SamplerHandle GetAnisotropicRepeat(u32 maxAniso = 16);
    SamplerHandle GetShadowSampler();

    u32 GetCachedCount() const;

private:
    IDevice* m_device = nullptr;
    std::unordered_map<SamplerDesc, SamplerHandle, SamplerDescHash> m_cache;
    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
