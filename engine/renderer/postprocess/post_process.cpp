#include "engine/renderer/postprocess/post_process.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"

namespace nge::renderer {

bool PostProcessStack::Init(rhi::IDevice* device, u32 width, u32 height) {
    m_device = device;
    m_width  = width;
    m_height = height;

    // Create samplers
    rhi::SamplerDesc linearClamp;
    linearClamp.minFilter = rhi::FilterMode::Linear;
    linearClamp.magFilter = rhi::FilterMode::Linear;
    linearClamp.addressU  = rhi::AddressMode::ClampToEdge;
    linearClamp.addressV  = rhi::AddressMode::ClampToEdge;
    linearClamp.enableAnisotropy = false;
    m_linearClampSampler = device->CreateSampler(linearClamp);

    rhi::SamplerDesc pointClamp;
    pointClamp.minFilter = rhi::FilterMode::Nearest;
    pointClamp.magFilter = rhi::FilterMode::Nearest;
    pointClamp.addressU  = rhi::AddressMode::ClampToEdge;
    pointClamp.addressV  = rhi::AddressMode::ClampToEdge;
    pointClamp.enableAnisotropy = false;
    m_pointClampSampler = device->CreateSampler(pointClamp);

    CreateBloomResources(width, height);

    // TSR history and output
    rhi::TextureDesc tsrDesc{};
    tsrDesc.width  = width;
    tsrDesc.height = height;
    tsrDesc.format = rhi::Format::RGBA16_FLOAT;
    tsrDesc.usage  = rhi::TextureUsage::ShaderRead | rhi::TextureUsage::ShaderWrite;

    tsrDesc.debugName = "TSR_History";
    m_tsrHistory = device->CreateTexture(tsrDesc);
    tsrDesc.debugName = "TSR_Output";
    m_tsrOutput = device->CreateTexture(tsrDesc);

    NGE_LOG_INFO("Post-processing stack initialized: {}x{}", width, height);
    return true;
}

void PostProcessStack::Shutdown() {
    if (!m_device) return;

    DestroyBloomResources();

    auto destroy = [&](rhi::TextureHandle& h) {
        if (h.IsValid()) { m_device->DestroyTexture(h); h = rhi::TextureHandle{}; }
    };
    destroy(m_tsrHistory);
    destroy(m_tsrOutput);

    if (m_linearClampSampler.IsValid()) m_device->DestroySampler(m_linearClampSampler);
    if (m_pointClampSampler.IsValid()) m_device->DestroySampler(m_pointClampSampler);

    m_device = nullptr;
}

void PostProcessStack::Resize(u32 width, u32 height) {
    if (width == m_width && height == m_height) return;

    DestroyBloomResources();

    auto destroy = [&](rhi::TextureHandle& h) {
        if (h.IsValid()) { m_device->DestroyTexture(h); h = rhi::TextureHandle{}; }
    };
    destroy(m_tsrHistory);
    destroy(m_tsrOutput);

    m_width = width;
    m_height = height;

    CreateBloomResources(width, height);

    rhi::TextureDesc tsrDesc{};
    tsrDesc.width  = width;
    tsrDesc.height = height;
    tsrDesc.format = rhi::Format::RGBA16_FLOAT;
    tsrDesc.usage  = rhi::TextureUsage::ShaderRead | rhi::TextureUsage::ShaderWrite;

    tsrDesc.debugName = "TSR_History";
    m_tsrHistory = m_device->CreateTexture(tsrDesc);
    tsrDesc.debugName = "TSR_Output";
    m_tsrOutput = m_device->CreateTexture(tsrDesc);
}

void PostProcessStack::Execute(rhi::ICommandList* cmd,
                                rhi::TextureHandle sceneColor,
                                rhi::TextureHandle motionVectors,
                                rhi::TextureHandle /*depthBuffer*/,
                                rhi::TextureHandle outputTarget,
                                u32 frameIndex) {
    cmd->BeginDebugLabel("Post-Processing", 0.9f, 0.5f, 0.2f);

    rhi::TextureHandle current = sceneColor;

    // 1. Bloom
    if (m_settings.bloomEnabled) {
        PassBloom(cmd, current);
    }

    // 2. TSR (Temporal Super Resolution)
    if (m_settings.tsrEnabled) {
        PassTSR(cmd, current, motionVectors, frameIndex);
        current = m_tsrOutput;
    }

    // 3. Motion blur
    if (m_settings.motionBlurEnabled) {
        PassMotionBlur(cmd, current, motionVectors);
    }

    // 4. Tone mapping → LDR output
    PassTonemap(cmd, current, outputTarget);

    cmd->EndDebugLabel();
}

void PostProcessStack::PassBloom(rhi::ICommandList* cmd, rhi::TextureHandle sceneColor) {
    cmd->BeginDebugLabel("Bloom", 1.0f, 0.8f, 0.2f);

    if (m_bloomDownPipeline.IsValid() && m_bloomMipCount > 0) {
        // Downsample chain: scene → mip0 → mip1 → ... → mipN
        // First pass applies threshold + Karis average
        for (u32 i = 0; i < m_bloomMipCount; ++i) {
            cmd->BindComputePipeline(m_bloomDownPipeline);
            // TODO: Set push constants (texelSize, threshold, isFirstPass, etc.)
            // TODO: Bind input texture (previous mip or scene), output texture (current mip)
            u32 mipW = math::Max(m_width >> (i + 1), 1u);
            u32 mipH = math::Max(m_height >> (i + 1), 1u);
            cmd->Dispatch((mipW + 7) / 8, (mipH + 7) / 8, 1);

            // Barrier between mips
            cmd->TextureBarrier(m_bloomMips[i],
                                rhi::ResourceState::ShaderWrite,
                                rhi::ResourceState::ShaderRead);
        }

        // Upsample chain: mipN → ... → mip1 → mip0 (additive blend)
        if (m_bloomUpPipeline.IsValid()) {
            for (i32 i = static_cast<i32>(m_bloomMipCount) - 2; i >= 0; --i) {
                cmd->BindComputePipeline(m_bloomUpPipeline);
                // TODO: Set push constants, bind input (lower mip), high-res (current mip), output
                u32 mipW = math::Max(m_width >> (i + 1), 1u);
                u32 mipH = math::Max(m_height >> (i + 1), 1u);
                cmd->Dispatch((mipW + 7) / 8, (mipH + 7) / 8, 1);

                cmd->TextureBarrier(m_bloomMips[i],
                                    rhi::ResourceState::ShaderWrite,
                                    rhi::ResourceState::ShaderRead);
            }
        }
    }

    (void)sceneColor;
    cmd->EndDebugLabel();
}

void PostProcessStack::PassTSR(rhi::ICommandList* cmd,
                                rhi::TextureHandle sceneColor,
                                rhi::TextureHandle motionVectors,
                                u32 /*frameIndex*/) {
    cmd->BeginDebugLabel("TSR", 0.2f, 0.8f, 0.6f);

    if (m_tsrPipeline.IsValid()) {
        cmd->BindComputePipeline(m_tsrPipeline);
        // TODO: Set push constants (texelSize, renderScale, blendFactor, etc.)
        // TODO: Bind current frame, history, motion vectors, depth, output
        cmd->Dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);
    }

    // Swap history: current output becomes next frame's history
    std::swap(m_tsrHistory, m_tsrOutput);

    (void)sceneColor;
    (void)motionVectors;
    cmd->EndDebugLabel();
}

void PostProcessStack::PassTonemap(rhi::ICommandList* cmd,
                                    rhi::TextureHandle hdrInput,
                                    rhi::TextureHandle ldrOutput) {
    cmd->BeginDebugLabel("Tone Mapping", 0.8f, 0.3f, 0.1f);

    if (m_tonemapPipeline.IsValid()) {
        cmd->BindComputePipeline(m_tonemapPipeline);
        // TODO: Set push constants (operator, exposure, contrast, saturation, etc.)
        // TODO: Bind HDR input, LDR output, color LUT
        cmd->Dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);
    }

    (void)hdrInput;
    (void)ldrOutput;
    cmd->EndDebugLabel();
}

void PostProcessStack::PassMotionBlur(rhi::ICommandList* cmd,
                                       rhi::TextureHandle /*color*/,
                                       rhi::TextureHandle /*motionVectors*/) {
    cmd->BeginDebugLabel("Motion Blur", 0.5f, 0.5f, 0.8f);

    if (m_motionBlurPipeline.IsValid()) {
        cmd->BindComputePipeline(m_motionBlurPipeline);
        cmd->Dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);
    }

    cmd->EndDebugLabel();
}

void PostProcessStack::CreateBloomResources(u32 width, u32 height) {
    m_bloomMipCount = math::Min(m_settings.bloomMipCount, MAX_BLOOM_MIPS);

    for (u32 i = 0; i < m_bloomMipCount; ++i) {
        u32 mipW = math::Max(width >> (i + 1), 1u);
        u32 mipH = math::Max(height >> (i + 1), 1u);

        rhi::TextureDesc desc{};
        desc.width  = mipW;
        desc.height = mipH;
        desc.format = rhi::Format::RGBA16_FLOAT;
        desc.usage  = rhi::TextureUsage::ShaderRead | rhi::TextureUsage::ShaderWrite;
        desc.debugName = "BloomMip";
        m_bloomMips[i] = m_device->CreateTexture(desc);
    }
}

void PostProcessStack::DestroyBloomResources() {
    for (u32 i = 0; i < m_bloomMipCount; ++i) {
        if (m_bloomMips[i].IsValid()) {
            m_device->DestroyTexture(m_bloomMips[i]);
            m_bloomMips[i] = rhi::TextureHandle{};
        }
    }
    m_bloomMipCount = 0;
}

} // namespace nge::renderer
