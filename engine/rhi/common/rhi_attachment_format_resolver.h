#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Attachment Format Resolver ──────────────────────────────────────
// Resolves optimal attachment formats for render passes based on device
// capabilities, usage requirements, and fallback chains. Handles color,
// depth, and stencil format selection with feature validation.
//
// Use cases:
//   - Select optimal color attachment format (HDR, sRGB, etc.)
//   - Select depth/stencil format with fallback chain
//   - Validate format support for specific usage flags
//   - Cache resolved formats to avoid repeated queries
//   - Platform-specific format preferences

enum class AttachmentUsage : u8 {
    ColorAttachment,
    DepthStencil,
    DepthOnly,
    StencilOnly,
    StorageImage,
    SampledImage,
    TransferSrc,
    TransferDst,
};

enum class FormatFeature : u8 {
    None             = 0,
    Filterable       = 1 << 0,   // Supports linear filtering
    Blendable        = 1 << 1,   // Supports color blending
    StorageAtomic    = 1 << 2,   // Supports atomic operations
    MipGeneration    = 1 << 3,   // Can generate mipmaps
    MultiSample      = 1 << 4,   // Supports MSAA
};

enum class ResolvedFormat : u8 {
    Unknown,
    RGBA8_Unorm,
    RGBA8_SRGB,
    RGBA16_Float,
    RGBA32_Float,
    R11G11B10_Float,
    RGB10A2_Unorm,
    RG16_Float,
    R16_Float,
    R32_Float,
    R8_Unorm,
    D16_Unorm,
    D24_Unorm_S8,
    D32_Float,
    D32_Float_S8,
};

struct FormatCapability {
    ResolvedFormat format;
    u8             features;     // FormatFeature bitmask
    bool           supported;
    u32            maxSamples;   // Max MSAA sample count
};

struct FormatRequest {
    AttachmentUsage usage;
    bool            preferHDR;
    bool            requireSRGB;
    bool            requireFilterable;
    bool            requireBlendable;
    u32             requiredSamples;
    std::vector<ResolvedFormat> preferredFormats; // Ordered preference
};

struct AttachmentFormatResolverConfig {
    u32  maxFormats = 64;
    bool cacheResults = true;
};

struct AttachmentFormatResolverStats {
    u32 totalQueries;
    u32 cacheHits;
    u32 cacheMisses;
    u32 fallbacksUsed;
    u32 formatsRegistered;
};

class AttachmentFormatResolver {
public:
    bool Init(const AttachmentFormatResolverConfig& config = {});
    void Shutdown();

    // Register device format capability
    void RegisterFormat(ResolvedFormat format, u8 features, bool supported, u32 maxSamples = 1);

    // Resolve the best format for a request
    ResolvedFormat Resolve(const FormatRequest& request);

    // Quick resolve for common cases
    ResolvedFormat ResolveColorFormat(bool hdr = false, bool srgb = false);
    ResolvedFormat ResolveDepthFormat(bool withStencil = false);

    // Check if a specific format is supported
    bool IsSupported(ResolvedFormat format) const;

    // Check if a format has a specific feature
    bool HasFeature(ResolvedFormat format, FormatFeature feature) const;

    // Get max MSAA samples for a format
    u32 GetMaxSamples(ResolvedFormat format) const;

    // Get format capability info
    const FormatCapability* GetCapability(ResolvedFormat format) const;

    // Get bytes per pixel for a format
    static u32 GetBytesPerPixel(ResolvedFormat format);

    // Check if format is a depth format
    static bool IsDepthFormat(ResolvedFormat format);

    // Check if format has stencil
    static bool HasStencil(ResolvedFormat format);

    u32 GetRegisteredFormatCount() const;

    void ClearCache();
    void Reset();

    AttachmentFormatResolverStats GetStats() const;

private:
    ResolvedFormat FindBestMatch(const FormatRequest& request) const;
    u64 ComputeRequestHash(const FormatRequest& request) const;

    AttachmentFormatResolverConfig m_config;
    std::unordered_map<u8, FormatCapability> m_capabilities; // format enum -> capability

    mutable std::unordered_map<u64, ResolvedFormat> m_cache;
    mutable u32 m_totalQueries = 0;
    mutable u32 m_cacheHits = 0;
    mutable u32 m_cacheMisses = 0;
    mutable u32 m_fallbacksUsed = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
