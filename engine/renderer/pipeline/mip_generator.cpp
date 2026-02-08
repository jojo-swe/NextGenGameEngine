#include "engine/renderer/pipeline/mip_generator.h"
#include "engine/rhi/common/rhi_format_utils.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::renderer {

bool MipGenerator::Init(rhi::IDevice* device, const MipGeneratorConfig& config) {
    m_device = device;
    m_config = config;

    // TODO: Create compute pipelines from compiled shaders
    // m_boxFilterPipeline = CreateComputePipeline("mip_downsample.hlsl", "CSBoxFilter");
    // m_kaiserFilterPipeline = CreateComputePipeline("mip_downsample.hlsl", "CSKaiserFilter");
    // m_lanczosPipeline = CreateComputePipeline("mip_downsample.hlsl", "CSLanczosFilter");
    // m_spdPipeline = CreateComputePipeline("spd_downsample.hlsl", "CSMain");

    if (config.useSinglePass) {
        // SPD atomic counter buffer
        rhi::BufferDesc desc;
        desc.size = sizeof(u32);
        desc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::TransferDst;
        desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
        desc.debugName = "SPDCounter";
        m_spdCounterBuffer = device->CreateBuffer(desc);
    }

    NGE_LOG_INFO("Mip generator initialized (filter={}, srgb={}, SPD={})",
                 static_cast<u32>(config.filterMode), config.srgbCorrect, config.useSinglePass);
    return true;
}

void MipGenerator::Shutdown() {
    if (!m_device) return;
    if (m_spdCounterBuffer.IsValid()) {
        m_device->DestroyBuffer(m_spdCounterBuffer);
        m_spdCounterBuffer = {};
    }
}

void MipGenerator::Generate(rhi::ICommandList* cmd, rhi::TextureHandle texture,
                              u32 width, u32 height, u32 mipCount, rhi::Format format) {
    GenerateRange(cmd, texture, width, height, 0, mipCount - 1, format);
}

void MipGenerator::GenerateRange(rhi::ICommandList* cmd, rhi::TextureHandle texture,
                                   u32 width, u32 height, u32 startMip, u32 endMip,
                                   rhi::Format format) {
    cmd->BeginDebugLabel("MipGenerate", 0.4f, 0.8f, 0.4f);

    // SPD single-pass path (generates all mips in one dispatch)
    if (m_config.useSinglePass && startMip == 0) {
        // TODO: Bind SPD pipeline and dispatch
        // The SPD algorithm uses subgroup operations to generate the full mip chain
        // in a single compute dispatch with workgroup-level synchronization.
        //
        // cmd->BindComputePipeline(m_spdPipeline);
        // cmd->FillBuffer(m_spdCounterBuffer, 0, sizeof(u32), 0); // Reset counter
        // cmd->BufferBarrier(m_spdCounterBuffer, TransferDst, ShaderWrite);
        // cmd->BindTexture(0, texture); // All mip levels as UAV
        // cmd->BindBuffer(1, m_spdCounterBuffer);
        // u32 dispatchX = (width + 63) / 64;
        // u32 dispatchY = (height + 63) / 64;
        // cmd->Dispatch(dispatchX, dispatchY, 1);

        // For now, fall through to multi-pass
    }

    // Multi-pass: one dispatch per mip level
    for (u32 mip = startMip + 1; mip <= endMip; ++mip) {
        u32 dstWidth = rhi::FormatUtils::CalculateMipDimension(width, mip);
        u32 dstHeight = rhi::FormatUtils::CalculateMipDimension(height, mip);

        DispatchMipLevel(cmd, texture, mip - 1, mip, dstWidth, dstHeight, 0, format);

        // Barrier: mip N write complete before mip N+1 reads it
        cmd->TextureBarrier(texture, rhi::ResourceState::ShaderWrite, rhi::ResourceState::ShaderRead);
    }

    cmd->EndDebugLabel();
}

void MipGenerator::GenerateCubemap(rhi::ICommandList* cmd, rhi::TextureHandle texture,
                                      u32 faceSize, u32 mipCount, rhi::Format format) {
    cmd->BeginDebugLabel("MipGenerateCube", 0.4f, 0.6f, 0.8f);

    for (u32 face = 0; face < 6; ++face) {
        for (u32 mip = 1; mip < mipCount; ++mip) {
            u32 dstSize = rhi::FormatUtils::CalculateMipDimension(faceSize, mip);
            DispatchMipLevel(cmd, texture, mip - 1, mip, dstSize, dstSize, face, format);
            cmd->TextureBarrier(texture, rhi::ResourceState::ShaderWrite, rhi::ResourceState::ShaderRead);
        }
    }

    cmd->EndDebugLabel();
}

void MipGenerator::GenerateArray(rhi::ICommandList* cmd, rhi::TextureHandle texture,
                                    u32 width, u32 height, u32 arrayLayers, u32 mipCount,
                                    rhi::Format format) {
    cmd->BeginDebugLabel("MipGenerateArray", 0.6f, 0.4f, 0.8f);

    for (u32 layer = 0; layer < arrayLayers; ++layer) {
        for (u32 mip = 1; mip < mipCount; ++mip) {
            u32 dstW = rhi::FormatUtils::CalculateMipDimension(width, mip);
            u32 dstH = rhi::FormatUtils::CalculateMipDimension(height, mip);
            DispatchMipLevel(cmd, texture, mip - 1, mip, dstW, dstH, layer, format);
            cmd->TextureBarrier(texture, rhi::ResourceState::ShaderWrite, rhi::ResourceState::ShaderRead);
        }
    }

    cmd->EndDebugLabel();
}

void MipGenerator::DispatchMipLevel(rhi::ICommandList* cmd, rhi::TextureHandle texture,
                                       u32 srcMip, u32 dstMip, u32 dstWidth, u32 dstHeight,
                                       u32 arrayLayer, rhi::Format format) {
    // Select pipeline based on filter mode
    // rhi::PipelineHandle pipeline;
    // switch (m_config.filterMode) {
    //     case MipFilterMode::Box:     pipeline = m_boxFilterPipeline; break;
    //     case MipFilterMode::Kaiser:  pipeline = m_kaiserFilterPipeline; break;
    //     case MipFilterMode::Lanczos: pipeline = m_lanczosPipeline; break;
    // }
    // cmd->BindComputePipeline(pipeline);

    // Push constants: src mip, dst mip, dst dimensions, sRGB flag, array layer
    struct MipConstants {
        u32 srcMip;
        u32 dstMip;
        u32 dstWidth;
        u32 dstHeight;
        u32 arrayLayer;
        u32 isSRGB;
        f32 invDstWidth;
        f32 invDstHeight;
    } constants;

    constants.srcMip = srcMip;
    constants.dstMip = dstMip;
    constants.dstWidth = dstWidth;
    constants.dstHeight = dstHeight;
    constants.arrayLayer = arrayLayer;
    constants.isSRGB = (m_config.srgbCorrect && rhi::FormatUtils::IsSRGBFormat(format)) ? 1 : 0;
    constants.invDstWidth = 1.0f / static_cast<f32>(dstWidth);
    constants.invDstHeight = 1.0f / static_cast<f32>(dstHeight);

    // cmd->PushConstants(&constants, sizeof(constants));
    // cmd->BindTextureMip(0, texture, srcMip); // SRV: source mip
    // cmd->BindTextureUAVMip(1, texture, dstMip); // UAV: destination mip

    u32 dispatchX = (dstWidth + 7) / 8;
    u32 dispatchY = (dstHeight + 7) / 8;
    cmd->Dispatch(dispatchX, dispatchY, 1);

    (void)texture; (void)format;
}

} // namespace nge::renderer
