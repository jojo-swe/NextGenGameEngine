#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <mutex>

namespace nge::rhi::vulkan {

// ─── Vulkan Swapchain Format Selector ────────────────────────────────────
// Negotiates optimal swapchain surface format and color space based on
// display capabilities, user HDR preference, and Vulkan surface support.
//
// Supports:
//   - sRGB (SDR default)
//   - HDR10 (ST2084 PQ, Rec.2020)
//   - scRGB (linear, extended range)
//   - Display-P3 (wide gamut SDR)
//
// Queries VkSurfaceFormatKHR from the physical device and selects
// the best match based on priority and display capability.

enum class HDRMode : u8 {
    SDR,         // Standard dynamic range (sRGB)
    HDR10,       // HDR10 (PQ transfer, Rec.2020 gamut)
    HDR_scRGB,   // scRGB linear (FP16, extended range)
    DisplayP3,   // Wide gamut SDR (Display-P3)
    Auto,        // Auto-detect best available
};

// Mirrors VkFormat values
namespace SwapFmt {
    constexpr u32 B8G8R8A8_SRGB       = 50;
    constexpr u32 B8G8R8A8_UNORM      = 44;
    constexpr u32 R8G8B8A8_SRGB       = 43;
    constexpr u32 R8G8B8A8_UNORM      = 37;
    constexpr u32 A2B10G10R10_UNORM   = 64;
    constexpr u32 R16G16B16A16_SFLOAT = 97;
}

// Mirrors VkColorSpaceKHR values
namespace ColorSpace {
    constexpr u32 SRGB_NONLINEAR     = 0;
    constexpr u32 HDR10_ST2084       = 1000104008;
    constexpr u32 EXTENDED_SRGB_LINEAR = 1000104014;
    constexpr u32 DISPLAY_P3_NONLINEAR = 1000104001;
    constexpr u32 BT2020_LINEAR      = 1000104007;
}

struct SurfaceFormat {
    u32 format;
    u32 colorSpace;
};

struct SwapchainFormatConfig {
    HDRMode preferredMode = HDRMode::Auto;
    bool    allowFallback = true;       // Fall back to SDR if HDR unavailable
    u32     preferredBitDepth = 10;     // 8 or 10 for HDR10
};

struct SwapchainFormatResult {
    SurfaceFormat selected;
    HDRMode       activeMode;
    bool          isFallback;
    std::string   formatName;
    std::string   colorSpaceName;
    f32           maxLuminance;     // Display max nits (0 if unknown)
    f32           minLuminance;     // Display min nits
};

struct SwapchainFormatStats {
    u32 availableFormats;
    u32 hdrFormatsAvailable;
    bool hdr10Supported;
    bool scRGBSupported;
    bool displayP3Supported;
};

class SwapchainFormatSelector {
public:
    bool Init(const SwapchainFormatConfig& config = {});
    void Shutdown();

    // Set available surface formats (from vkGetPhysicalDeviceSurfaceFormatsKHR)
    void SetAvailableFormats(const std::vector<SurfaceFormat>& formats);

    // Set display HDR capabilities (from VkHdrMetadataEXT or OS query)
    void SetDisplayCapabilities(f32 maxLuminance, f32 minLuminance, f32 maxFrameAvgLuminance);

    // Select the best format based on config and available formats
    SwapchainFormatResult Select();

    // Force a specific format (override auto-selection)
    void ForceFormat(u32 format, u32 colorSpace);

    // Check if a specific mode is available
    bool IsModeAvailable(HDRMode mode) const;

    SwapchainFormatStats GetStats() const;

private:
    SwapchainFormatResult SelectHDR10() const;
    SwapchainFormatResult SelectScRGB() const;
    SwapchainFormatResult SelectDisplayP3() const;
    SwapchainFormatResult SelectSDR() const;
    std::string FormatToString(u32 format) const;
    std::string ColorSpaceToString(u32 colorSpace) const;

    SwapchainFormatConfig m_config;
    std::vector<SurfaceFormat> m_available;
    SurfaceFormat m_forced = {0, 0};
    bool m_hasForced = false;

    f32 m_maxLuminance = 0.0f;
    f32 m_minLuminance = 0.0f;
    f32 m_maxFrameAvgLuminance = 0.0f;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi::vulkan
