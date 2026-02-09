#include "engine/rhi/vulkan/vk_swapchain_format_selector.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi::vulkan {

bool SwapchainFormatSelector::Init(const SwapchainFormatConfig& config) {
    m_config = config;
    m_hasForced = false;

    NGE_LOG_INFO("Swapchain format selector initialized: mode={}, fallback={}, bitDepth={}",
                 static_cast<int>(config.preferredMode), config.allowFallback, config.preferredBitDepth);
    return true;
}

void SwapchainFormatSelector::Shutdown() {
    m_available.clear();
    m_hasForced = false;
}

void SwapchainFormatSelector::SetAvailableFormats(const std::vector<SurfaceFormat>& formats) {
    std::lock_guard lock(m_mutex);
    m_available = formats;
}

void SwapchainFormatSelector::SetDisplayCapabilities(f32 maxLuminance, f32 minLuminance, f32 maxFrameAvgLuminance) {
    std::lock_guard lock(m_mutex);
    m_maxLuminance = maxLuminance;
    m_minLuminance = minLuminance;
    m_maxFrameAvgLuminance = maxFrameAvgLuminance;
}

SwapchainFormatResult SwapchainFormatSelector::Select() {
    std::lock_guard lock(m_mutex);

    // Forced override
    if (m_hasForced) {
        SwapchainFormatResult result;
        result.selected = m_forced;
        result.activeMode = HDRMode::SDR;
        result.isFallback = false;
        result.formatName = FormatToString(m_forced.format);
        result.colorSpaceName = ColorSpaceToString(m_forced.colorSpace);
        result.maxLuminance = m_maxLuminance;
        result.minLuminance = m_minLuminance;
        return result;
    }

    HDRMode mode = m_config.preferredMode;

    if (mode == HDRMode::Auto) {
        // Auto: try HDR10 first, then scRGB, then DisplayP3, then SDR
        if (IsModeAvailable(HDRMode::HDR10) && m_maxLuminance > 400.0f) {
            mode = HDRMode::HDR10;
        } else if (IsModeAvailable(HDRMode::HDR_scRGB)) {
            mode = HDRMode::HDR_scRGB;
        } else if (IsModeAvailable(HDRMode::DisplayP3)) {
            mode = HDRMode::DisplayP3;
        } else {
            mode = HDRMode::SDR;
        }
    }

    SwapchainFormatResult result;

    switch (mode) {
        case HDRMode::HDR10:
            result = SelectHDR10();
            if (result.selected.format == 0 && m_config.allowFallback) {
                result = SelectSDR();
                result.isFallback = true;
            }
            break;

        case HDRMode::HDR_scRGB:
            result = SelectScRGB();
            if (result.selected.format == 0 && m_config.allowFallback) {
                result = SelectSDR();
                result.isFallback = true;
            }
            break;

        case HDRMode::DisplayP3:
            result = SelectDisplayP3();
            if (result.selected.format == 0 && m_config.allowFallback) {
                result = SelectSDR();
                result.isFallback = true;
            }
            break;

        default:
            result = SelectSDR();
            break;
    }

    result.maxLuminance = m_maxLuminance;
    result.minLuminance = m_minLuminance;

    NGE_LOG_INFO("Swapchain format selected: {} / {} (mode={}, fallback={})",
                 result.formatName, result.colorSpaceName,
                 static_cast<int>(result.activeMode), result.isFallback);

    return result;
}

void SwapchainFormatSelector::ForceFormat(u32 format, u32 colorSpace) {
    std::lock_guard lock(m_mutex);
    m_forced = {format, colorSpace};
    m_hasForced = true;
}

bool SwapchainFormatSelector::IsModeAvailable(HDRMode mode) const {
    for (const auto& fmt : m_available) {
        switch (mode) {
            case HDRMode::HDR10:
                if (fmt.colorSpace == ColorSpace::HDR10_ST2084 &&
                    (fmt.format == SwapFmt::A2B10G10R10_UNORM || fmt.format == SwapFmt::R16G16B16A16_SFLOAT))
                    return true;
                break;

            case HDRMode::HDR_scRGB:
                if (fmt.colorSpace == ColorSpace::EXTENDED_SRGB_LINEAR &&
                    fmt.format == SwapFmt::R16G16B16A16_SFLOAT)
                    return true;
                break;

            case HDRMode::DisplayP3:
                if (fmt.colorSpace == ColorSpace::DISPLAY_P3_NONLINEAR)
                    return true;
                break;

            case HDRMode::SDR:
                if (fmt.colorSpace == ColorSpace::SRGB_NONLINEAR)
                    return true;
                break;

            default:
                break;
        }
    }
    return false;
}

SwapchainFormatStats SwapchainFormatSelector::GetStats() const {
    std::lock_guard lock(m_mutex);
    SwapchainFormatStats stats{};
    stats.availableFormats = static_cast<u32>(m_available.size());

    for (const auto& fmt : m_available) {
        if (fmt.colorSpace != ColorSpace::SRGB_NONLINEAR) stats.hdrFormatsAvailable++;
        if (fmt.colorSpace == ColorSpace::HDR10_ST2084) stats.hdr10Supported = true;
        if (fmt.colorSpace == ColorSpace::EXTENDED_SRGB_LINEAR &&
            fmt.format == SwapFmt::R16G16B16A16_SFLOAT) stats.scRGBSupported = true;
        if (fmt.colorSpace == ColorSpace::DISPLAY_P3_NONLINEAR) stats.displayP3Supported = true;
    }

    return stats;
}

SwapchainFormatResult SwapchainFormatSelector::SelectHDR10() const {
    SwapchainFormatResult result{};
    result.activeMode = HDRMode::HDR10;
    result.isFallback = false;

    // Prefer A2B10G10R10 for HDR10 (10-bit)
    for (const auto& fmt : m_available) {
        if (fmt.colorSpace == ColorSpace::HDR10_ST2084) {
            if (m_config.preferredBitDepth >= 10 && fmt.format == SwapFmt::A2B10G10R10_UNORM) {
                result.selected = fmt;
                break;
            }
            if (fmt.format == SwapFmt::R16G16B16A16_SFLOAT) {
                result.selected = fmt;
                // Keep looking for 10-bit
            }
        }
    }

    result.formatName = FormatToString(result.selected.format);
    result.colorSpaceName = ColorSpaceToString(result.selected.colorSpace);
    return result;
}

SwapchainFormatResult SwapchainFormatSelector::SelectScRGB() const {
    SwapchainFormatResult result{};
    result.activeMode = HDRMode::HDR_scRGB;
    result.isFallback = false;

    for (const auto& fmt : m_available) {
        if (fmt.colorSpace == ColorSpace::EXTENDED_SRGB_LINEAR &&
            fmt.format == SwapFmt::R16G16B16A16_SFLOAT) {
            result.selected = fmt;
            break;
        }
    }

    result.formatName = FormatToString(result.selected.format);
    result.colorSpaceName = ColorSpaceToString(result.selected.colorSpace);
    return result;
}

SwapchainFormatResult SwapchainFormatSelector::SelectDisplayP3() const {
    SwapchainFormatResult result{};
    result.activeMode = HDRMode::DisplayP3;
    result.isFallback = false;

    for (const auto& fmt : m_available) {
        if (fmt.colorSpace == ColorSpace::DISPLAY_P3_NONLINEAR) {
            result.selected = fmt;
            break;
        }
    }

    result.formatName = FormatToString(result.selected.format);
    result.colorSpaceName = ColorSpaceToString(result.selected.colorSpace);
    return result;
}

SwapchainFormatResult SwapchainFormatSelector::SelectSDR() const {
    SwapchainFormatResult result{};
    result.activeMode = HDRMode::SDR;
    result.isFallback = false;

    // Prefer B8G8R8A8_SRGB, then R8G8B8A8_SRGB, then UNORM variants
    const u32 preferred[] = {
        SwapFmt::B8G8R8A8_SRGB, SwapFmt::R8G8B8A8_SRGB,
        SwapFmt::B8G8R8A8_UNORM, SwapFmt::R8G8B8A8_UNORM
    };

    for (u32 pref : preferred) {
        for (const auto& fmt : m_available) {
            if (fmt.format == pref && fmt.colorSpace == ColorSpace::SRGB_NONLINEAR) {
                result.selected = fmt;
                result.formatName = FormatToString(result.selected.format);
                result.colorSpaceName = ColorSpaceToString(result.selected.colorSpace);
                return result;
            }
        }
    }

    // Fallback: first sRGB format
    for (const auto& fmt : m_available) {
        if (fmt.colorSpace == ColorSpace::SRGB_NONLINEAR) {
            result.selected = fmt;
            break;
        }
    }

    // Last resort: first available
    if (result.selected.format == 0 && !m_available.empty()) {
        result.selected = m_available[0];
    }

    result.formatName = FormatToString(result.selected.format);
    result.colorSpaceName = ColorSpaceToString(result.selected.colorSpace);
    return result;
}

std::string SwapchainFormatSelector::FormatToString(u32 format) const {
    switch (format) {
        case SwapFmt::B8G8R8A8_SRGB:       return "B8G8R8A8_SRGB";
        case SwapFmt::B8G8R8A8_UNORM:      return "B8G8R8A8_UNORM";
        case SwapFmt::R8G8B8A8_SRGB:       return "R8G8B8A8_SRGB";
        case SwapFmt::R8G8B8A8_UNORM:      return "R8G8B8A8_UNORM";
        case SwapFmt::A2B10G10R10_UNORM:   return "A2B10G10R10_UNORM";
        case SwapFmt::R16G16B16A16_SFLOAT: return "R16G16B16A16_SFLOAT";
        default:                            return "UNKNOWN(" + std::to_string(format) + ")";
    }
}

std::string SwapchainFormatSelector::ColorSpaceToString(u32 colorSpace) const {
    switch (colorSpace) {
        case ColorSpace::SRGB_NONLINEAR:        return "SRGB_NONLINEAR";
        case ColorSpace::HDR10_ST2084:           return "HDR10_ST2084";
        case ColorSpace::EXTENDED_SRGB_LINEAR:   return "EXTENDED_SRGB_LINEAR";
        case ColorSpace::DISPLAY_P3_NONLINEAR:   return "DISPLAY_P3_NONLINEAR";
        case ColorSpace::BT2020_LINEAR:          return "BT2020_LINEAR";
        default:                                  return "UNKNOWN(" + std::to_string(colorSpace) + ")";
    }
}

} // namespace nge::rhi::vulkan
