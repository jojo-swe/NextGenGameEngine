#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>
#include <functional>

namespace nge::rhi {

// ─── GPU Sampler Deduplication Manager ───────────────────────────────────
// Hash-based sampler deduplication with reference counting. Vulkan has a
// limited number of unique samplers (~4000 on most GPUs), so deduplication
// is critical for bindless rendering with many materials.
//
// Use cases:
//   - Deduplicate identical sampler create infos across materials
//   - Reference counting for safe cleanup
//   - Reduce VkSampler object count toward hardware limits
//   - Sampler state canonicalization (normalize equivalent states)

enum class SamplerFilter : u8 {
    Nearest,
    Linear,
    CubicIMG,
};

enum class SamplerMipMode : u8 {
    Nearest,
    Linear,
};

enum class SamplerAddressMode : u8 {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
    MirrorClampToEdge,
};

enum class SamplerBorderColor : u8 {
    TransparentBlack,
    OpaqueBlack,
    OpaqueWhite,
};

enum class SamplerCompareOp : u8 {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
    Disabled,
};

struct SamplerDesc {
    SamplerFilter      magFilter = SamplerFilter::Linear;
    SamplerFilter      minFilter = SamplerFilter::Linear;
    SamplerMipMode     mipMode = SamplerMipMode::Linear;
    SamplerAddressMode addressU = SamplerAddressMode::Repeat;
    SamplerAddressMode addressV = SamplerAddressMode::Repeat;
    SamplerAddressMode addressW = SamplerAddressMode::Repeat;
    float              mipLodBias = 0.0f;
    bool               anisotropyEnable = false;
    float              maxAnisotropy = 1.0f;
    SamplerCompareOp   compareOp = SamplerCompareOp::Disabled;
    float              minLod = 0.0f;
    float              maxLod = 1000.0f;
    SamplerBorderColor borderColor = SamplerBorderColor::TransparentBlack;
    bool               unnormalizedCoords = false;
    std::string        debugName;

    bool operator==(const SamplerDesc& other) const {
        return magFilter == other.magFilter &&
               minFilter == other.minFilter &&
               mipMode == other.mipMode &&
               addressU == other.addressU &&
               addressV == other.addressV &&
               addressW == other.addressW &&
               mipLodBias == other.mipLodBias &&
               anisotropyEnable == other.anisotropyEnable &&
               maxAnisotropy == other.maxAnisotropy &&
               compareOp == other.compareOp &&
               minLod == other.minLod &&
               maxLod == other.maxLod &&
               borderColor == other.borderColor &&
               unnormalizedCoords == other.unnormalizedCoords;
    }
    u64 ComputeHash() const;
};

struct SamplerDescHasher {
    size_t operator()(const SamplerDesc& desc) const {
        return static_cast<size_t>(desc.ComputeHash());
    }
};

struct SamplerEntry {
    SamplerDesc desc;
    u64         samplerHandle;  // VkSampler or equivalent
    u32         refCount;
    u32         bindlessIndex;  // Index in global sampler array (if bindless)
};

struct SamplerDedupConfig {
    u32  maxSamplers = 4096;    // Vulkan typical limit
    bool enableRefCounting = true;
    bool canonicalize = true;   // Normalize equivalent states
};

struct SamplerDedupStats {
    u32 totalSamplers;
    u32 totalAcquires;
    u32 deduplicated;           // Times an existing sampler was reused
    u32 totalReleases;
    u32 totalDestroyed;
    u32 peakSamplers;
};

class SamplerDedupManager {
public:
    bool Init(const SamplerDedupConfig& config = {});
    void Shutdown();

    // Acquire a sampler matching the description. Creates or reuses.
    // Returns sampler handle.
    u64 Acquire(const SamplerDesc& desc);

    // Release a reference. Destroys sampler when refCount reaches 0.
    void Release(u64 samplerHandle);

    // Get the ref count for a sampler.
    u32 GetRefCount(u64 samplerHandle) const;

    // Check if a sampler with this description exists.
    bool Exists(const SamplerDesc& desc) const;

    // Get the sampler entry by handle.
    const SamplerEntry* GetEntry(u64 samplerHandle) const;

    // Get current sampler count.
    u32 GetCount() const;

    // Force destroy all samplers with refCount == 0.
    u32 PurgeUnreferenced();

    void Reset();

    SamplerDedupStats GetStats() const;

private:
    SamplerDesc Canonicalize(const SamplerDesc& desc) const;
    u64 CreateSamplerHandle(const SamplerDesc& desc);

    SamplerDedupConfig m_config;

    // desc hash -> entry
    std::unordered_map<SamplerDesc, SamplerEntry, SamplerDescHasher> m_samplersByDesc;
    // handle -> desc (for reverse lookup on Release)
    std::unordered_map<u64, SamplerDesc> m_handleToDesc;

    u64 m_nextHandle = 1;
    u32 m_totalAcquires = 0;
    u32 m_deduplicated = 0;
    u32 m_totalReleases = 0;
    u32 m_totalDestroyed = 0;
    u32 m_peakSamplers = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
