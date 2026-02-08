#include "engine/rhi/vulkan/vk_dynamic_rendering.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

DynamicRenderingBuilder& DynamicRenderingBuilder::SetRenderArea(u32 x, u32 y, u32 width, u32 height) {
    m_info.renderAreaX = x;
    m_info.renderAreaY = y;
    m_info.renderAreaWidth = width;
    m_info.renderAreaHeight = height;
    return *this;
}

DynamicRenderingBuilder& DynamicRenderingBuilder::SetLayerCount(u32 count) {
    m_info.layerCount = count;
    return *this;
}

DynamicRenderingBuilder& DynamicRenderingBuilder::SetViewMask(u32 mask) {
    m_info.viewMask = mask;
    return *this;
}

DynamicRenderingBuilder& DynamicRenderingBuilder::AddColorAttachment(const DynamicColorAttachment& attachment) {
    m_info.colorAttachments.push_back(attachment);
    return *this;
}

DynamicRenderingBuilder& DynamicRenderingBuilder::AddColorAttachment(
    TextureHandle view, Format format, AttachmentLoadOp loadOp, ClearValue clear) {
    DynamicColorAttachment att;
    att.imageView = view;
    att.format = format;
    att.loadOp = loadOp;
    att.storeOp = AttachmentStoreOp::Store;
    att.clearValue = clear;
    m_info.colorAttachments.push_back(att);
    return *this;
}

DynamicRenderingBuilder& DynamicRenderingBuilder::SetDepthAttachment(const DynamicDepthAttachment& attachment) {
    m_info.depthAttachment = attachment;
    m_info.hasDepth = true;
    return *this;
}

DynamicRenderingBuilder& DynamicRenderingBuilder::SetDepthAttachment(
    TextureHandle view, Format format, AttachmentLoadOp loadOp, ClearValue clear) {
    DynamicDepthAttachment att;
    att.imageView = view;
    att.format = format;
    att.loadOp = loadOp;
    att.storeOp = AttachmentStoreOp::Store;
    att.clearValue = clear;
    m_info.depthAttachment = att;
    m_info.hasDepth = true;
    return *this;
}

DynamicRenderingBuilder& DynamicRenderingBuilder::SetStencilAttachment(const DynamicStencilAttachment& attachment) {
    m_info.stencilAttachment = attachment;
    m_info.hasStencil = true;
    return *this;
}

DynamicRenderingInfo DynamicRenderingBuilder::Build() const {
    return m_info;
}

DynamicRenderingInfo DynamicRenderingBuilder::GBufferPass(
    TextureHandle albedo, TextureHandle normal, TextureHandle motion,
    TextureHandle depth, u32 width, u32 height) {
    return DynamicRenderingBuilder()
        .SetRenderArea(0, 0, width, height)
        .AddColorAttachment(albedo, Format::RGBA8_UNORM, AttachmentLoadOp::Clear, ClearValue::Color(0, 0, 0, 0))
        .AddColorAttachment(normal, Format::RGB10A2_UNORM, AttachmentLoadOp::Clear, ClearValue::Color(0, 0, 0, 0))
        .AddColorAttachment(motion, Format::RG16_FLOAT, AttachmentLoadOp::Clear, ClearValue::Color(0, 0, 0, 0))
        .SetDepthAttachment(depth, Format::D32_FLOAT, AttachmentLoadOp::Clear, ClearValue::Depth(0.0f, 0))
        .Build();
}

DynamicRenderingInfo DynamicRenderingBuilder::DepthOnlyPass(
    TextureHandle depth, u32 width, u32 height) {
    return DynamicRenderingBuilder()
        .SetRenderArea(0, 0, width, height)
        .SetDepthAttachment(depth, Format::D32_FLOAT, AttachmentLoadOp::Clear, ClearValue::Depth(0.0f, 0))
        .Build();
}

DynamicRenderingInfo DynamicRenderingBuilder::PostProcessPass(
    TextureHandle output, u32 width, u32 height) {
    return DynamicRenderingBuilder()
        .SetRenderArea(0, 0, width, height)
        .AddColorAttachment(output, Format::RGBA16_FLOAT, AttachmentLoadOp::DontCare)
        .Build();
}

DynamicRenderingInfo DynamicRenderingBuilder::ShadowPass(
    TextureHandle shadowMap, u32 resolution) {
    return DynamicRenderingBuilder()
        .SetRenderArea(0, 0, resolution, resolution)
        .SetDepthAttachment(shadowMap, Format::D32_FLOAT, AttachmentLoadOp::Clear, ClearValue::Depth(1.0f, 0))
        .Build();
}

void DynamicRenderingBuilder::BeginRendering(ICommandList* cmd, const DynamicRenderingInfo& info) {
    // TODO: Build and call vkCmdBeginRendering (Vulkan 1.3 core)
    //
    // VkRenderingInfo renderingInfo{};
    // renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    // renderingInfo.renderArea.offset = { (i32)info.renderAreaX, (i32)info.renderAreaY };
    // renderingInfo.renderArea.extent = { info.renderAreaWidth, info.renderAreaHeight };
    // renderingInfo.layerCount = info.layerCount;
    // renderingInfo.viewMask = info.viewMask;
    //
    // std::vector<VkRenderingAttachmentInfo> colorInfos;
    // for (const auto& att : info.colorAttachments) {
    //     VkRenderingAttachmentInfo ci{};
    //     ci.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    //     ci.imageView = att.imageView; // Cast to VkImageView
    //     ci.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    //     ci.loadOp = toVkLoadOp(att.loadOp);
    //     ci.storeOp = toVkStoreOp(att.storeOp);
    //     ci.clearValue.color = {{ att.clearValue.color.r, att.clearValue.color.g,
    //                              att.clearValue.color.b, att.clearValue.color.a }};
    //     if (att.resolveView.IsValid()) {
    //         ci.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
    //         ci.resolveImageView = att.resolveView;
    //         ci.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    //     }
    //     colorInfos.push_back(ci);
    // }
    // renderingInfo.colorAttachmentCount = (u32)colorInfos.size();
    // renderingInfo.pColorAttachments = colorInfos.data();
    //
    // VkRenderingAttachmentInfo depthInfo{};
    // if (info.hasDepth) {
    //     depthInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    //     depthInfo.imageView = info.depthAttachment.imageView;
    //     depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    //     depthInfo.loadOp = toVkLoadOp(info.depthAttachment.loadOp);
    //     depthInfo.storeOp = toVkStoreOp(info.depthAttachment.storeOp);
    //     depthInfo.clearValue.depthStencil = { info.depthAttachment.clearValue.depthStencil.depth,
    //                                           info.depthAttachment.clearValue.depthStencil.stencil };
    //     renderingInfo.pDepthAttachment = &depthInfo;
    // }
    //
    // vkCmdBeginRendering(cmd->GetVkCommandBuffer(), &renderingInfo);

    (void)cmd;
    (void)info;
}

void DynamicRenderingBuilder::EndRendering(ICommandList* cmd) {
    // TODO: vkCmdEndRendering(cmd->GetVkCommandBuffer());
    (void)cmd;
}

} // namespace nge::rhi
