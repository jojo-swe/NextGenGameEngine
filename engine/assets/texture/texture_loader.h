#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_types.h"
#include <string>
#include <vector>

namespace nge::assets {

// ─── Texture Data ────────────────────────────────────────────────────────
struct TextureData {
    u32                 width      = 0;
    u32                 height     = 0;
    u32                 depth      = 1;
    u32                 mipLevels  = 1;
    u32                 arrayLayers = 1;
    u32                 channels   = 4;
    rhi::Format         format     = rhi::Format::RGBA8_UNORM;
    rhi::TextureType    type       = rhi::TextureType::Tex2D;
    bool                isSRGB     = false;
    bool                isHDR      = false;

    struct MipData {
        u32                offset = 0;
        u32                size   = 0;
        u32                width  = 0;
        u32                height = 0;
    };

    std::vector<byte>    pixels;
    std::vector<MipData> mips;

    usize GetPixelDataSize() const { return pixels.size(); }
};

// ─── Texture Loader ──────────────────────────────────────────────────────
// Loads textures from common image formats using stb_image.
// Supports: PNG, JPG, BMP, TGA, HDR, PSD, GIF
// Also supports KTX2 and DDS for GPU-compressed formats.

class TextureLoader {
public:
    // Load from file. Auto-detects format.
    static bool Load(const std::string& path, TextureData& outTexture, bool forceSRGB = false);

    // Load HDR image (returns float data)
    static bool LoadHDR(const std::string& path, TextureData& outTexture);

    // Generate mip chain on CPU
    static void GenerateMips(TextureData& texture);

    // Compress to BC7 (for GPU upload). Requires a compressor library.
    static bool CompressBC7(const TextureData& src, TextureData& dst);
};

} // namespace nge::assets
