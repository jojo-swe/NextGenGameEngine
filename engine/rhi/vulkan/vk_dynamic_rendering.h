#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>

namespace nge::rhi {

// ─── Vulkan Dynamic Rendering Helper ─────────────────────────────────────
// Wraps VK_KHR_dynamic_rendering (core in Vulkan 1.3) to eliminate
// VkRenderPass and VkFramebuffer objects. Rendering begins/ends with
// inline attachment descriptions, simplifying render graph integration.
//
// Benefits over traditional render passes:
//   - No upfront VkRenderPass creation
//   - No VkFramebuffer objects
//   - Simpler render graph barrier management
//   - Direct attachment specification at begin time

// ClearValue, LoadOp, StoreOp are defined in rhi_types.h (included via rhi_device.h)
using AttachmentLoadOp = LoadOp;
using AttachmentStoreOp = StoreOp;

struct DynamicColorAttachment {
    TextureHandle    imageView;
    Format           format = Format::RGBA8_UNORM;
    AttachmentLoadOp loadOp = AttachmentLoadOp::Clear;
    AttachmentStoreOp storeOp = AttachmentStoreOp::Store;
    ClearValue       clearValue = ClearValue::Color(0, 0, 0, 1);
    TextureHandle    resolveView;     // For MSAA resolve (optional)
};

struct DynamicDepthAttachment {
    TextureHandle    imageView;
    Format           format = Format::D32_FLOAT;
    AttachmentLoadOp loadOp = AttachmentLoadOp::Clear;
    AttachmentStoreOp storeOp = AttachmentStoreOp::Store;
    ClearValue       clearValue = ClearValue::Depth(0.0f, 0);
};

struct DynamicStencilAttachment {
    TextureHandle    imageView;
    Format           format = Format::D24_UNORM_S8_UINT;
    AttachmentLoadOp loadOp = AttachmentLoadOp::Clear;
    AttachmentStoreOp storeOp = AttachmentStoreOp::Store;
    ClearValue       clearValue = ClearValue::Depth(0.0f, 0);
};

struct DynamicRenderingInfo {
    u32              renderAreaX = 0;
    u32              renderAreaY = 0;
    u32              renderAreaWidth = 0;
    u32              renderAreaHeight = 0;
    u32              layerCount = 1;
    u32              viewMask = 0;

    std::vector<DynamicColorAttachment> colorAttachments;
    DynamicDepthAttachment              depthAttachment;
    DynamicStencilAttachment            stencilAttachment;
    bool                                hasDepth = false;
    bool                                hasStencil = false;
};

class DynamicRenderingBuilder {
public:
    DynamicRenderingBuilder& SetRenderArea(u32 x, u32 y, u32 width, u32 height);
    DynamicRenderingBuilder& SetLayerCount(u32 count);
    DynamicRenderingBuilder& SetViewMask(u32 mask);

    DynamicRenderingBuilder& AddColorAttachment(const DynamicColorAttachment& attachment);
    DynamicRenderingBuilder& AddColorAttachment(TextureHandle view, Format format,
                                                  AttachmentLoadOp loadOp = AttachmentLoadOp::Clear,
                                                  ClearValue clear = ClearValue::Color());

    DynamicRenderingBuilder& SetDepthAttachment(const DynamicDepthAttachment& attachment);
    DynamicRenderingBuilder& SetDepthAttachment(TextureHandle view, Format format = Format::D32_FLOAT,
                                                  AttachmentLoadOp loadOp = AttachmentLoadOp::Clear,
                                                  ClearValue clear = ClearValue::Depth());

    DynamicRenderingBuilder& SetStencilAttachment(const DynamicStencilAttachment& attachment);

    DynamicRenderingInfo Build() const;

    // Presets
    static DynamicRenderingInfo GBufferPass(TextureHandle albedo, TextureHandle normal,
                                             TextureHandle motion, TextureHandle depth,
                                             u32 width, u32 height);
    static DynamicRenderingInfo DepthOnlyPass(TextureHandle depth, u32 width, u32 height);
    static DynamicRenderingInfo PostProcessPass(TextureHandle output, u32 width, u32 height);
    static DynamicRenderingInfo ShadowPass(TextureHandle shadowMap, u32 resolution);

    // Begin/end rendering (command list helpers)
    static void BeginRendering(ICommandList* cmd, const DynamicRenderingInfo& info);
    static void EndRendering(ICommandList* cmd);

private:
    DynamicRenderingInfo m_info;
};

} // namespace nge::rhi
