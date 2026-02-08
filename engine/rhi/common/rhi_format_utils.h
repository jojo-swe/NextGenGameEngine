#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_types.h"

namespace nge::rhi {

// ─── Texture Format Utilities ────────────────────────────────────────────
// Helpers for querying format properties: block size, channel count,
// aspect flags, sRGB variants, compressed format detection, etc.

struct FormatInfo {
    Format  format;
    u32     blockWidth;       // 1 for uncompressed, 4 for BC formats
    u32     blockHeight;
    u32     bytesPerBlock;    // Bytes per texel (uncompressed) or per block (compressed)
    u32     channelCount;
    bool    isCompressed;
    bool    isSRGB;
    bool    isDepth;
    bool    isStencil;
    bool    isFloat;
    bool    isSigned;
    const char* name;
};

namespace FormatUtils {

// Get full format info
const FormatInfo& GetFormatInfo(Format format);

// Size queries
u32  GetBytesPerPixel(Format format);
u32  GetBlockSize(Format format);         // Bytes per block (or per pixel if uncompressed)
u32  GetBlockWidth(Format format);        // 1 for uncompressed
u32  GetBlockHeight(Format format);
u32  GetChannelCount(Format format);

// Calculate texture size in bytes
u64  CalculateTextureSize(Format format, u32 width, u32 height, u32 depth = 1, u32 mipLevels = 1);
u64  CalculateMipSize(Format format, u32 width, u32 height, u32 mipLevel);
u32  CalculateMipDimension(u32 baseDimension, u32 mipLevel);
u32  CalculateMipCount(u32 width, u32 height);

// Format classification
bool IsCompressed(Format format);
bool IsDepthFormat(Format format);
bool IsStencilFormat(Format format);
bool IsDepthStencilFormat(Format format);
bool IsSRGBFormat(Format format);
bool IsFloatFormat(Format format);
bool IsSignedFormat(Format format);
bool IsNormalized(Format format);
bool HasAlpha(Format format);

// Format conversion
Format ToSRGB(Format format);             // Get sRGB variant (or same if none)
Format ToLinear(Format format);            // Get linear variant (or same if none)
Format GetDepthFormat(Format depthStencil); // Extract depth-only format

// Aspect flags for Vulkan image views
u32 GetAspectFlags(Format format);         // VK_IMAGE_ASPECT_COLOR_BIT etc.

} // namespace FormatUtils

} // namespace nge::rhi
