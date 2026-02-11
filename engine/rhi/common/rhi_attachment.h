#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_types.h"
#include <vector>

namespace nge::rhi {

// ─── Render Pass Attachment Configuration ────────────────────────────────
// Fluent builder for describing color/depth/resolve attachments.
// Used by the render graph and Vulkan dynamic rendering (VK_KHR_dynamic_rendering).

// LoadOp, StoreOp, ClearValue are defined in rhi_types.h

struct ColorAttachment {
    TextureHandle texture;
    TextureHandle resolveTexture; // For MSAA resolve (optional)
    Format        format = Format::RGBA8_UNORM;
    LoadOp        loadOp = LoadOp::Clear;
    StoreOp       storeOp = StoreOp::Store;
    ClearValue    clearValue = ClearValue::Color(0, 0, 0, 1);
    u32           mipLevel = 0;
    u32           arrayLayer = 0;
};

struct DepthStencilAttachment {
    TextureHandle texture;
    Format        format = Format::D32_FLOAT;
    LoadOp        depthLoadOp = LoadOp::Clear;
    StoreOp       depthStoreOp = StoreOp::Store;
    LoadOp        stencilLoadOp = LoadOp::DontCare;
    StoreOp       stencilStoreOp = StoreOp::DontCare;
    ClearValue    clearValue = ClearValue::Depth(0.0f, 0); // Reverse-Z: clear to 0
    bool          readOnly = false; // Depth read-only (e.g., for late passes)
};

struct RenderPassDesc {
    std::vector<ColorAttachment> colorAttachments;
    DepthStencilAttachment       depthStencil;
    bool                         hasDepthStencil = false;
    u32                          renderWidth = 0;
    u32                          renderHeight = 0;
    u32                          viewMask = 0;   // Multi-view rendering
    u32                          sampleCount = 1;
};

// ─── Render Pass Builder (Fluent API) ────────────────────────────────────

class RenderPassBuilder {
public:
    RenderPassBuilder() = default;

    RenderPassBuilder& SetSize(u32 width, u32 height);
    RenderPassBuilder& SetSampleCount(u32 samples);

    // Color attachments
    RenderPassBuilder& AddColorAttachment(TextureHandle texture, Format format,
                                            LoadOp load = LoadOp::Clear,
                                            StoreOp store = StoreOp::Store);
    RenderPassBuilder& SetColorClear(u32 index, f32 r, f32 g, f32 b, f32 a = 1.0f);
    RenderPassBuilder& SetColorLoadOp(u32 index, LoadOp op);
    RenderPassBuilder& SetResolveTarget(u32 index, TextureHandle resolveTexture);

    // Depth/stencil
    RenderPassBuilder& SetDepthStencil(TextureHandle texture, Format format = Format::D32_FLOAT);
    RenderPassBuilder& SetDepthClear(f32 depth, u32 stencil = 0);
    RenderPassBuilder& SetDepthLoadOp(LoadOp op);
    RenderPassBuilder& SetDepthStoreOp(StoreOp op);
    RenderPassBuilder& SetDepthReadOnly(bool readOnly = true);
    RenderPassBuilder& SetStencilOps(LoadOp load, StoreOp store);

    // Multi-view
    RenderPassBuilder& SetViewMask(u32 mask);

    // Build
    RenderPassDesc Build() const { return m_desc; }

    // Presets
    static RenderPassDesc GBuffer(u32 width, u32 height,
                                    TextureHandle color, TextureHandle normal,
                                    TextureHandle motion, TextureHandle depth);
    static RenderPassDesc DepthOnly(u32 width, u32 height, TextureHandle depth);
    static RenderPassDesc PostProcess(u32 width, u32 height, TextureHandle output);
    static RenderPassDesc ShadowMap(u32 size, TextureHandle depth);

private:
    RenderPassDesc m_desc;
};

} // namespace nge::rhi
