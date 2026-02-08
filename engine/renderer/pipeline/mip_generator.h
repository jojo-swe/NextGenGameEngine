#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"

namespace nge::renderer {

// ─── Mip Chain Generator ─────────────────────────────────────────────────
// Generates mip levels for a texture using compute shader downsampling.
// Supports single-pass mip generation via subgroup operations (SPD)
// or multi-pass with one dispatch per mip level.
//
// Handles sRGB-correct downsampling by converting to linear before
// filtering and back to sRGB after.

enum class MipFilterMode : u8 {
    Box,        // Simple 2×2 average (fast, default)
    Kaiser,     // Kaiser-windowed sinc (higher quality)
    Lanczos,    // Lanczos 3-tap (sharpest)
};

struct MipGeneratorConfig {
    MipFilterMode filterMode = MipFilterMode::Box;
    bool          srgbCorrect = true;     // Linear-space filtering for sRGB textures
    bool          useSinglePass = true;    // Use SPD single-pass downsampler if available
    u32           maxMipLevels = 16;
};

class MipGenerator {
public:
    bool Init(rhi::IDevice* device, const MipGeneratorConfig& config = {});
    void Shutdown();

    // Generate all mip levels for a 2D texture
    void Generate(rhi::ICommandList* cmd, rhi::TextureHandle texture,
                   u32 width, u32 height, u32 mipCount, rhi::Format format);

    // Generate mip levels for a specific range
    void GenerateRange(rhi::ICommandList* cmd, rhi::TextureHandle texture,
                        u32 width, u32 height, u32 startMip, u32 endMip, rhi::Format format);

    // Generate mips for a cubemap (all 6 faces)
    void GenerateCubemap(rhi::ICommandList* cmd, rhi::TextureHandle texture,
                           u32 faceSize, u32 mipCount, rhi::Format format);

    // Generate mips for a texture array
    void GenerateArray(rhi::ICommandList* cmd, rhi::TextureHandle texture,
                         u32 width, u32 height, u32 arrayLayers, u32 mipCount, rhi::Format format);

    const MipGeneratorConfig& GetConfig() const { return m_config; }

private:
    void DispatchMipLevel(rhi::ICommandList* cmd, rhi::TextureHandle texture,
                            u32 srcMip, u32 dstMip, u32 dstWidth, u32 dstHeight,
                            u32 arrayLayer, rhi::Format format);

    rhi::IDevice* m_device = nullptr;
    MipGeneratorConfig m_config;

    // Compute pipelines (one per filter mode)
    rhi::PipelineHandle m_boxFilterPipeline;
    rhi::PipelineHandle m_kaiserFilterPipeline;
    rhi::PipelineHandle m_lanczosPipeline;
    rhi::PipelineHandle m_spdPipeline; // Single-pass downsampler

    // Scratch buffer for SPD atomic counter
    rhi::BufferHandle m_spdCounterBuffer;
};

} // namespace nge::renderer
