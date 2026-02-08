#include "engine/rhi/common/rhi_attachment.h"

namespace nge::rhi {

RenderPassBuilder& RenderPassBuilder::SetSize(u32 width, u32 height) {
    m_desc.renderWidth = width;
    m_desc.renderHeight = height;
    return *this;
}

RenderPassBuilder& RenderPassBuilder::SetSampleCount(u32 samples) {
    m_desc.sampleCount = samples;
    return *this;
}

RenderPassBuilder& RenderPassBuilder::AddColorAttachment(TextureHandle texture, Format format,
                                                           LoadOp load, StoreOp store) {
    ColorAttachment att;
    att.texture = texture;
    att.format = format;
    att.loadOp = load;
    att.storeOp = store;
    m_desc.colorAttachments.push_back(att);
    return *this;
}

RenderPassBuilder& RenderPassBuilder::SetColorClear(u32 index, f32 r, f32 g, f32 b, f32 a) {
    if (index < m_desc.colorAttachments.size()) {
        m_desc.colorAttachments[index].clearValue = ClearValue::Color(r, g, b, a);
    }
    return *this;
}

RenderPassBuilder& RenderPassBuilder::SetColorLoadOp(u32 index, LoadOp op) {
    if (index < m_desc.colorAttachments.size()) {
        m_desc.colorAttachments[index].loadOp = op;
    }
    return *this;
}

RenderPassBuilder& RenderPassBuilder::SetResolveTarget(u32 index, TextureHandle resolveTexture) {
    if (index < m_desc.colorAttachments.size()) {
        m_desc.colorAttachments[index].resolveTexture = resolveTexture;
    }
    return *this;
}

RenderPassBuilder& RenderPassBuilder::SetDepthStencil(TextureHandle texture, Format format) {
    m_desc.depthStencil.texture = texture;
    m_desc.depthStencil.format = format;
    m_desc.hasDepthStencil = true;
    return *this;
}

RenderPassBuilder& RenderPassBuilder::SetDepthClear(f32 depth, u32 stencil) {
    m_desc.depthStencil.clearValue = ClearValue::Depth(depth, stencil);
    return *this;
}

RenderPassBuilder& RenderPassBuilder::SetDepthLoadOp(LoadOp op) {
    m_desc.depthStencil.depthLoadOp = op;
    return *this;
}

RenderPassBuilder& RenderPassBuilder::SetDepthStoreOp(StoreOp op) {
    m_desc.depthStencil.depthStoreOp = op;
    return *this;
}

RenderPassBuilder& RenderPassBuilder::SetDepthReadOnly(bool readOnly) {
    m_desc.depthStencil.readOnly = readOnly;
    return *this;
}

RenderPassBuilder& RenderPassBuilder::SetStencilOps(LoadOp load, StoreOp store) {
    m_desc.depthStencil.stencilLoadOp = load;
    m_desc.depthStencil.stencilStoreOp = store;
    return *this;
}

RenderPassBuilder& RenderPassBuilder::SetViewMask(u32 mask) {
    m_desc.viewMask = mask;
    return *this;
}

// ─── Presets ─────────────────────────────────────────────────────────────

RenderPassDesc RenderPassBuilder::GBuffer(u32 width, u32 height,
                                            TextureHandle color, TextureHandle normal,
                                            TextureHandle motion, TextureHandle depth) {
    return RenderPassBuilder()
        .SetSize(width, height)
        .AddColorAttachment(color, Format::RGBA16_FLOAT, LoadOp::Clear, StoreOp::Store)
        .AddColorAttachment(normal, Format::RG16_FLOAT, LoadOp::Clear, StoreOp::Store)
        .AddColorAttachment(motion, Format::RG16_FLOAT, LoadOp::Clear, StoreOp::Store)
        .SetColorClear(0, 0, 0, 0, 0)
        .SetColorClear(1, 0, 0, 0, 0)
        .SetColorClear(2, 0, 0, 0, 0)
        .SetDepthStencil(depth, Format::D32_FLOAT)
        .SetDepthClear(0.0f, 0) // Reverse-Z
        .Build();
}

RenderPassDesc RenderPassBuilder::DepthOnly(u32 width, u32 height, TextureHandle depth) {
    return RenderPassBuilder()
        .SetSize(width, height)
        .SetDepthStencil(depth, Format::D32_FLOAT)
        .SetDepthClear(0.0f, 0)
        .Build();
}

RenderPassDesc RenderPassBuilder::PostProcess(u32 width, u32 height, TextureHandle output) {
    return RenderPassBuilder()
        .SetSize(width, height)
        .AddColorAttachment(output, Format::RGBA8_UNORM, LoadOp::DontCare, StoreOp::Store)
        .Build();
}

RenderPassDesc RenderPassBuilder::ShadowMap(u32 size, TextureHandle depth) {
    return RenderPassBuilder()
        .SetSize(size, size)
        .SetDepthStencil(depth, Format::D32_FLOAT)
        .SetDepthClear(1.0f, 0) // Standard Z for shadow maps
        .Build();
}

} // namespace nge::rhi
