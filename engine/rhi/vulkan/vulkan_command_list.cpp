#include "engine/rhi/vulkan/vulkan_device.h"
#include "engine/rhi/common/rhi_format_utils.h"
#include "engine/core/assert.h"
#include <cstring>

namespace nge::rhi::vulkan {

// ─── VulkanCommandList Implementation ────────────────────────────────────

void VulkanCommandList::Begin() {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_cb, &beginInfo);
}

void VulkanCommandList::End() {
    vkEndCommandBuffer(m_cb);
}

void VulkanCommandList::BufferBarrier(BufferHandle buffer, ResourceState before, ResourceState after) {
    const auto& buf = m_device->GetBuffer(buffer);

    VkBufferMemoryBarrier2 barrier{};
    barrier.sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask  = m_device->ToVkStage(before);
    barrier.srcAccessMask = m_device->ToVkAccess(before);
    barrier.dstStageMask  = m_device->ToVkStage(after);
    barrier.dstAccessMask = m_device->ToVkAccess(after);
    barrier.buffer        = buf.buffer;
    barrier.offset        = 0;
    barrier.size          = VK_WHOLE_SIZE;

    VkDependencyInfo depInfo{};
    depInfo.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.bufferMemoryBarrierCount = 1;
    depInfo.pBufferMemoryBarriers    = &barrier;

    vkCmdPipelineBarrier2(m_cb, &depInfo);
}

void VulkanCommandList::TextureBarrier(TextureHandle texture, ResourceState before, ResourceState after) {
    const auto& tex = m_device->GetTexture(texture);

    VkImageMemoryBarrier2 barrier{};
    barrier.sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask  = m_device->ToVkStage(before);
    barrier.srcAccessMask = m_device->ToVkAccess(before);
    barrier.dstStageMask  = m_device->ToVkStage(after);
    barrier.dstAccessMask = m_device->ToVkAccess(after);
    barrier.oldLayout     = m_device->ToVkLayout(before);
    barrier.newLayout     = m_device->ToVkLayout(after);
    barrier.image         = tex.image;
    barrier.subresourceRange.aspectMask     = static_cast<VkImageAspectFlags>(FormatUtils::GetAspectFlags(tex.format));
    barrier.subresourceRange.baseMipLevel   = 0;
    barrier.subresourceRange.levelCount     = VK_REMAINING_MIP_LEVELS;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount     = VK_REMAINING_ARRAY_LAYERS;

    VkDependencyInfo depInfo{};
    depInfo.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers    = &barrier;

    vkCmdPipelineBarrier2(m_cb, &depInfo);
}

void VulkanCommandList::BeginRendering(
    const TextureHandle* colorTargets, u32 colorCount,
    TextureHandle depthTarget,
    const ClearValue* clearValues,
    const Viewport& viewport, const Scissor& scissor,
    const LoadOp* colorLoadOps, const LoadOp depthLoadOp)
{
    auto toVkLoadOp = [](LoadOp op) -> VkAttachmentLoadOp {
        switch (op) {
            case LoadOp::Load:     return VK_ATTACHMENT_LOAD_OP_LOAD;
            case LoadOp::Clear:    return VK_ATTACHMENT_LOAD_OP_CLEAR;
            case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            default: return VK_ATTACHMENT_LOAD_OP_CLEAR;
        }
    };

    std::vector<VkRenderingAttachmentInfo> colorAttachments(colorCount);
    for (u32 i = 0; i < colorCount; ++i) {
        const auto& tex = m_device->GetTexture(colorTargets[i]);
        colorAttachments[i] = {};
        colorAttachments[i].sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachments[i].imageView   = tex.view;
        colorAttachments[i].imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorAttachments[i].loadOp      = colorLoadOps ? toVkLoadOp(colorLoadOps[i]) : VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachments[i].storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        if (clearValues) {
            std::memcpy(&colorAttachments[i].clearValue.color, clearValues[i].color, sizeof(f32) * 4);
        }
    }

    VkRenderingAttachmentInfo depthAttachment{};
    bool hasDepth = depthTarget.IsValid();
    if (hasDepth) {
        const auto& depthTex = m_device->GetTexture(depthTarget);
        depthAttachment.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.imageView   = depthTex.view;
        depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachment.loadOp      = toVkLoadOp(depthLoadOp);
        depthAttachment.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.clearValue.depthStencil = {1.0f, 0};
    }

    VkRenderingInfo renderInfo{};
    renderInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea.offset    = {0, 0};
    renderInfo.renderArea.extent    = {static_cast<u32>(viewport.width), static_cast<u32>(viewport.height)};
    renderInfo.layerCount           = 1;
    renderInfo.colorAttachmentCount = colorCount;
    renderInfo.pColorAttachments    = colorAttachments.data();
    renderInfo.pDepthAttachment     = hasDepth ? &depthAttachment : nullptr;

    vkCmdBeginRendering(m_cb, &renderInfo);

    // Set dynamic viewport and scissor
    VkViewport vp{viewport.x, viewport.y, viewport.width, viewport.height, viewport.minDepth, viewport.maxDepth};
    vkCmdSetViewport(m_cb, 0, 1, &vp);

    VkRect2D sc{{scissor.x, scissor.y}, {scissor.width, scissor.height}};
    vkCmdSetScissor(m_cb, 0, 1, &sc);
}

void VulkanCommandList::EndRendering() {
    vkCmdEndRendering(m_cb);
}

void VulkanCommandList::BindGraphicsPipeline(PipelineHandle pipeline) {
    const auto& p = m_device->GetPipeline(pipeline);
    vkCmdBindPipeline(m_cb, VK_PIPELINE_BIND_POINT_GRAPHICS, p.pipeline);
    m_currentPipeline = pipeline;
}

void VulkanCommandList::BindComputePipeline(PipelineHandle pipeline) {
    const auto& p = m_device->GetPipeline(pipeline);
    vkCmdBindPipeline(m_cb, VK_PIPELINE_BIND_POINT_COMPUTE, p.pipeline);
    m_currentPipeline = pipeline;
}

void VulkanCommandList::BindRayTracingPipeline(PipelineHandle pipeline) {
    const auto& p = m_device->GetPipeline(pipeline);
    vkCmdBindPipeline(m_cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, p.pipeline);
    m_currentPipeline = pipeline;
}

void VulkanCommandList::SetPushConstants(const void* data, u32 size, u32 offset) {
    const auto& p = m_device->GetPipeline(m_currentPipeline);
    vkCmdPushConstants(m_cb, p.layout, VK_SHADER_STAGE_ALL, offset, size, data);
}

void VulkanCommandList::BindDescriptorSet(DescriptorSetHandle /*set*/, u32 setIndex) {
    // Bind the global bindless descriptor set
    const auto& p = m_device->GetPipeline(m_currentPipeline);
    VkDescriptorSet globalSet = VK_NULL_HANDLE; // Retrieved from device
    // In a full implementation, we'd retrieve the actual VkDescriptorSet
    // For now, this is wired up through the device's bindless set
    (void)setIndex;
    (void)globalSet;
}

void VulkanCommandList::BindVertexBuffer(BufferHandle buffer, u32 binding, u64 offset) {
    const auto& buf = m_device->GetBuffer(buffer);
    VkDeviceSize vkOffset = offset;
    vkCmdBindVertexBuffers(m_cb, binding, 1, &buf.buffer, &vkOffset);
}

void VulkanCommandList::BindIndexBuffer(BufferHandle buffer, IndexType type, u64 offset) {
    const auto& buf = m_device->GetBuffer(buffer);
    VkIndexType vkType = type == IndexType::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(m_cb, buf.buffer, offset, vkType);
}

void VulkanCommandList::Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) {
    vkCmdDraw(m_cb, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandList::DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance) {
    vkCmdDrawIndexed(m_cb, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandList::DrawIndirect(BufferHandle buffer, u64 offset, u32 drawCount, u32 stride) {
    const auto& buf = m_device->GetBuffer(buffer);
    vkCmdDrawIndirect(m_cb, buf.buffer, offset, drawCount, stride);
}

void VulkanCommandList::DrawIndexedIndirect(BufferHandle buffer, u64 offset, u32 drawCount, u32 stride) {
    const auto& buf = m_device->GetBuffer(buffer);
    vkCmdDrawIndexedIndirect(m_cb, buf.buffer, offset, drawCount, stride);
}

void VulkanCommandList::DrawMeshTasks(u32 groupCountX, u32 groupCountY, u32 groupCountZ) {
    // VK_EXT_mesh_shader
    auto fn = reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(
        vkGetDeviceProcAddr(m_device->GetVkDevice(), "vkCmdDrawMeshTasksEXT"));
    if (fn) fn(m_cb, groupCountX, groupCountY, groupCountZ);
}

void VulkanCommandList::DrawMeshTasksIndirect(BufferHandle buffer, u64 offset, u32 drawCount, u32 stride) {
    const auto& buf = m_device->GetBuffer(buffer);
    auto fn = reinterpret_cast<PFN_vkCmdDrawMeshTasksIndirectEXT>(
        vkGetDeviceProcAddr(m_device->GetVkDevice(), "vkCmdDrawMeshTasksIndirectEXT"));
    if (fn) fn(m_cb, buf.buffer, offset, drawCount, stride);
}

void VulkanCommandList::Dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ) {
    vkCmdDispatch(m_cb, groupCountX, groupCountY, groupCountZ);
}

void VulkanCommandList::DispatchIndirect(BufferHandle buffer, u64 offset) {
    const auto& buf = m_device->GetBuffer(buffer);
    vkCmdDispatchIndirect(m_cb, buf.buffer, offset);
}

void VulkanCommandList::TraceRays(u32 /*width*/, u32 /*height*/, u32 /*depth*/) {
    // TODO: Implement when RT pipeline is complete
}

void VulkanCommandList::CopyBuffer(BufferHandle src, u64 srcOffset, BufferHandle dst, u64 dstOffset, u64 size) {
    const auto& srcBuf = m_device->GetBuffer(src);
    const auto& dstBuf = m_device->GetBuffer(dst);
    VkBufferCopy region{srcOffset, dstOffset, size};
    vkCmdCopyBuffer(m_cb, srcBuf.buffer, dstBuf.buffer, 1, &region);
}

void VulkanCommandList::CopyBufferToTexture(BufferHandle src, TextureHandle dst, u32 mipLevel, u32 arrayLayer) {
    const auto& srcBuf = m_device->GetBuffer(src);
    const auto& dstTex = m_device->GetTexture(dst);

    VkBufferImageCopy region{};
    region.bufferOffset      = 0;
    region.bufferRowLength   = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = mipLevel;
    region.imageSubresource.baseArrayLayer = arrayLayer;
    region.imageSubresource.layerCount     = 1;
    region.imageExtent = {dstTex.width >> mipLevel, dstTex.height >> mipLevel, 1};

    vkCmdCopyBufferToImage(m_cb, srcBuf.buffer, dstTex.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void VulkanCommandList::CopyTextureToBuffer(TextureHandle src, BufferHandle dst, u32 mipLevel, u32 arrayLayer) {
    const auto& srcTex = m_device->GetTexture(src);
    const auto& dstBuf = m_device->GetBuffer(dst);

    VkBufferImageCopy region{};
    region.bufferOffset      = 0;
    region.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel       = mipLevel;
    region.imageSubresource.baseArrayLayer = arrayLayer;
    region.imageSubresource.layerCount     = 1;
    region.imageExtent = {srcTex.width >> mipLevel, srcTex.height >> mipLevel, 1};

    vkCmdCopyImageToBuffer(m_cb, srcTex.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           dstBuf.buffer, 1, &region);
}

void VulkanCommandList::BuildAccelerationStructure(AccelStructHandle /*accelStruct*/) {
    // TODO: Implement BLAS/TLAS build
}

void VulkanCommandList::FillBuffer(BufferHandle buffer, u64 offset, u64 size, u32 value) {
    const auto& buf = m_device->GetBuffer(buffer);
    vkCmdFillBuffer(m_cb, buf.buffer, offset, size, value);
}

void VulkanCommandList::UpdateBuffer(BufferHandle buffer, u64 offset, u64 size, const void* data) {
    const auto& buf = m_device->GetBuffer(buffer);
    vkCmdUpdateBuffer(m_cb, buf.buffer, offset, size, data);
}

void VulkanCommandList::BlitTexture(TextureHandle src, TextureHandle dst, u32 dstWidth, u32 dstHeight) {
    const auto& srcTex = m_device->GetTexture(src);
    const auto& dstTex = m_device->GetTexture(dst);

    VkImageBlit region{};
    region.srcSubresource.aspectMask = static_cast<VkImageAspectFlags>(FormatUtils::GetAspectFlags(srcTex.format));
    region.srcSubresource.mipLevel = 0;
    region.srcSubresource.baseArrayLayer = 0;
    region.srcSubresource.layerCount = 1;
    region.srcOffsets[1] = { static_cast<i32>(srcTex.width), static_cast<i32>(srcTex.height), 1 };
    region.dstSubresource.aspectMask = static_cast<VkImageAspectFlags>(FormatUtils::GetAspectFlags(dstTex.format));
    region.dstSubresource.mipLevel = 0;
    region.dstSubresource.baseArrayLayer = 0;
    region.dstSubresource.layerCount = 1;
    region.dstOffsets[1] = {
        static_cast<i32>(dstWidth == 0 ? dstTex.width : dstWidth),
        static_cast<i32>(dstHeight == 0 ? dstTex.height : dstHeight),
        1
    };

    vkCmdBlitImage(
        m_cb,
        srcTex.image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dstTex.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region,
        VK_FILTER_LINEAR);
}

void VulkanCommandList::SetViewport(const Viewport& viewport) {
    VkViewport vkViewport{viewport.x, viewport.y, viewport.width, viewport.height, viewport.minDepth, viewport.maxDepth};
    vkCmdSetViewport(m_cb, 0, 1, &vkViewport);
}

void VulkanCommandList::SetScissor(const Scissor& scissor) {
    VkRect2D vkScissor{{scissor.x, scissor.y}, {scissor.width, scissor.height}};
    vkCmdSetScissor(m_cb, 0, 1, &vkScissor);
}

void VulkanCommandList::WaitSemaphore(u64 semaphore, u64 value) {
    (void)semaphore;
    (void)value;
}

void VulkanCommandList::SignalSemaphore(u64 semaphore, u64 value) {
    (void)semaphore;
    (void)value;
}

void VulkanCommandList::BeginDebugLabel(const char* name, f32 r, f32 g, f32 b) {
    auto fn = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(m_device->GetVkDevice(), "vkCmdBeginDebugUtilsLabelEXT"));
    if (fn) {
        VkDebugUtilsLabelEXT label{};
        label.sType      = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
        label.pLabelName = name;
        label.color[0] = r; label.color[1] = g; label.color[2] = b; label.color[3] = 1.0f;
        fn(m_cb, &label);
    }
}

void VulkanCommandList::EndDebugLabel() {
    auto fn = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
        vkGetDeviceProcAddr(m_device->GetVkDevice(), "vkCmdEndDebugUtilsLabelEXT"));
    if (fn) fn(m_cb);
}

} // namespace nge::rhi::vulkan
