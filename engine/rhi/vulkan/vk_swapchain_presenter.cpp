#include "engine/rhi/vulkan/vk_swapchain_presenter.h"
#include "engine/core/logging/log.h"

namespace nge::rhi::vulkan {

bool SwapchainPresenter::Init(IDevice* device, void* surface, const SwapchainConfig& config) {
    m_device = device;
    m_surface = surface;
    m_config = config;

    CreateSwapchain();
    CreateSyncObjects();

    NGE_LOG_INFO("Swapchain presenter initialized: {}x{}, {} images, vsync={}",
                 config.width, config.height, m_images.size(), config.vsync ? "on" : "off");
    return true;
}

void SwapchainPresenter::Shutdown() {
    if (!m_device) return;

    // Wait for all in-flight frames
    // TODO: vkDeviceWaitIdle

    DestroySyncObjects();
    DestroySwapchain();
}

u32 SwapchainPresenter::AcquireNextImage() {
    auto& sync = m_frameSyncs[m_currentFrame];

    // TODO: Wait for this frame's fence
    // vkWaitForFences(device, 1, &sync.inFlightFence, VK_TRUE, UINT64_MAX);
    // vkResetFences(device, 1, &sync.inFlightFence);

    // TODO: Acquire next swapchain image
    // VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
    //     sync.imageAvailableSemaphore, VK_NULL_HANDLE, &m_currentImageIndex);
    // if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    //     m_needsRecreate = true;
    // }

    m_currentImageIndex = m_currentFrame % static_cast<u32>(m_images.size()); // Stub
    return m_currentImageIndex;
}

void SwapchainPresenter::BlitToSwapchain(ICommandList* cmd, TextureHandle sourceTexture) {
    if (m_currentImageIndex >= m_images.size()) return;

    // Transition swapchain image to transfer dst
    auto& swapImg = m_images[m_currentImageIndex];
    cmd->TextureBarrier(swapImg.texture, ResourceState::Undefined, ResourceState::TransferDst);

    // Transition source to transfer src
    cmd->TextureBarrier(sourceTexture, ResourceState::ShaderRead, ResourceState::TransferSrc);

    // Blit (handles format conversion and scaling)
    cmd->BlitTexture(sourceTexture, swapImg.texture, m_config.width, m_config.height);

    // Transition swapchain image for presentation
    cmd->TextureBarrier(swapImg.texture, ResourceState::TransferDst, ResourceState::Present);

    // Transition source back
    cmd->TextureBarrier(sourceTexture, ResourceState::TransferSrc, ResourceState::ShaderRead);
}

bool SwapchainPresenter::Present() {
    auto& sync = m_frameSyncs[m_currentFrame];

    // TODO: Submit command buffer with proper semaphore signaling
    // VkSubmitInfo submitInfo{};
    // VkSemaphore waitSemaphores[] = { sync.imageAvailableSemaphore };
    // VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    // submitInfo.waitSemaphoreCount = 1;
    // submitInfo.pWaitSemaphores = waitSemaphores;
    // submitInfo.pWaitDstStageMask = waitStages;
    // submitInfo.signalSemaphoreCount = 1;
    // submitInfo.pSignalSemaphores = &sync.renderFinishedSemaphore;
    // vkQueueSubmit(graphicsQueue, 1, &submitInfo, sync.inFlightFence);

    // TODO: Present
    // VkPresentInfoKHR presentInfo{};
    // presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    // presentInfo.waitSemaphoreCount = 1;
    // presentInfo.pWaitSemaphores = &sync.renderFinishedSemaphore;
    // presentInfo.swapchainCount = 1;
    // presentInfo.pSwapchains = &swapchain;
    // presentInfo.pImageIndices = &m_currentImageIndex;
    // VkResult result = vkQueuePresentKHR(presentQueue, &presentInfo);
    // if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    //     m_needsRecreate = true;
    // }

    m_currentFrame = (m_currentFrame + 1) % static_cast<u32>(m_frameSyncs.size());
    return !m_needsRecreate;
}

bool SwapchainPresenter::Recreate(u32 newWidth, u32 newHeight) {
    // TODO: vkDeviceWaitIdle
    m_config.width = newWidth;
    m_config.height = newHeight;
    m_needsRecreate = false;

    DestroySwapchain();
    CreateSwapchain();

    NGE_LOG_INFO("Swapchain recreated: {}x{}", newWidth, newHeight);
    return true;
}

void SwapchainPresenter::SetVSync(bool enable) {
    if (m_config.vsync == enable) return;
    m_config.vsync = enable;
    m_needsRecreate = true; // Need to recreate with new present mode
}

void SwapchainPresenter::CreateSwapchain() {
    // TODO: Create VkSwapchainKHR
    // VkSwapchainCreateInfoKHR ci{};
    // ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    // ci.surface = static_cast<VkSurfaceKHR>(m_surface);
    // ci.minImageCount = m_config.imageCount;
    // ci.imageFormat = ConvertFormat(m_config.preferredFormat);
    // ci.imageColorSpace = m_config.hdr ? VK_COLOR_SPACE_HDR10_ST2084_EXT : VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    // ci.imageExtent = { m_config.width, m_config.height };
    // ci.imageArrayLayers = 1;
    // ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    // ci.presentMode = m_config.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;
    // ci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    // ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // ci.clipped = VK_TRUE;
    // vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain);

    // Stub: create placeholder images
    m_images.resize(m_config.imageCount);
    for (auto& img : m_images) {
        img.texture = TextureHandle{};
        img.imageView = 0;
    }

    m_actualFormat = m_config.preferredFormat;
}

void SwapchainPresenter::DestroySwapchain() {
    // TODO: Destroy VkImageViews and VkSwapchainKHR
    m_images.clear();
    m_swapchain = 0;
}

void SwapchainPresenter::CreateSyncObjects() {
    m_frameSyncs.resize(m_config.imageCount);

    for (auto& sync : m_frameSyncs) {
        // TODO: Create VkSemaphore (imageAvailable, renderFinished) and VkFence
        sync.imageAvailableSemaphore = 0;
        sync.renderFinishedSemaphore = 0;
        sync.inFlightFence = 0;
    }
}

void SwapchainPresenter::DestroySyncObjects() {
    // TODO: Destroy VkSemaphores and VkFences
    m_frameSyncs.clear();
}

} // namespace nge::rhi::vulkan
