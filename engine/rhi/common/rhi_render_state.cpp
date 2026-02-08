#include "engine/rhi/common/rhi_render_state.h"
#include <cstring>
#include <functional>

namespace nge::rhi {

u64 RenderState::Hash() const {
    // FNV-1a hash over the entire state block
    const u8* data = reinterpret_cast<const u8*>(this);
    usize size = sizeof(RenderState);
    u64 h = 14695981039346656037ULL;
    for (usize i = 0; i < size; ++i) {
        h ^= static_cast<u64>(data[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

// ─── Builder Methods ─────────────────────────────────────────────────────

RenderStateBuilder& RenderStateBuilder::EnableBlend(u32 attachment) {
    if (attachment < 8) m_state.blend.attachments[attachment].enable = true;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::SetBlendFunc(BlendFactor src, BlendFactor dst, u32 attachment) {
    if (attachment < 8) {
        m_state.blend.attachments[attachment].srcColor = src;
        m_state.blend.attachments[attachment].dstColor = dst;
    }
    return *this;
}

RenderStateBuilder& RenderStateBuilder::SetBlendFuncAlpha(BlendFactor src, BlendFactor dst, u32 attachment) {
    if (attachment < 8) {
        m_state.blend.attachments[attachment].srcAlpha = src;
        m_state.blend.attachments[attachment].dstAlpha = dst;
    }
    return *this;
}

RenderStateBuilder& RenderStateBuilder::SetBlendOp(BlendOp op, u32 attachment) {
    if (attachment < 8) {
        m_state.blend.attachments[attachment].colorOp = op;
        m_state.blend.attachments[attachment].alphaOp = op;
    }
    return *this;
}

RenderStateBuilder& RenderStateBuilder::SetColorWriteMask(u8 mask, u32 attachment) {
    if (attachment < 8) m_state.blend.attachments[attachment].writeMask = mask;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::SetAttachmentCount(u32 count) {
    m_state.blend.attachmentCount = count;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::EnableAlphaToCoverage() {
    m_state.blend.alphaToCoverage = true;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::EnableDepthTest(bool enable) {
    m_state.depthStencil.depthTestEnable = enable;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::EnableDepthWrite(bool enable) {
    m_state.depthStencil.depthWriteEnable = enable;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::SetDepthCompare(CompareOp op) {
    m_state.depthStencil.depthCompareOp = op;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::EnableDepthBounds(f32 min, f32 max) {
    m_state.depthStencil.depthBoundsEnable = true;
    m_state.depthStencil.minDepthBounds = min;
    m_state.depthStencil.maxDepthBounds = max;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::EnableStencil() {
    m_state.depthStencil.stencilEnable = true;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::SetStencilFront(StencilOp fail, StencilOp depthFail,
                                                          StencilOp pass, CompareOp compare) {
    m_state.depthStencil.front.failOp = fail;
    m_state.depthStencil.front.depthFailOp = depthFail;
    m_state.depthStencil.front.passOp = pass;
    m_state.depthStencil.front.compareOp = compare;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::SetStencilBack(StencilOp fail, StencilOp depthFail,
                                                         StencilOp pass, CompareOp compare) {
    m_state.depthStencil.back.failOp = fail;
    m_state.depthStencil.back.depthFailOp = depthFail;
    m_state.depthStencil.back.passOp = pass;
    m_state.depthStencil.back.compareOp = compare;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::SetCullMode(CullMode mode) {
    m_state.rasterizer.cullMode = mode;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::SetFrontFace(FrontFace face) {
    m_state.rasterizer.frontFace = face;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::SetPolygonMode(PolygonMode mode) {
    m_state.rasterizer.polygonMode = mode;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::EnableDepthBias(f32 constant, f32 clamp, f32 slope) {
    m_state.rasterizer.depthBias = true;
    m_state.rasterizer.depthBiasConstant = constant;
    m_state.rasterizer.depthBiasClamp = clamp;
    m_state.rasterizer.depthBiasSlope = slope;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::EnableDepthClamp() {
    m_state.rasterizer.depthClamp = true;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::SetLineWidth(f32 width) {
    m_state.rasterizer.lineWidth = width;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::EnableConservativeRasterization() {
    m_state.rasterizer.conservativeRasterization = true;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::SetSampleCount(u32 count) {
    m_state.multisample.sampleCount = count;
    return *this;
}

RenderStateBuilder& RenderStateBuilder::EnableSampleShading(f32 minRate) {
    m_state.multisample.sampleShading = true;
    m_state.multisample.minSampleShading = minRate;
    return *this;
}

// ─── Presets ─────────────────────────────────────────────────────────────

RenderState RenderStateBuilder::Opaque() {
    return RenderStateBuilder()
        .EnableDepthTest()
        .EnableDepthWrite()
        .SetDepthCompare(CompareOp::Greater) // Reverse-Z
        .SetCullMode(CullMode::Back)
        .Build();
}

RenderState RenderStateBuilder::AlphaBlend() {
    return RenderStateBuilder()
        .EnableBlend()
        .SetBlendFunc(BlendFactor::SrcAlpha, BlendFactor::OneMinusSrcAlpha)
        .SetBlendFuncAlpha(BlendFactor::One, BlendFactor::OneMinusSrcAlpha)
        .EnableDepthTest()
        .EnableDepthWrite(false) // Transparent objects don't write depth
        .SetDepthCompare(CompareOp::Greater)
        .SetCullMode(CullMode::Back)
        .Build();
}

RenderState RenderStateBuilder::Additive() {
    return RenderStateBuilder()
        .EnableBlend()
        .SetBlendFunc(BlendFactor::One, BlendFactor::One)
        .SetBlendFuncAlpha(BlendFactor::One, BlendFactor::One)
        .EnableDepthTest()
        .EnableDepthWrite(false)
        .SetDepthCompare(CompareOp::Greater)
        .SetCullMode(CullMode::None)
        .Build();
}

RenderState RenderStateBuilder::DepthOnly() {
    return RenderStateBuilder()
        .EnableDepthTest()
        .EnableDepthWrite()
        .SetDepthCompare(CompareOp::Greater)
        .SetCullMode(CullMode::Back)
        .SetColorWriteMask(0x0) // No color write
        .Build();
}

RenderState RenderStateBuilder::Wireframe() {
    return RenderStateBuilder()
        .SetPolygonMode(PolygonMode::Line)
        .SetCullMode(CullMode::None)
        .EnableDepthTest()
        .EnableDepthWrite(false)
        .SetDepthCompare(CompareOp::Greater)
        .SetLineWidth(1.0f)
        .Build();
}

RenderState RenderStateBuilder::ShadowMap() {
    return RenderStateBuilder()
        .EnableDepthTest()
        .EnableDepthWrite()
        .SetDepthCompare(CompareOp::Less) // Standard Z for shadow maps
        .SetCullMode(CullMode::Front)     // Front-face culling reduces peter-panning
        .EnableDepthBias(1.25f, 0.0f, 1.75f) // Bias to reduce shadow acne
        .EnableDepthClamp() // Clamp to avoid clipping near-plane geometry
        .SetColorWriteMask(0x0)
        .Build();
}

} // namespace nge::rhi
