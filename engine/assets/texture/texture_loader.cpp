#include "engine/assets/texture/texture_loader.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"
#include "engine/core/math/math_types.h"
#include <cstring>

// stb_image — single-header image loader (via vcpkg)
// #define STB_IMAGE_IMPLEMENTATION is in a separate .c file
// #include <stb_image.h>

// Stub implementation until stb_image is available via vcpkg build

namespace nge::assets {

bool TextureLoader::Load(const std::string& path, TextureData& outTexture, bool forceSRGB) {
    NGE_LOG_INFO("Loading texture: {}", path);

    // TODO: Use stb_image when vcpkg dependencies are built
    // int w, h, channels;
    // stbi_set_flip_vertically_on_load(1);
    // unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    // if (!data) { NGE_LOG_ERROR("Failed to load: {}", path); return false; }

    // For now, generate a 2x2 checkerboard test texture
    outTexture.width    = 256;
    outTexture.height   = 256;
    outTexture.channels = 4;
    outTexture.format   = forceSRGB ? rhi::Format::RGBA8_SRGB : rhi::Format::RGBA8_UNORM;
    outTexture.isSRGB   = forceSRGB;
    outTexture.isHDR    = false;
    outTexture.mipLevels = 1;

    usize pixelCount = outTexture.width * outTexture.height;
    outTexture.pixels.resize(pixelCount * 4);

    for (u32 y = 0; y < outTexture.height; ++y) {
        for (u32 x = 0; x < outTexture.width; ++x) {
            usize idx = (y * outTexture.width + x) * 4;
            bool checker = ((x / 32) + (y / 32)) % 2 == 0;
            byte val = checker ? 255 : 64;
            outTexture.pixels[idx + 0] = val;
            outTexture.pixels[idx + 1] = val;
            outTexture.pixels[idx + 2] = val;
            outTexture.pixels[idx + 3] = 255;
        }
    }

    TextureData::MipData mip0;
    mip0.offset = 0;
    mip0.size   = static_cast<u32>(outTexture.pixels.size());
    mip0.width  = outTexture.width;
    mip0.height = outTexture.height;
    outTexture.mips.push_back(mip0);

    NGE_LOG_INFO("Loaded texture: {}x{}, {} channels", outTexture.width, outTexture.height, outTexture.channels);
    return true;
}

bool TextureLoader::LoadHDR(const std::string& path, TextureData& outTexture) {
    NGE_LOG_INFO("Loading HDR texture: {}", path);

    // TODO: stbi_loadf() for HDR
    // float* data = stbi_loadf(path.c_str(), &w, &h, &channels, 4);

    // Stub: 1x1 white HDR pixel
    outTexture.width    = 1;
    outTexture.height   = 1;
    outTexture.channels = 4;
    outTexture.format   = rhi::Format::RGBA32_FLOAT;
    outTexture.isHDR    = true;
    outTexture.mipLevels = 1;

    outTexture.pixels.resize(sizeof(f32) * 4);
    f32 white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    std::memcpy(outTexture.pixels.data(), white, sizeof(white));

    TextureData::MipData mip0;
    mip0.offset = 0;
    mip0.size   = static_cast<u32>(outTexture.pixels.size());
    mip0.width  = 1;
    mip0.height = 1;
    outTexture.mips.push_back(mip0);

    return true;
}

void TextureLoader::GenerateMips(TextureData& texture) {
    if (texture.isHDR) return; // TODO: HDR mip generation

    u32 w = texture.width;
    u32 h = texture.height;
    u32 channels = texture.channels;
    u32 mipCount = static_cast<u32>(std::floor(std::log2(math::Max(w, h)))) + 1;

    // Start with existing mip 0 data
    std::vector<byte> allPixels = texture.pixels;
    texture.mips.clear();

    TextureData::MipData mip0;
    mip0.offset = 0;
    mip0.size   = w * h * channels;
    mip0.width  = w;
    mip0.height = h;
    texture.mips.push_back(mip0);

    u32 prevW = w;
    u32 prevH = h;
    u32 prevOffset = 0;

    for (u32 mip = 1; mip < mipCount; ++mip) {
        u32 mipW = math::Max(prevW / 2, 1u);
        u32 mipH = math::Max(prevH / 2, 1u);
        u32 mipSize = mipW * mipH * channels;
        u32 mipOffset = static_cast<u32>(allPixels.size());

        allPixels.resize(mipOffset + mipSize);

        // Box filter downsample
        const byte* src = allPixels.data() + prevOffset;
        byte* dst = allPixels.data() + mipOffset;

        for (u32 y = 0; y < mipH; ++y) {
            for (u32 x = 0; x < mipW; ++x) {
                u32 sx = x * 2;
                u32 sy = y * 2;

                for (u32 c = 0; c < channels; ++c) {
                    u32 sum = 0;
                    u32 count = 0;

                    auto sample = [&](u32 px, u32 py) {
                        if (px < prevW && py < prevH) {
                            sum += src[(py * prevW + px) * channels + c];
                            count++;
                        }
                    };

                    sample(sx, sy);
                    sample(sx + 1, sy);
                    sample(sx, sy + 1);
                    sample(sx + 1, sy + 1);

                    dst[(y * mipW + x) * channels + c] = static_cast<byte>(count > 0 ? sum / count : 0);
                }
            }
        }

        TextureData::MipData mipData;
        mipData.offset = mipOffset;
        mipData.size   = mipSize;
        mipData.width  = mipW;
        mipData.height = mipH;
        texture.mips.push_back(mipData);

        prevW = mipW;
        prevH = mipH;
        prevOffset = mipOffset;
    }

    texture.pixels = std::move(allPixels);
    texture.mipLevels = mipCount;
}

bool TextureLoader::CompressBC7(const TextureData& /*src*/, TextureData& /*dst*/) {
    // TODO: Implement BC7 compression using ispc_texcomp or similar
    NGE_LOG_WARN("BC7 compression not yet implemented");
    return false;
}

} // namespace nge::assets
