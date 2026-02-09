#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Render Target Format Negotiator ─────────────────────────────────
// Selects optimal texture formats per attachment type based on device
// capabilities, with fallback chains for portability across GPUs.
//
// Use cases:
//   - GBuffer format selection (HDR vs LDR, precision vs bandwidth)
//   - Depth/stencil format negotiation (D32F vs D24S8 vs D16)
//   - Shadow map format selection
//   - Post-process intermediate format selection
//   - HDR swapchain format negotiation

enum class AttachmentPurpose : u8 {
    ColorHDR,           // HDR render target (R16G16B16A16_SFLOAT preferred)
    ColorLDR,           // LDR render target (R8G8B8A8_UNORM)
    ColorSRGB,          // sRGB output (R8G8B8A8_SRGB)
    GBufferAlbedo,      // Albedo + metallic (R8G8B8A8_UNORM)
    GBufferNormal,      // Encoded normals (R16G16_SFLOAT or R10G10B10A2_UNORM)
    GBufferMotion,      // Motion vectors (R16G16_SFLOAT)
    DepthOnly,          // Depth without stencil (D32_SFLOAT preferred)
    DepthStencil,       // Depth + stencil (D24_UNORM_S8_UINT or D32_SFLOAT_S8_UINT)
    ShadowMap,          // Shadow depth (D16_UNORM for bandwidth)
    StorageImage,       // Compute read/write (R32_UINT or R16G16B16A16_SFLOAT)
    VelocityBuffer,     // Per-pixel velocity (R16G16_SFLOAT)
    AOBuffer,           // Ambient occlusion (R8_UNORM)
    SSAONormals,        // View-space normals for SSAO (R16G16B16A16_SFLOAT)
    HDRBloom,           // Bloom intermediate (R11G11B10_UFLOAT)
};

struct FormatCandidate {
    u32  format;        // VkFormat value
    bool optimal;       // Supports optimal tiling
    bool linear;        // Supports linear tiling
    bool blendable;     // Supports color blend
    bool filterable;    // Supports linear filtering
    bool storageImage;  // Supports storage image (compute R/W)
};

struct FormatFallbackChain {
    AttachmentPurpose purpose;
    std::vector<u32>  candidates; // Ordered by preference (first = best)
    std::string       debugName;
};

struct FormatNegotiatorConfig {
    bool preferBandwidthOverPrecision = false; // True for mobile/tile-based GPUs
    bool requireBlending = true;               // For color attachments
    bool requireFiltering = true;              // For sampled textures
};

struct NegotiatedFormat {
    u32                format;
    AttachmentPurpose  purpose;
    bool               isFallback;  // True if not the preferred format
    std::string        formatName;
};

struct FormatNegotiatorStats {
    u32 totalNegotiations;
    u32 preferredFormatHits;
    u32 fallbacksUsed;
    u32 unsupportedPurposes;
};

class FormatNegotiator {
public:
    bool Init(IDevice* device, const FormatNegotiatorConfig& config = {});
    void Shutdown();

    // Negotiate the best format for a given purpose
    NegotiatedFormat Negotiate(AttachmentPurpose purpose) const;

    // Negotiate with explicit requirements
    NegotiatedFormat NegotiateWithRequirements(AttachmentPurpose purpose,
                                                bool needsBlend, bool needsFilter,
                                                bool needsStorage) const;

    // Check if a specific format is supported for a purpose
    bool IsFormatSupported(u32 format, AttachmentPurpose purpose) const;

    // Get the full fallback chain for a purpose
    const FormatFallbackChain* GetFallbackChain(AttachmentPurpose purpose) const;

    // Override a format for a specific purpose (user preference)
    void OverrideFormat(AttachmentPurpose purpose, u32 format);

    // Get all negotiated formats (cached results)
    std::vector<NegotiatedFormat> GetAllNegotiated() const;

    FormatNegotiatorStats GetStats() const;

private:
    void BuildFallbackChains();
    FormatCandidate QueryFormatSupport(u32 format) const;
    std::string FormatToString(u32 format) const;

    IDevice* m_device = nullptr;
    FormatNegotiatorConfig m_config;
    std::vector<FormatFallbackChain> m_chains;
    std::unordered_map<u8, u32> m_overrides; // purpose -> format
    mutable std::unordered_map<u8, NegotiatedFormat> m_cache;

    mutable u32 m_preferredHits = 0;
    mutable u32 m_fallbacksUsed = 0;
    mutable u32 m_unsupported = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
