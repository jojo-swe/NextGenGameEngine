#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_format_utils.h"

using namespace nge;
using namespace nge::rhi;
using namespace nge::rhi::FormatUtils;

// ─── Basic Format Properties ─────────────────────────────────────────────

TEST(FormatUtils, UncompressedBytesPerPixel) {
    EXPECT_EQ(GetBytesPerPixel(Format::R8_UNORM), 1u);
    EXPECT_EQ(GetBytesPerPixel(Format::RG8_UNORM), 2u);
    EXPECT_EQ(GetBytesPerPixel(Format::RGBA8_UNORM), 4u);
    EXPECT_EQ(GetBytesPerPixel(Format::RGBA16_FLOAT), 8u);
    EXPECT_EQ(GetBytesPerPixel(Format::RGBA32_FLOAT), 16u);
    EXPECT_EQ(GetBytesPerPixel(Format::R11G11B10_FLOAT), 4u);
}

TEST(FormatUtils, CompressedBlockSize) {
    EXPECT_EQ(GetBlockSize(Format::BC1_UNORM), 8u);
    EXPECT_EQ(GetBlockSize(Format::BC3_UNORM), 16u);
    EXPECT_EQ(GetBlockSize(Format::BC5_UNORM), 16u);
    EXPECT_EQ(GetBlockSize(Format::BC7_UNORM), 16u);
    EXPECT_EQ(GetBlockWidth(Format::BC1_UNORM), 4u);
    EXPECT_EQ(GetBlockHeight(Format::BC1_UNORM), 4u);
}

TEST(FormatUtils, ChannelCount) {
    EXPECT_EQ(GetChannelCount(Format::R8_UNORM), 1u);
    EXPECT_EQ(GetChannelCount(Format::RG8_UNORM), 2u);
    EXPECT_EQ(GetChannelCount(Format::R11G11B10_FLOAT), 3u);
    EXPECT_EQ(GetChannelCount(Format::RGBA8_UNORM), 4u);
    EXPECT_EQ(GetChannelCount(Format::BC7_UNORM), 4u);
}

// ─── Format Classification ───────────────────────────────────────────────

TEST(FormatUtils, CompressedDetection) {
    EXPECT_TRUE(IsCompressed(Format::BC1_UNORM));
    EXPECT_TRUE(IsCompressed(Format::BC7_SRGB));
    EXPECT_FALSE(IsCompressed(Format::RGBA8_UNORM));
    EXPECT_FALSE(IsCompressed(Format::D32_FLOAT));
}

TEST(FormatUtils, DepthDetection) {
    EXPECT_TRUE(IsDepthFormat(Format::D32_FLOAT));
    EXPECT_TRUE(IsDepthFormat(Format::D16_UNORM));
    EXPECT_TRUE(IsDepthFormat(Format::D24_UNORM_S8_UINT));
    EXPECT_FALSE(IsDepthFormat(Format::RGBA8_UNORM));
}

TEST(FormatUtils, DepthStencilDetection) {
    EXPECT_TRUE(IsDepthStencilFormat(Format::D24_UNORM_S8_UINT));
    EXPECT_TRUE(IsDepthStencilFormat(Format::D32_FLOAT_S8_UINT));
    EXPECT_FALSE(IsDepthStencilFormat(Format::D32_FLOAT));
    EXPECT_FALSE(IsDepthStencilFormat(Format::RGBA8_UNORM));
}

TEST(FormatUtils, SRGBDetection) {
    EXPECT_TRUE(IsSRGBFormat(Format::RGBA8_SRGB));
    EXPECT_TRUE(IsSRGBFormat(Format::BGRA8_SRGB));
    EXPECT_TRUE(IsSRGBFormat(Format::BC1_SRGB));
    EXPECT_TRUE(IsSRGBFormat(Format::BC7_SRGB));
    EXPECT_FALSE(IsSRGBFormat(Format::RGBA8_UNORM));
    EXPECT_FALSE(IsSRGBFormat(Format::RGBA16_FLOAT));
}

TEST(FormatUtils, FloatDetection) {
    EXPECT_TRUE(IsFloatFormat(Format::R16_FLOAT));
    EXPECT_TRUE(IsFloatFormat(Format::RGBA16_FLOAT));
    EXPECT_TRUE(IsFloatFormat(Format::R32_FLOAT));
    EXPECT_TRUE(IsFloatFormat(Format::D32_FLOAT));
    EXPECT_FALSE(IsFloatFormat(Format::RGBA8_UNORM));
    EXPECT_FALSE(IsFloatFormat(Format::R8_UINT));
}

TEST(FormatUtils, HasAlpha) {
    EXPECT_TRUE(HasAlpha(Format::RGBA8_UNORM));
    EXPECT_TRUE(HasAlpha(Format::RGBA16_FLOAT));
    EXPECT_TRUE(HasAlpha(Format::BGRA8_SRGB));
    EXPECT_FALSE(HasAlpha(Format::R8_UNORM));
    EXPECT_FALSE(HasAlpha(Format::RG16_FLOAT));
    EXPECT_FALSE(HasAlpha(Format::R11G11B10_FLOAT));
}

// ─── Format Conversion ──────────────────────────────────────────────────

TEST(FormatUtils, ToSRGBConversion) {
    EXPECT_EQ(ToSRGB(Format::RGBA8_UNORM), Format::RGBA8_SRGB);
    EXPECT_EQ(ToSRGB(Format::BGRA8_UNORM), Format::BGRA8_SRGB);
    EXPECT_EQ(ToSRGB(Format::BC7_UNORM), Format::BC7_SRGB);
    // Already sRGB or no sRGB variant
    EXPECT_EQ(ToSRGB(Format::RGBA8_SRGB), Format::RGBA8_SRGB);
    EXPECT_EQ(ToSRGB(Format::RGBA16_FLOAT), Format::RGBA16_FLOAT);
}

TEST(FormatUtils, ToLinearConversion) {
    EXPECT_EQ(ToLinear(Format::RGBA8_SRGB), Format::RGBA8_UNORM);
    EXPECT_EQ(ToLinear(Format::BGRA8_SRGB), Format::BGRA8_UNORM);
    EXPECT_EQ(ToLinear(Format::BC7_SRGB), Format::BC7_UNORM);
    EXPECT_EQ(ToLinear(Format::RGBA8_UNORM), Format::RGBA8_UNORM);
}

// ─── Mip Calculations ───────────────────────────────────────────────────

TEST(FormatUtils, MipDimension) {
    EXPECT_EQ(CalculateMipDimension(1024, 0), 1024u);
    EXPECT_EQ(CalculateMipDimension(1024, 1), 512u);
    EXPECT_EQ(CalculateMipDimension(1024, 2), 256u);
    EXPECT_EQ(CalculateMipDimension(1024, 10), 1u);
    EXPECT_EQ(CalculateMipDimension(1024, 20), 1u); // Clamped to 1
    EXPECT_EQ(CalculateMipDimension(1, 0), 1u);
}

TEST(FormatUtils, MipCount) {
    EXPECT_EQ(CalculateMipCount(1, 1), 1u);
    EXPECT_EQ(CalculateMipCount(2, 2), 2u);
    EXPECT_EQ(CalculateMipCount(256, 256), 9u);
    EXPECT_EQ(CalculateMipCount(1024, 1024), 11u);
    EXPECT_EQ(CalculateMipCount(1920, 1080), 11u); // max(1920,1080)=1920
}

TEST(FormatUtils, TextureSizeUncompressed) {
    // 256x256 RGBA8, 1 mip = 256*256*4 = 262144
    EXPECT_EQ(CalculateTextureSize(Format::RGBA8_UNORM, 256, 256, 1, 1), 262144u);

    // 1x1 R8 = 1 byte
    EXPECT_EQ(CalculateTextureSize(Format::R8_UNORM, 1, 1, 1, 1), 1u);
}

TEST(FormatUtils, TextureSizeCompressed) {
    // 256x256 BC1: (256/4)*(256/4)*8 = 64*64*8 = 32768
    EXPECT_EQ(CalculateMipSize(Format::BC1_UNORM, 256, 256, 0), 32768u);

    // 4x4 BC1: 1 block * 8 bytes
    EXPECT_EQ(CalculateMipSize(Format::BC1_UNORM, 4, 4, 0), 8u);
}

// ─── Aspect Flags ────────────────────────────────────────────────────────

TEST(FormatUtils, AspectFlags) {
    EXPECT_EQ(GetAspectFlags(Format::RGBA8_UNORM), 0x1u);      // COLOR
    EXPECT_EQ(GetAspectFlags(Format::D32_FLOAT), 0x2u);        // DEPTH
    EXPECT_EQ(GetAspectFlags(Format::D24_UNORM_S8_UINT), 0x3u);// DEPTH | STENCIL
}
