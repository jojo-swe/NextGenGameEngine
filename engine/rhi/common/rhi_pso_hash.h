#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_pso_builder.h"
#include <functional>

namespace nge::rhi {

// ─── GPU Pipeline State Hash ─────────────────────────────────────────────
// Fast PSO deduplication by hashing the entire pipeline state descriptor.
// Allows the pipeline cache to detect duplicate configurations without
// comparing every field individually.

struct PSOHash {
    u64 value = 0;

    bool operator==(const PSOHash& other) const { return value == other.value; }
    bool operator!=(const PSOHash& other) const { return value != other.value; }
    bool operator<(const PSOHash& other) const { return value < other.value; }
};

class PSOHasher {
public:
    // Hash a complete graphics PSO description
    static PSOHash Hash(const GraphicsPSODesc& desc);

    // Hash a complete compute PSO description
    static PSOHash Hash(const ComputePSODesc& desc);

    // Incremental hash builder for custom state combinations
    class Builder {
    public:
        Builder& Feed(u8 val);
        Builder& Feed(u16 val);
        Builder& Feed(u32 val);
        Builder& Feed(u64 val);
        Builder& Feed(f32 val);
        Builder& Feed(bool val);
        Builder& Feed(const std::string& val);
        Builder& Feed(const void* data, u32 bytes);

        PSOHash Finalize() const;

    private:
        u64 m_hash = 0xcbf29ce484222325ULL; // FNV-1a offset basis
        void FeedByte(u8 byte);
    };

private:
    static void HashRasterState(Builder& b, const GraphicsPSODesc& desc);
    static void HashDepthStencilState(Builder& b, const GraphicsPSODesc& desc);
    static void HashBlendState(Builder& b, const GraphicsPSODesc& desc);
    static void HashVertexInput(Builder& b, const GraphicsPSODesc& desc);
    static void HashShaders(Builder& b, const GraphicsPSODesc& desc);
    static void HashRenderTargets(Builder& b, const GraphicsPSODesc& desc);
};

} // namespace nge::rhi

// STL hash specialization
namespace std {
template<> struct hash<nge::rhi::PSOHash> {
    size_t operator()(const nge::rhi::PSOHash& h) const noexcept {
        return static_cast<size_t>(h.value);
    }
};
} // namespace std
