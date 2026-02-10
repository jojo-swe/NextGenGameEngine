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

// FilterMode, AddressMode, CompareOp, SamplerDesc are defined in rhi_types.h (included via rhi_device.h)

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
