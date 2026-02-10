#include "engine/rhi/common/rhi_attachment_format_resolver.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool AttachmentFormatResolver::Init(const AttachmentFormatResolverConfig& config) {
    m_config = config;
    m_totalQueries = 0;
    m_cacheHits = 0;
    m_cacheMisses = 0;
    m_fallbacksUsed = 0;

    NGE_LOG_INFO("Attachment format resolver initialized: maxFormats={}, cache={}",
                 config.maxFormats, config.cacheResults);
    return true;
}

void AttachmentFormatResolver::Shutdown() {
    m_capabilities.clear();
    m_cache.clear();
}

void AttachmentFormatResolver::RegisterFormat(ResolvedFormat format, u8 features, bool supported, u32 maxSamples) {
    std::lock_guard lock(m_mutex);

    FormatCapability cap;
    cap.format = format;
    cap.features = features;
    cap.supported = supported;
    cap.maxSamples = maxSamples;

    m_capabilities[static_cast<u8>(format)] = cap;
}

ResolvedFormat AttachmentFormatResolver::Resolve(const FormatRequest& request) {
    std::lock_guard lock(m_mutex);

    m_totalQueries++;

    // Check cache
    if (m_config.cacheResults) {
        u64 hash = ComputeRequestHash(request);
        auto it = m_cache.find(hash);
        if (it != m_cache.end()) {
            m_cacheHits++;
            return it->second;
        }
        m_cacheMisses++;
    }

    ResolvedFormat result = FindBestMatch(request);

    // Cache result
    if (m_config.cacheResults) {
        u64 hash = ComputeRequestHash(request);
        m_cache[hash] = result;
    }

    return result;
}

ResolvedFormat AttachmentFormatResolver::ResolveColorFormat(bool hdr, bool srgb) {
    FormatRequest req;
    req.usage = AttachmentUsage::ColorAttachment;
    req.preferHDR = hdr;
    req.requireSRGB = srgb;
    req.requireFilterable = true;
    req.requireBlendable = true;
    req.requiredSamples = 1;

    if (hdr) {
        req.preferredFormats = {ResolvedFormat::RGBA16_Float, ResolvedFormat::R11G11B10_Float,
                                 ResolvedFormat::RGB10A2_Unorm, ResolvedFormat::RGBA8_Unorm};
    } else if (srgb) {
        req.preferredFormats = {ResolvedFormat::RGBA8_SRGB, ResolvedFormat::RGBA8_Unorm};
    } else {
        req.preferredFormats = {ResolvedFormat::RGBA8_Unorm, ResolvedFormat::RGBA8_SRGB};
    }

    return Resolve(req);
}

ResolvedFormat AttachmentFormatResolver::ResolveDepthFormat(bool withStencil) {
    FormatRequest req;
    req.usage = withStencil ? AttachmentUsage::DepthStencil : AttachmentUsage::DepthOnly;
    req.preferHDR = false;
    req.requireSRGB = false;
    req.requireFilterable = false;
    req.requireBlendable = false;
    req.requiredSamples = 1;

    if (withStencil) {
        req.preferredFormats = {ResolvedFormat::D32_Float_S8, ResolvedFormat::D24_Unorm_S8};
    } else {
        req.preferredFormats = {ResolvedFormat::D32_Float, ResolvedFormat::D24_Unorm_S8,
                                 ResolvedFormat::D16_Unorm};
    }

    return Resolve(req);
}

bool AttachmentFormatResolver::IsSupported(ResolvedFormat format) const {
    std::lock_guard lock(m_mutex);

    auto it = m_capabilities.find(static_cast<u8>(format));
    if (it == m_capabilities.end()) return false;
    return it->second.supported;
}

bool AttachmentFormatResolver::HasFeature(ResolvedFormat format, FormatFeature feature) const {
    std::lock_guard lock(m_mutex);

    auto it = m_capabilities.find(static_cast<u8>(format));
    if (it == m_capabilities.end()) return false;

    return (it->second.features & static_cast<u8>(feature)) != 0;
}

u32 AttachmentFormatResolver::GetMaxSamples(ResolvedFormat format) const {
    std::lock_guard lock(m_mutex);

    auto it = m_capabilities.find(static_cast<u8>(format));
    if (it == m_capabilities.end()) return 0;

    return it->second.maxSamples;
}

const FormatCapability* AttachmentFormatResolver::GetCapability(ResolvedFormat format) const {
    std::lock_guard lock(m_mutex);

    auto it = m_capabilities.find(static_cast<u8>(format));
    if (it == m_capabilities.end()) return nullptr;

    return &it->second;
}

u32 AttachmentFormatResolver::GetBytesPerPixel(ResolvedFormat format) {
    switch (format) {
        case ResolvedFormat::R8_Unorm:          return 1;
        case ResolvedFormat::R16_Float:
        case ResolvedFormat::D16_Unorm:         return 2;
        case ResolvedFormat::RGBA8_Unorm:
        case ResolvedFormat::RGBA8_SRGB:
        case ResolvedFormat::R11G11B10_Float:
        case ResolvedFormat::RGB10A2_Unorm:
        case ResolvedFormat::RG16_Float:
        case ResolvedFormat::R32_Float:
        case ResolvedFormat::D24_Unorm_S8:
        case ResolvedFormat::D32_Float:         return 4;
        case ResolvedFormat::D32_Float_S8:      return 5;
        case ResolvedFormat::RGBA16_Float:      return 8;
        case ResolvedFormat::RGBA32_Float:      return 16;
        default:                                return 0;
    }
}

bool AttachmentFormatResolver::IsDepthFormat(ResolvedFormat format) {
    return format == ResolvedFormat::D16_Unorm ||
           format == ResolvedFormat::D24_Unorm_S8 ||
           format == ResolvedFormat::D32_Float ||
           format == ResolvedFormat::D32_Float_S8;
}

bool AttachmentFormatResolver::HasStencil(ResolvedFormat format) {
    return format == ResolvedFormat::D24_Unorm_S8 ||
           format == ResolvedFormat::D32_Float_S8;
}

u32 AttachmentFormatResolver::GetRegisteredFormatCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_capabilities.size());
}

void AttachmentFormatResolver::ClearCache() {
    std::lock_guard lock(m_mutex);
    m_cache.clear();
}

void AttachmentFormatResolver::Reset() {
    std::lock_guard lock(m_mutex);
    m_capabilities.clear();
    m_cache.clear();
    m_totalQueries = 0;
    m_cacheHits = 0;
    m_cacheMisses = 0;
    m_fallbacksUsed = 0;
}

AttachmentFormatResolverStats AttachmentFormatResolver::GetStats() const {
    std::lock_guard lock(m_mutex);

    AttachmentFormatResolverStats stats{};
    stats.totalQueries = m_totalQueries;
    stats.cacheHits = m_cacheHits;
    stats.cacheMisses = m_cacheMisses;
    stats.fallbacksUsed = m_fallbacksUsed;
    stats.formatsRegistered = static_cast<u32>(m_capabilities.size());

    return stats;
}

ResolvedFormat AttachmentFormatResolver::FindBestMatch(const FormatRequest& request) const {
    // Try preferred formats in order
    for (auto preferred : request.preferredFormats) {
        auto it = m_capabilities.find(static_cast<u8>(preferred));
        if (it == m_capabilities.end() || !it->second.supported) continue;

        const auto& cap = it->second;

        // Check required features
        if (request.requireFilterable && !(cap.features & static_cast<u8>(FormatFeature::Filterable))) continue;
        if (request.requireBlendable && !(cap.features & static_cast<u8>(FormatFeature::Blendable))) continue;
        if (request.requiredSamples > 1 && cap.maxSamples < request.requiredSamples) continue;

        return preferred;
    }

    m_fallbacksUsed++;

    // Fallback: find any supported format matching usage
    for (const auto& [key, cap] : m_capabilities) {
        if (!cap.supported) continue;

        bool usageMatch = false;
        switch (request.usage) {
            case AttachmentUsage::ColorAttachment:
                usageMatch = !IsDepthFormat(cap.format);
                break;
            case AttachmentUsage::DepthStencil:
                usageMatch = IsDepthFormat(cap.format) && HasStencil(cap.format);
                break;
            case AttachmentUsage::DepthOnly:
                usageMatch = IsDepthFormat(cap.format);
                break;
            default:
                usageMatch = true;
                break;
        }

        if (usageMatch) return cap.format;
    }

    return ResolvedFormat::Unknown;
}

u64 AttachmentFormatResolver::ComputeRequestHash(const FormatRequest& request) const {
    u64 hash = 14695981039346656037ULL;
    auto hashByte = [&](u8 b) { hash ^= b; hash *= 1099511628211ULL; };

    hashByte(static_cast<u8>(request.usage));
    hashByte(request.preferHDR ? 1 : 0);
    hashByte(request.requireSRGB ? 1 : 0);
    hashByte(request.requireFilterable ? 1 : 0);
    hashByte(request.requireBlendable ? 1 : 0);
    hashByte(static_cast<u8>(request.requiredSamples));

    for (auto fmt : request.preferredFormats) {
        hashByte(static_cast<u8>(fmt));
    }

    return hash;
}

} // namespace nge::rhi
