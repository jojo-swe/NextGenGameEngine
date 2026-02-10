#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_types.h"

namespace nge::rhi {

// ─── Render State Builder ────────────────────────────────────────────────
// Fluent API for constructing blend, depth/stencil, and rasterizer state.
// Produces a hashable state block that maps to PSO creation.

// ─── Blend State ─────────────────────────────────────────────────────────
// BlendFactor, BlendOp, BlendAttachment are defined in rhi_types.h

struct BlendState {
    BlendAttachment attachments[8];
    u32             attachmentCount = 1;
    bool            alphaToCoverage = false;
    f32             blendConstants[4] = {0, 0, 0, 0};
};

// ─── Depth/Stencil State ─────────────────────────────────────────────────

// StencilOp, CompareOp are defined in rhi_types.h

struct StencilFace {
    StencilOp failOp      = StencilOp::Keep;
    StencilOp depthFailOp = StencilOp::Keep;
    StencilOp passOp      = StencilOp::Keep;
    CompareOp compareOp   = CompareOp::Always;
    u8        compareMask = 0xFF;
    u8        writeMask   = 0xFF;
    u8        reference   = 0;
};

struct DepthStencilState {
    bool        depthTestEnable  = true;
    bool        depthWriteEnable = true;
    CompareOp   depthCompareOp   = CompareOp::Less; // Or Greater for reverse-Z
    bool        depthBoundsEnable = false;
    f32         minDepthBounds = 0.0f;
    f32         maxDepthBounds = 1.0f;
    bool        stencilEnable = false;
    StencilFace front;
    StencilFace back;
};

// ─── Rasterizer State ────────────────────────────────────────────────────

// PolygonMode, CullMode, FrontFace are defined in rhi_types.h

struct RasterizerState {
    PolygonMode polygonMode = PolygonMode::Fill;
    CullMode    cullMode    = CullMode::Back;
    FrontFace   frontFace   = FrontFace::CounterClockwise;
    bool        depthClamp  = false;
    bool        depthBias   = false;
    f32         depthBiasConstant = 0.0f;
    f32         depthBiasClamp    = 0.0f;
    f32         depthBiasSlope    = 0.0f;
    f32         lineWidth   = 1.0f;
    bool        conservativeRasterization = false;
};

// ─── Multisample State ───────────────────────────────────────────────────

struct MultisampleState {
    u32  sampleCount = 1;
    bool sampleShading = false;
    f32  minSampleShading = 1.0f;
    u32  sampleMask = 0xFFFFFFFF;
};

// ─── Combined Render State ───────────────────────────────────────────────

struct RenderState {
    BlendState        blend;
    DepthStencilState depthStencil;
    RasterizerState   rasterizer;
    MultisampleState  multisample;

    u64 Hash() const;
};

// ─── Render State Builder (Fluent API) ───────────────────────────────────

class RenderStateBuilder {
public:
    RenderStateBuilder() = default;

    // Blend
    RenderStateBuilder& EnableBlend(u32 attachment = 0);
    RenderStateBuilder& SetBlendFunc(BlendFactor src, BlendFactor dst, u32 attachment = 0);
    RenderStateBuilder& SetBlendFuncAlpha(BlendFactor src, BlendFactor dst, u32 attachment = 0);
    RenderStateBuilder& SetBlendOp(BlendOp op, u32 attachment = 0);
    RenderStateBuilder& SetColorWriteMask(u8 mask, u32 attachment = 0);
    RenderStateBuilder& SetAttachmentCount(u32 count);
    RenderStateBuilder& EnableAlphaToCoverage();

    // Depth/Stencil
    RenderStateBuilder& EnableDepthTest(bool enable = true);
    RenderStateBuilder& EnableDepthWrite(bool enable = true);
    RenderStateBuilder& SetDepthCompare(CompareOp op);
    RenderStateBuilder& EnableDepthBounds(f32 min, f32 max);
    RenderStateBuilder& EnableStencil();
    RenderStateBuilder& SetStencilFront(StencilOp fail, StencilOp depthFail, StencilOp pass, CompareOp compare);
    RenderStateBuilder& SetStencilBack(StencilOp fail, StencilOp depthFail, StencilOp pass, CompareOp compare);

    // Rasterizer
    RenderStateBuilder& SetCullMode(CullMode mode);
    RenderStateBuilder& SetFrontFace(FrontFace face);
    RenderStateBuilder& SetPolygonMode(PolygonMode mode);
    RenderStateBuilder& EnableDepthBias(f32 constant, f32 clamp, f32 slope);
    RenderStateBuilder& EnableDepthClamp();
    RenderStateBuilder& SetLineWidth(f32 width);
    RenderStateBuilder& EnableConservativeRasterization();

    // Multisample
    RenderStateBuilder& SetSampleCount(u32 count);
    RenderStateBuilder& EnableSampleShading(f32 minRate = 0.25f);

    // Build
    RenderState Build() const { return m_state; }

    // Presets
    static RenderState Opaque();
    static RenderState AlphaBlend();
    static RenderState Additive();
    static RenderState DepthOnly();
    static RenderState Wireframe();
    static RenderState ShadowMap();

private:
    RenderState m_state;
};

} // namespace nge::rhi
