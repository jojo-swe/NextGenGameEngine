#include "engine/rhi/common/rhi_format_negotiator.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

// Common VkFormat values for reference
namespace fmt {
    constexpr u32 R8_UNORM             = 9;
    constexpr u32 R16_SFLOAT           = 76;
    constexpr u32 R16G16_SFLOAT        = 83;
    constexpr u32 R16G16B16A16_SFLOAT  = 97;
    constexpr u32 R32_SFLOAT           = 100;
    constexpr u32 R32_UINT             = 98;
    constexpr u32 R8G8B8A8_UNORM       = 37;
    constexpr u32 R8G8B8A8_SRGB        = 43;
    constexpr u32 B8G8R8A8_UNORM       = 44;
    constexpr u32 B8G8R8A8_SRGB        = 50;
    constexpr u32 R10G10B10A2_UNORM    = 64;
    constexpr u32 R11G11B10_UFLOAT     = 26;
    constexpr u32 D16_UNORM            = 124;
    constexpr u32 D24_UNORM_S8_UINT    = 129;
    constexpr u32 D32_SFLOAT           = 126;
    constexpr u32 D32_SFLOAT_S8_UINT   = 130;
}

bool FormatNegotiator::Init(IDevice* device, const FormatNegotiatorConfig& config) {
    m_device = device;
    m_config = config;

    BuildFallbackChains();

    NGE_LOG_INFO("Format negotiator initialized: {} fallback chains, bandwidth_pref={}, blend={}, filter={}",
                 m_chains.size(), config.preferBandwidthOverPrecision,
                 config.requireBlending, config.requireFiltering);
    return true;
}

void FormatNegotiator::Shutdown() {
    m_chains.clear();
    m_overrides.clear();
    m_cache.clear();
}

NegotiatedFormat FormatNegotiator::Negotiate(AttachmentPurpose purpose) const {
    return NegotiateWithRequirements(purpose, m_config.requireBlending,
                                      m_config.requireFiltering, false);
}

NegotiatedFormat FormatNegotiator::NegotiateWithRequirements(AttachmentPurpose purpose,
                                                              bool needsBlend, bool needsFilter,
                                                              bool needsStorage) const {
    std::lock_guard lock(m_mutex);

    // Check override
    auto overIt = m_overrides.find(static_cast<u8>(purpose));
    if (overIt != m_overrides.end()) {
        NegotiatedFormat result;
        result.format = overIt->second;
        result.purpose = purpose;
        result.isFallback = false;
        result.formatName = FormatToString(overIt->second);
        return result;
    }

    // Check cache
    auto cacheIt = m_cache.find(static_cast<u8>(purpose));
    if (cacheIt != m_cache.end()) {
        return cacheIt->second;
    }

    // Find fallback chain
    const FormatFallbackChain* chain = nullptr;
    for (const auto& c : m_chains) {
        if (c.purpose == purpose) {
            chain = &c;
            break;
        }
    }

    if (!chain || chain->candidates.empty()) {
        m_unsupported++;
        NegotiatedFormat result;
        result.format = 0;
        result.purpose = purpose;
        result.isFallback = true;
        result.formatName = "UNSUPPORTED";
        return result;
    }

    // Walk the fallback chain
    bool isFirst = true;
    for (u32 candidate : chain->candidates) {
        FormatCandidate support = QueryFormatSupport(candidate);

        if (!support.optimal) { isFirst = false; continue; }
        if (needsBlend && !support.blendable) { isFirst = false; continue; }
        if (needsFilter && !support.filterable) { isFirst = false; continue; }
        if (needsStorage && !support.storageImage) { isFirst = false; continue; }

        NegotiatedFormat result;
        result.format = candidate;
        result.purpose = purpose;
        result.isFallback = !isFirst;
        result.formatName = FormatToString(candidate);

        if (isFirst) m_preferredHits++;
        else m_fallbacksUsed++;

        m_cache[static_cast<u8>(purpose)] = result;
        return result;
    }

    // No suitable format found
    m_unsupported++;
    NegotiatedFormat result;
    result.format = chain->candidates[0]; // Return preferred anyway
    result.purpose = purpose;
    result.isFallback = true;
    result.formatName = FormatToString(chain->candidates[0]) + " (forced)";
    return result;
}

bool FormatNegotiator::IsFormatSupported(u32 format, AttachmentPurpose purpose) const {
    std::lock_guard lock(m_mutex);
    const FormatFallbackChain* chain = GetFallbackChain(purpose);
    if (!chain) return false;

    for (u32 candidate : chain->candidates) {
        if (candidate == format) {
            FormatCandidate support = QueryFormatSupport(format);
            return support.optimal;
        }
    }
    return false;
}

const FormatFallbackChain* FormatNegotiator::GetFallbackChain(AttachmentPurpose purpose) const {
    for (const auto& chain : m_chains) {
        if (chain.purpose == purpose) return &chain;
    }
    return nullptr;
}

void FormatNegotiator::OverrideFormat(AttachmentPurpose purpose, u32 format) {
    std::lock_guard lock(m_mutex);
    m_overrides[static_cast<u8>(purpose)] = format;
    m_cache.erase(static_cast<u8>(purpose)); // Invalidate cache
}

std::vector<NegotiatedFormat> FormatNegotiator::GetAllNegotiated() const {
    std::vector<NegotiatedFormat> results;
    for (const auto& chain : m_chains) {
        results.push_back(Negotiate(chain.purpose));
    }
    return results;
}

FormatNegotiatorStats FormatNegotiator::GetStats() const {
    std::lock_guard lock(m_mutex);
    FormatNegotiatorStats stats{};
    stats.totalNegotiations = m_preferredHits + m_fallbacksUsed + m_unsupported;
    stats.preferredFormatHits = m_preferredHits;
    stats.fallbacksUsed = m_fallbacksUsed;
    stats.unsupportedPurposes = m_unsupported;
    return stats;
}

void FormatNegotiator::BuildFallbackChains() {
    m_chains.clear();

    auto addChain = [this](AttachmentPurpose purpose, const std::string& name,
                            std::vector<u32> candidates) {
        FormatFallbackChain chain;
        chain.purpose = purpose;
        chain.debugName = name;
        chain.candidates = std::move(candidates);
        m_chains.push_back(std::move(chain));
    };

    if (m_config.preferBandwidthOverPrecision) {
        // Mobile/tile-based: prefer smaller formats
        addChain(AttachmentPurpose::ColorHDR, "ColorHDR",
                 {fmt::R11G11B10_UFLOAT, fmt::R16G16B16A16_SFLOAT});
        addChain(AttachmentPurpose::GBufferNormal, "GBufferNormal",
                 {fmt::R10G10B10A2_UNORM, fmt::R16G16_SFLOAT, fmt::R16G16B16A16_SFLOAT});
        addChain(AttachmentPurpose::ShadowMap, "ShadowMap",
                 {fmt::D16_UNORM, fmt::D32_SFLOAT});
    } else {
        // Desktop: prefer precision
        addChain(AttachmentPurpose::ColorHDR, "ColorHDR",
                 {fmt::R16G16B16A16_SFLOAT, fmt::R11G11B10_UFLOAT});
        addChain(AttachmentPurpose::GBufferNormal, "GBufferNormal",
                 {fmt::R16G16_SFLOAT, fmt::R10G10B10A2_UNORM, fmt::R16G16B16A16_SFLOAT});
        addChain(AttachmentPurpose::ShadowMap, "ShadowMap",
                 {fmt::D32_SFLOAT, fmt::D16_UNORM});
    }

    addChain(AttachmentPurpose::ColorLDR, "ColorLDR",
             {fmt::R8G8B8A8_UNORM, fmt::B8G8R8A8_UNORM});

    addChain(AttachmentPurpose::ColorSRGB, "ColorSRGB",
             {fmt::R8G8B8A8_SRGB, fmt::B8G8R8A8_SRGB});

    addChain(AttachmentPurpose::GBufferAlbedo, "GBufferAlbedo",
             {fmt::R8G8B8A8_UNORM, fmt::B8G8R8A8_UNORM});

    addChain(AttachmentPurpose::GBufferMotion, "GBufferMotion",
             {fmt::R16G16_SFLOAT, fmt::R16G16B16A16_SFLOAT});

    addChain(AttachmentPurpose::DepthOnly, "DepthOnly",
             {fmt::D32_SFLOAT, fmt::D16_UNORM});

    addChain(AttachmentPurpose::DepthStencil, "DepthStencil",
             {fmt::D24_UNORM_S8_UINT, fmt::D32_SFLOAT_S8_UINT});

    addChain(AttachmentPurpose::StorageImage, "StorageImage",
             {fmt::R16G16B16A16_SFLOAT, fmt::R32_UINT});

    addChain(AttachmentPurpose::VelocityBuffer, "VelocityBuffer",
             {fmt::R16G16_SFLOAT, fmt::R16G16B16A16_SFLOAT});

    addChain(AttachmentPurpose::AOBuffer, "AOBuffer",
             {fmt::R8_UNORM, fmt::R16_SFLOAT});

    addChain(AttachmentPurpose::SSAONormals, "SSAONormals",
             {fmt::R16G16B16A16_SFLOAT, fmt::R10G10B10A2_UNORM});

    addChain(AttachmentPurpose::HDRBloom, "HDRBloom",
             {fmt::R11G11B10_UFLOAT, fmt::R16G16B16A16_SFLOAT});
}

FormatCandidate FormatNegotiator::QueryFormatSupport(u32 format) const {
    // TODO: Query via vkGetPhysicalDeviceFormatProperties
    // VkFormatProperties props;
    // vkGetPhysicalDeviceFormatProperties(physicalDevice, (VkFormat)format, &props);
    // support.optimal = (props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;
    // support.blendable = (props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0;
    // support.filterable = (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
    // support.storageImage = (props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;

    // Stub: assume all formats supported on desktop discrete GPU
    FormatCandidate support;
    support.format = format;
    support.optimal = true;
    support.linear = true;
    support.blendable = (format != fmt::D16_UNORM && format != fmt::D32_SFLOAT &&
                          format != fmt::D24_UNORM_S8_UINT && format != fmt::D32_SFLOAT_S8_UINT);
    support.filterable = true;
    support.storageImage = (format == fmt::R16G16B16A16_SFLOAT || format == fmt::R32_UINT ||
                             format == fmt::R32_SFLOAT || format == fmt::R11G11B10_UFLOAT);
    return support;
}

std::string FormatNegotiator::FormatToString(u32 format) const {
    switch (format) {
        case fmt::R8_UNORM:            return "R8_UNORM";
        case fmt::R16_SFLOAT:          return "R16_SFLOAT";
        case fmt::R16G16_SFLOAT:       return "R16G16_SFLOAT";
        case fmt::R16G16B16A16_SFLOAT: return "R16G16B16A16_SFLOAT";
        case fmt::R32_SFLOAT:          return "R32_SFLOAT";
        case fmt::R32_UINT:            return "R32_UINT";
        case fmt::R8G8B8A8_UNORM:      return "R8G8B8A8_UNORM";
        case fmt::R8G8B8A8_SRGB:       return "R8G8B8A8_SRGB";
        case fmt::B8G8R8A8_UNORM:      return "B8G8R8A8_UNORM";
        case fmt::B8G8R8A8_SRGB:       return "B8G8R8A8_SRGB";
        case fmt::R10G10B10A2_UNORM:   return "R10G10B10A2_UNORM";
        case fmt::R11G11B10_UFLOAT:    return "R11G11B10_UFLOAT";
        case fmt::D16_UNORM:           return "D16_UNORM";
        case fmt::D24_UNORM_S8_UINT:   return "D24_UNORM_S8_UINT";
        case fmt::D32_SFLOAT:          return "D32_SFLOAT";
        case fmt::D32_SFLOAT_S8_UINT:  return "D32_SFLOAT_S8_UINT";
        default:                        return "UNKNOWN(" + std::to_string(format) + ")";
    }
}

} // namespace nge::rhi
