#include <gtest/gtest.h>
#include "engine/rhi/vulkan/vk_swapchain_format_selector.h"

using namespace nge::rhi::vulkan;

// ─── Swapchain Format Selector Tests ─────────────────────────────────────

TEST(SwapchainFormatSelector, InitAndShutdown) {
    SwapchainFormatSelector selector;
    EXPECT_TRUE(selector.Init());

    auto stats = selector.GetStats();
    EXPECT_EQ(stats.availableFormats, 0u);

    selector.Shutdown();
}

TEST(SwapchainFormatSelector, SDRSelection) {
    SwapchainFormatSelector selector;
    SwapchainFormatConfig config;
    config.preferredMode = HDRMode::SDR;
    selector.Init(config);

    std::vector<SurfaceFormat> formats = {
        {SwapFmt::B8G8R8A8_SRGB, ColorSpace::SRGB_NONLINEAR},
        {SwapFmt::R8G8B8A8_SRGB, ColorSpace::SRGB_NONLINEAR},
    };
    selector.SetAvailableFormats(formats);

    auto result = selector.Select();
    EXPECT_EQ(result.selected.format, SwapFmt::B8G8R8A8_SRGB);
    EXPECT_EQ(result.selected.colorSpace, ColorSpace::SRGB_NONLINEAR);
    EXPECT_EQ(result.activeMode, HDRMode::SDR);
    EXPECT_FALSE(result.isFallback);
    EXPECT_FALSE(result.formatName.empty());

    selector.Shutdown();
}

TEST(SwapchainFormatSelector, HDR10Selection) {
    SwapchainFormatSelector selector;
    SwapchainFormatConfig config;
    config.preferredMode = HDRMode::HDR10;
    config.preferredBitDepth = 10;
    selector.Init(config);

    std::vector<SurfaceFormat> formats = {
        {SwapFmt::B8G8R8A8_SRGB, ColorSpace::SRGB_NONLINEAR},
        {SwapFmt::A2B10G10R10_UNORM, ColorSpace::HDR10_ST2084},
        {SwapFmt::R16G16B16A16_SFLOAT, ColorSpace::HDR10_ST2084},
    };
    selector.SetAvailableFormats(formats);

    auto result = selector.Select();
    EXPECT_EQ(result.selected.format, SwapFmt::A2B10G10R10_UNORM);
    EXPECT_EQ(result.selected.colorSpace, ColorSpace::HDR10_ST2084);
    EXPECT_EQ(result.activeMode, HDRMode::HDR10);
    EXPECT_FALSE(result.isFallback);

    selector.Shutdown();
}

TEST(SwapchainFormatSelector, HDR10FallbackToSDR) {
    SwapchainFormatSelector selector;
    SwapchainFormatConfig config;
    config.preferredMode = HDRMode::HDR10;
    config.allowFallback = true;
    selector.Init(config);

    // No HDR10 formats available
    std::vector<SurfaceFormat> formats = {
        {SwapFmt::B8G8R8A8_SRGB, ColorSpace::SRGB_NONLINEAR},
    };
    selector.SetAvailableFormats(formats);

    auto result = selector.Select();
    EXPECT_EQ(result.selected.format, SwapFmt::B8G8R8A8_SRGB);
    EXPECT_EQ(result.activeMode, HDRMode::SDR);
    EXPECT_TRUE(result.isFallback);

    selector.Shutdown();
}

TEST(SwapchainFormatSelector, ScRGBSelection) {
    SwapchainFormatSelector selector;
    SwapchainFormatConfig config;
    config.preferredMode = HDRMode::HDR_scRGB;
    selector.Init(config);

    std::vector<SurfaceFormat> formats = {
        {SwapFmt::B8G8R8A8_SRGB, ColorSpace::SRGB_NONLINEAR},
        {SwapFmt::R16G16B16A16_SFLOAT, ColorSpace::EXTENDED_SRGB_LINEAR},
    };
    selector.SetAvailableFormats(formats);

    auto result = selector.Select();
    EXPECT_EQ(result.selected.format, SwapFmt::R16G16B16A16_SFLOAT);
    EXPECT_EQ(result.selected.colorSpace, ColorSpace::EXTENDED_SRGB_LINEAR);
    EXPECT_EQ(result.activeMode, HDRMode::HDR_scRGB);

    selector.Shutdown();
}

TEST(SwapchainFormatSelector, DisplayP3Selection) {
    SwapchainFormatSelector selector;
    SwapchainFormatConfig config;
    config.preferredMode = HDRMode::DisplayP3;
    selector.Init(config);

    std::vector<SurfaceFormat> formats = {
        {SwapFmt::B8G8R8A8_SRGB, ColorSpace::SRGB_NONLINEAR},
        {SwapFmt::R8G8B8A8_UNORM, ColorSpace::DISPLAY_P3_NONLINEAR},
    };
    selector.SetAvailableFormats(formats);

    auto result = selector.Select();
    EXPECT_EQ(result.selected.colorSpace, ColorSpace::DISPLAY_P3_NONLINEAR);
    EXPECT_EQ(result.activeMode, HDRMode::DisplayP3);

    selector.Shutdown();
}

TEST(SwapchainFormatSelector, AutoSelectsHDR10WhenAvailable) {
    SwapchainFormatSelector selector;
    SwapchainFormatConfig config;
    config.preferredMode = HDRMode::Auto;
    selector.Init(config);

    std::vector<SurfaceFormat> formats = {
        {SwapFmt::B8G8R8A8_SRGB, ColorSpace::SRGB_NONLINEAR},
        {SwapFmt::A2B10G10R10_UNORM, ColorSpace::HDR10_ST2084},
    };
    selector.SetAvailableFormats(formats);
    selector.SetDisplayCapabilities(1000.0f, 0.001f, 500.0f); // HDR display

    auto result = selector.Select();
    EXPECT_EQ(result.activeMode, HDRMode::HDR10);
    EXPECT_EQ(result.maxLuminance, 1000.0f);

    selector.Shutdown();
}

TEST(SwapchainFormatSelector, AutoFallsBackToSDRLowLuminance) {
    SwapchainFormatSelector selector;
    SwapchainFormatConfig config;
    config.preferredMode = HDRMode::Auto;
    selector.Init(config);

    std::vector<SurfaceFormat> formats = {
        {SwapFmt::B8G8R8A8_SRGB, ColorSpace::SRGB_NONLINEAR},
        {SwapFmt::A2B10G10R10_UNORM, ColorSpace::HDR10_ST2084},
    };
    selector.SetAvailableFormats(formats);
    selector.SetDisplayCapabilities(300.0f, 0.1f, 200.0f); // SDR display (low nits)

    auto result = selector.Select();
    // maxLuminance < 400 so Auto won't pick HDR10
    EXPECT_EQ(result.activeMode, HDRMode::SDR);

    selector.Shutdown();
}

TEST(SwapchainFormatSelector, ForceFormatOverride) {
    SwapchainFormatSelector selector;
    selector.Init();

    std::vector<SurfaceFormat> formats = {
        {SwapFmt::B8G8R8A8_SRGB, ColorSpace::SRGB_NONLINEAR},
        {SwapFmt::A2B10G10R10_UNORM, ColorSpace::HDR10_ST2084},
    };
    selector.SetAvailableFormats(formats);

    selector.ForceFormat(SwapFmt::R8G8B8A8_UNORM, ColorSpace::SRGB_NONLINEAR);

    auto result = selector.Select();
    EXPECT_EQ(result.selected.format, SwapFmt::R8G8B8A8_UNORM);
    EXPECT_EQ(result.selected.colorSpace, ColorSpace::SRGB_NONLINEAR);
    EXPECT_FALSE(result.isFallback);

    selector.Shutdown();
}

TEST(SwapchainFormatSelector, IsModeAvailable) {
    SwapchainFormatSelector selector;
    selector.Init();

    std::vector<SurfaceFormat> formats = {
        {SwapFmt::B8G8R8A8_SRGB, ColorSpace::SRGB_NONLINEAR},
        {SwapFmt::A2B10G10R10_UNORM, ColorSpace::HDR10_ST2084},
    };
    selector.SetAvailableFormats(formats);

    EXPECT_TRUE(selector.IsModeAvailable(HDRMode::SDR));
    EXPECT_TRUE(selector.IsModeAvailable(HDRMode::HDR10));
    EXPECT_FALSE(selector.IsModeAvailable(HDRMode::HDR_scRGB));
    EXPECT_FALSE(selector.IsModeAvailable(HDRMode::DisplayP3));

    selector.Shutdown();
}

TEST(SwapchainFormatSelector, StatsTracking) {
    SwapchainFormatSelector selector;
    selector.Init();

    std::vector<SurfaceFormat> formats = {
        {SwapFmt::B8G8R8A8_SRGB, ColorSpace::SRGB_NONLINEAR},
        {SwapFmt::A2B10G10R10_UNORM, ColorSpace::HDR10_ST2084},
        {SwapFmt::R16G16B16A16_SFLOAT, ColorSpace::EXTENDED_SRGB_LINEAR},
        {SwapFmt::R8G8B8A8_UNORM, ColorSpace::DISPLAY_P3_NONLINEAR},
    };
    selector.SetAvailableFormats(formats);

    auto stats = selector.GetStats();
    EXPECT_EQ(stats.availableFormats, 4u);
    EXPECT_EQ(stats.hdrFormatsAvailable, 3u);
    EXPECT_TRUE(stats.hdr10Supported);
    EXPECT_TRUE(stats.scRGBSupported);
    EXPECT_TRUE(stats.displayP3Supported);

    selector.Shutdown();
}

TEST(SwapchainFormatSelector, EmptyFormatsStillSelects) {
    SwapchainFormatSelector selector;
    selector.Init();

    // No formats available
    auto result = selector.Select();
    EXPECT_EQ(result.selected.format, 0u); // No format
    EXPECT_EQ(result.activeMode, HDRMode::SDR);

    selector.Shutdown();
}

TEST(SwapchainFormatSelector, PreferUNORMFallback) {
    SwapchainFormatSelector selector;
    SwapchainFormatConfig config;
    config.preferredMode = HDRMode::SDR;
    selector.Init(config);

    // Only UNORM available (no SRGB)
    std::vector<SurfaceFormat> formats = {
        {SwapFmt::B8G8R8A8_UNORM, ColorSpace::SRGB_NONLINEAR},
    };
    selector.SetAvailableFormats(formats);

    auto result = selector.Select();
    EXPECT_EQ(result.selected.format, SwapFmt::B8G8R8A8_UNORM);

    selector.Shutdown();
}
