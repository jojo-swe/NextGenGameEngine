#include "engine/rhi/common/rhi_format_utils.h"
#include <algorithm>
#include <cmath>

namespace nge::rhi::FormatUtils {

// ─── Format Table ────────────────────────────────────────────────────────

static const FormatInfo s_formatTable[] = {
    // format                    bw bh bpb ch comp  srgb  depth stncl float sign  name
    { Format::Undefined,          1, 1, 0,  0, false, false, false, false, false, false, "Undefined" },
    { Format::R8_UNORM,           1, 1, 1,  1, false, false, false, false, false, false, "R8_UNORM" },
    { Format::R8_SNORM,           1, 1, 1,  1, false, false, false, false, false, true,  "R8_SNORM" },
    { Format::R8_UINT,            1, 1, 1,  1, false, false, false, false, false, false, "R8_UINT" },
    { Format::R8_SINT,            1, 1, 1,  1, false, false, false, false, false, true,  "R8_SINT" },
    { Format::RG8_UNORM,          1, 1, 2,  2, false, false, false, false, false, false, "RG8_UNORM" },
    { Format::RGBA8_UNORM,        1, 1, 4,  4, false, false, false, false, false, false, "RGBA8_UNORM" },
    { Format::RGBA8_SRGB,         1, 1, 4,  4, false, true,  false, false, false, false, "RGBA8_SRGB" },
    { Format::BGRA8_UNORM,        1, 1, 4,  4, false, false, false, false, false, false, "BGRA8_UNORM" },
    { Format::BGRA8_SRGB,         1, 1, 4,  4, false, true,  false, false, false, false, "BGRA8_SRGB" },
    { Format::R16_FLOAT,          1, 1, 2,  1, false, false, false, false, true,  true,  "R16_FLOAT" },
    { Format::R16_UINT,           1, 1, 2,  1, false, false, false, false, false, false, "R16_UINT" },
    { Format::RG16_FLOAT,         1, 1, 4,  2, false, false, false, false, true,  true,  "RG16_FLOAT" },
    { Format::RGBA16_FLOAT,       1, 1, 8,  4, false, false, false, false, true,  true,  "RGBA16_FLOAT" },
    { Format::R32_FLOAT,          1, 1, 4,  1, false, false, false, false, true,  true,  "R32_FLOAT" },
    { Format::R32_UINT,           1, 1, 4,  1, false, false, false, false, false, false, "R32_UINT" },
    { Format::RG32_FLOAT,         1, 1, 8,  2, false, false, false, false, true,  true,  "RG32_FLOAT" },
    { Format::RGBA32_FLOAT,       1, 1, 16, 4, false, false, false, false, true,  true,  "RGBA32_FLOAT" },
    { Format::R11G11B10_FLOAT,    1, 1, 4,  3, false, false, false, false, true,  false, "R11G11B10_FLOAT" },
    { Format::RGB10A2_UNORM,      1, 1, 4,  4, false, false, false, false, false, false, "RGB10A2_UNORM" },
    { Format::D16_UNORM,          1, 1, 2,  1, false, false, true,  false, false, false, "D16_UNORM" },
    { Format::D32_FLOAT,          1, 1, 4,  1, false, false, true,  false, true,  true,  "D32_FLOAT" },
    { Format::D24_UNORM_S8_UINT,  1, 1, 4,  2, false, false, true,  true,  false, false, "D24_UNORM_S8_UINT" },
    { Format::D32_FLOAT_S8_UINT,  1, 1, 8,  2, false, false, true,  true,  true,  true,  "D32_FLOAT_S8_UINT" },
    { Format::BC1_UNORM,          4, 4, 8,  4, true,  false, false, false, false, false, "BC1_UNORM" },
    { Format::BC1_SRGB,           4, 4, 8,  4, true,  true,  false, false, false, false, "BC1_SRGB" },
    { Format::BC2_UNORM,          4, 4, 16, 4, true,  false, false, false, false, false, "BC2_UNORM" },
    { Format::BC2_SRGB,           4, 4, 16, 4, true,  true,  false, false, false, false, "BC2_SRGB" },
    { Format::BC3_UNORM,          4, 4, 16, 4, true,  false, false, false, false, false, "BC3_UNORM" },
    { Format::BC3_SRGB,           4, 4, 16, 4, true,  true,  false, false, false, false, "BC3_SRGB" },
    { Format::BC4_UNORM,          4, 4, 8,  1, true,  false, false, false, false, false, "BC4_UNORM" },
    { Format::BC4_SNORM,          4, 4, 8,  1, true,  false, false, false, false, true,  "BC4_SNORM" },
    { Format::BC5_UNORM,          4, 4, 16, 2, true,  false, false, false, false, false, "BC5_UNORM" },
    { Format::BC5_SNORM,          4, 4, 16, 2, true,  false, false, false, false, true,  "BC5_SNORM" },
    { Format::BC6H_UFLOAT,        4, 4, 16, 3, true,  false, false, false, true,  false, "BC6H_UFLOAT" },
    { Format::BC6H_SFLOAT,        4, 4, 16, 3, true,  false, false, false, true,  true,  "BC6H_SFLOAT" },
    { Format::BC7_UNORM,          4, 4, 16, 4, true,  false, false, false, false, false, "BC7_UNORM" },
    { Format::BC7_SRGB,           4, 4, 16, 4, true,  true,  false, false, false, false, "BC7_SRGB" },
};

static constexpr u32 FORMAT_COUNT = sizeof(s_formatTable) / sizeof(s_formatTable[0]);

const FormatInfo& GetFormatInfo(Format format) {
    u32 idx = static_cast<u32>(format);
    if (idx < FORMAT_COUNT) return s_formatTable[idx];
    return s_formatTable[0]; // Undefined
}

u32 GetBytesPerPixel(Format format) {
    const auto& info = GetFormatInfo(format);
    if (info.isCompressed) return 0; // Use GetBlockSize for compressed
    return info.bytesPerBlock;
}

u32 GetBlockSize(Format format)  { return GetFormatInfo(format).bytesPerBlock; }
u32 GetBlockWidth(Format format) { return GetFormatInfo(format).blockWidth; }
u32 GetBlockHeight(Format format){ return GetFormatInfo(format).blockHeight; }
u32 GetChannelCount(Format format){ return GetFormatInfo(format).channelCount; }

u32 CalculateMipDimension(u32 baseDimension, u32 mipLevel) {
    return std::max(1u, baseDimension >> mipLevel);
}

u32 CalculateMipCount(u32 width, u32 height) {
    u32 maxDim = std::max(width, height);
    return static_cast<u32>(std::floor(std::log2(static_cast<f64>(maxDim)))) + 1;
}

u64 CalculateMipSize(Format format, u32 width, u32 height, u32 mipLevel) {
    u32 mipW = CalculateMipDimension(width, mipLevel);
    u32 mipH = CalculateMipDimension(height, mipLevel);

    const auto& info = GetFormatInfo(format);
    if (info.isCompressed) {
        u32 blocksX = (mipW + info.blockWidth - 1) / info.blockWidth;
        u32 blocksY = (mipH + info.blockHeight - 1) / info.blockHeight;
        return static_cast<u64>(blocksX) * blocksY * info.bytesPerBlock;
    }
    return static_cast<u64>(mipW) * mipH * info.bytesPerBlock;
}

u64 CalculateTextureSize(Format format, u32 width, u32 height, u32 depth, u32 mipLevels) {
    u64 total = 0;
    for (u32 mip = 0; mip < mipLevels; ++mip) {
        u32 mipDepth = std::max(1u, depth >> mip);
        total += CalculateMipSize(format, width, height, mip) * mipDepth;
    }
    return total;
}

bool IsCompressed(Format format)      { return GetFormatInfo(format).isCompressed; }
bool IsDepthFormat(Format format)     { return GetFormatInfo(format).isDepth; }
bool IsStencilFormat(Format format)   { return GetFormatInfo(format).isStencil; }
bool IsDepthStencilFormat(Format format) { return GetFormatInfo(format).isDepth && GetFormatInfo(format).isStencil; }
bool IsSRGBFormat(Format format)      { return GetFormatInfo(format).isSRGB; }
bool IsFloatFormat(Format format)     { return GetFormatInfo(format).isFloat; }
bool IsSignedFormat(Format format)    { return GetFormatInfo(format).isSigned; }

bool IsNormalized(Format format) {
    const auto& info = GetFormatInfo(format);
    return !info.isFloat && !info.isCompressed && !info.isDepth && info.bytesPerBlock > 0;
}

bool HasAlpha(Format format) {
    return GetFormatInfo(format).channelCount == 4;
}

Format ToSRGB(Format format) {
    switch (format) {
        case Format::RGBA8_UNORM: return Format::RGBA8_SRGB;
        case Format::BGRA8_UNORM: return Format::BGRA8_SRGB;
        case Format::BC1_UNORM:  return Format::BC1_SRGB;
        case Format::BC2_UNORM:  return Format::BC2_SRGB;
        case Format::BC3_UNORM:  return Format::BC3_SRGB;
        case Format::BC7_UNORM:  return Format::BC7_SRGB;
        default: return format;
    }
}

Format ToLinear(Format format) {
    switch (format) {
        case Format::RGBA8_SRGB:  return Format::RGBA8_UNORM;
        case Format::BGRA8_SRGB:  return Format::BGRA8_UNORM;
        case Format::BC1_SRGB:   return Format::BC1_UNORM;
        case Format::BC2_SRGB:   return Format::BC2_UNORM;
        case Format::BC3_SRGB:   return Format::BC3_UNORM;
        case Format::BC7_SRGB:   return Format::BC7_UNORM;
        default: return format;
    }
}

Format GetDepthFormat(Format depthStencil) {
    switch (depthStencil) {
        case Format::D24_UNORM_S8_UINT: return Format::D16_UNORM; // Approximate
        case Format::D32_FLOAT_S8_UINT: return Format::D32_FLOAT;
        default: return depthStencil;
    }
}

u32 GetAspectFlags(Format format) {
    const auto& info = GetFormatInfo(format);
    if (info.isDepth && info.isStencil) return 0x3; // DEPTH | STENCIL
    if (info.isDepth)                   return 0x2; // DEPTH
    if (info.isStencil)                 return 0x4; // STENCIL
    return 0x1; // COLOR
}

} // namespace nge::rhi::FormatUtils
