#include "engine/rhi/vulkan/vk_swapchain_manager.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool SwapchainManager::Init(IDevice* device) {
    m_device = device;
    m_nextId = 0;
    m_totalPresents = 0;
    m_recreations = 0;
    NGE_LOG_INFO("Swapchain manager initialized");
    return true;
}

void SwapchainManager::Shutdown() {
    for (auto& sc : m_swapchains) {
        if (sc.handle != 0) {
            // TODO: vkDestroySwapchainKHR(device, sc.handle, nullptr);
            // vkDestroySemaphore(device, sc.acquireSemaphore, nullptr);
            // vkDestroySemaphore(device, sc.presentSemaphore, nullptr);
        }
    }
    m_swapchains.clear();
}

u32 SwapchainManager::CreateSwapchain(const SwapchainDesc& desc) {
    std::lock_guard lock(m_mutex);

    SwapchainState state;
    state.desc = desc;

    // TODO: Create VkSwapchainKHR
    // VkSwapchainCreateInfoKHR ci{};
    // ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    // ci.surface = (VkSurfaceKHR)desc.surface;
    // ci.minImageCount = desc.imageCount;
    // ci.imageFormat = toVkFormat(desc.format);
    // ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    // ci.imageExtent = { desc.width, desc.height };
    // ci.imageArrayLayers = 1;
    // ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    // ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    // ci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    // ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    // ci.presentMode = desc.vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;
    // ci.clipped = VK_TRUE;
    // vkCreateSwapchainKHR(device, &ci, nullptr, &state.handle);

    state.handle = static_cast<u64>(m_nextId + 1); // Stub

    // Get swapchain images
    // u32 imageCount;
    // vkGetSwapchainImagesKHR(device, state.handle, &imageCount, nullptr);
    // std::vector<VkImage> images(imageCount);
    // vkGetSwapchainImagesKHR(device, state.handle, &imageCount, images.data());
    state.images.resize(desc.imageCount);
    for (u32 i = 0; i < desc.imageCount; ++i) {
        state.images[i].index = i;
        state.images[i].acquired = false;
    }

    // Create semaphores
    // VkSemaphoreCreateInfo semCI{};
    // semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    // vkCreateSemaphore(device, &semCI, nullptr, &state.acquireSemaphore);
    // vkCreateSemaphore(device, &semCI, nullptr, &state.presentSemaphore);
    state.acquireSemaphore = state.handle * 10 + 1;
    state.presentSemaphore = state.handle * 10 + 2;

    u32 id = m_nextId++;
    m_swapchains.push_back(std::move(state));

    NGE_LOG_INFO("Swapchain created: {}x{}, {} images, vsync={}, name='{}'",
                 desc.width, desc.height, desc.imageCount, desc.vsync, desc.debugName);
    return id;
}

void SwapchainManager::DestroySwapchain(u32 swapchainId) {
    std::lock_guard lock(m_mutex);
    if (swapchainId >= m_swapchains.size()) return;

    auto& sc = m_swapchains[swapchainId];
    // TODO: vkDestroySwapchainKHR, vkDestroySemaphore×2
    sc.handle = 0;
    sc.images.clear();
}

bool SwapchainManager::AcquireNextImage(u32 swapchainId, u64 timeoutNs) {
    std::lock_guard lock(m_mutex);
    if (swapchainId >= m_swapchains.size()) return false;

    auto& sc = m_swapchains[swapchainId];
    if (sc.handle == 0) return false;

    // TODO:
    // VkResult result = vkAcquireNextImageKHR(device, sc.handle, timeoutNs,
    //                                          sc.acquireSemaphore, VK_NULL_HANDLE,
    //                                          &sc.currentImageIndex);
    // if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    //     sc.needsRecreate = true;
    //     return false;
    // }
    // if (result == VK_SUBOPTIMAL_KHR) {
    //     sc.suboptimal = true;
    // }

    (void)timeoutNs;
    sc.currentImageIndex = (sc.currentImageIndex + 1) % static_cast<u32>(sc.images.size());
    if (sc.currentImageIndex < sc.images.size()) {
        sc.images[sc.currentImageIndex].acquired = true;
    }

    return true;
}

bool SwapchainManager::Present(u32 swapchainId, u64 queueHandle) {
    std::lock_guard lock(m_mutex);
    if (swapchainId >= m_swapchains.size()) return false;

    auto& sc = m_swapchains[swapchainId];
    if (sc.handle == 0) return false;

    // TODO:
    // VkPresentInfoKHR presentInfo{};
    // presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    // presentInfo.waitSemaphoreCount = 1;
    // presentInfo.pWaitSemaphores = &sc.presentSemaphore;
    // presentInfo.swapchainCount = 1;
    // VkSwapchainKHR swapchain = sc.handle;
    // presentInfo.pSwapchains = &swapchain;
    // presentInfo.pImageIndices = &sc.currentImageIndex;
    // VkResult result = vkQueuePresentKHR((VkQueue)queueHandle, &presentInfo);
    // if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
    //     sc.needsRecreate = true;
    // }

    (void)queueHandle;
    if (sc.currentImageIndex < sc.images.size()) {
        sc.images[sc.currentImageIndex].acquired = false;
    }
    m_totalPresents++;

    return true;
}

void SwapchainManager::OnResize(u32 swapchainId, u32 newWidth, u32 newHeight) {
    std::lock_guard lock(m_mutex);
    if (swapchainId >= m_swapchains.size()) return;

    auto& sc = m_swapchains[swapchainId];
    if (sc.desc.width != newWidth || sc.desc.height != newHeight) {
        sc.desc.width = newWidth;
        sc.desc.height = newHeight;
        sc.needsRecreate = true;
        NGE_LOG_INFO("Swapchain {} marked for recreation: {}x{}", swapchainId, newWidth, newHeight);
    }
}

bool SwapchainManager::Recreate(u32 swapchainId) {
    std::lock_guard lock(m_mutex);
    if (swapchainId >= m_swapchains.size()) return false;

    auto& sc = m_swapchains[swapchainId];

    // TODO: Destroy old, create new with updated dimensions
    // VkSwapchainKHR oldSwapchain = sc.handle;
    // ci.oldSwapchain = oldSwapchain; // Allow driver to reuse resources
    // vkCreateSwapchainKHR(...);
    // vkDestroySwapchainKHR(device, oldSwapchain, nullptr);

    sc.needsRecreate = false;
    sc.suboptimal = false;
    m_recreations++;

    NGE_LOG_INFO("Swapchain {} recreated: {}x{}", swapchainId, sc.desc.width, sc.desc.height);
    return true;
}

TextureHandle SwapchainManager::GetCurrentImage(u32 swapchainId) const {
    std::lock_guard lock(m_mutex);
    if (swapchainId >= m_swapchains.size()) return {};
    const auto& sc = m_swapchains[swapchainId];
    if (sc.currentImageIndex < sc.images.size()) {
        return sc.images[sc.currentImageIndex].handle;
    }
    return {};
}

u32 SwapchainManager::GetCurrentImageIndex(u32 swapchainId) const {
    std::lock_guard lock(m_mutex);
    if (swapchainId >= m_swapchains.size()) return 0;
    return m_swapchains[swapchainId].currentImageIndex;
}

const SwapchainDesc& SwapchainManager::GetDesc(u32 swapchainId) const {
    std::lock_guard lock(m_mutex);
    return m_swapchains[swapchainId].desc;
}

u32 SwapchainManager::GetImageCount(u32 swapchainId) const {
    std::lock_guard lock(m_mutex);
    if (swapchainId >= m_swapchains.size()) return 0;
    return static_cast<u32>(m_swapchains[swapchainId].images.size());
}

u64 SwapchainManager::GetAcquireSemaphore(u32 swapchainId) const {
    std::lock_guard lock(m_mutex);
    if (swapchainId >= m_swapchains.size()) return 0;
    return m_swapchains[swapchainId].acquireSemaphore;
}

u64 SwapchainManager::GetPresentSemaphore(u32 swapchainId) const {
    std::lock_guard lock(m_mutex);
    if (swapchainId >= m_swapchains.size()) return 0;
    return m_swapchains[swapchainId].presentSemaphore;
}

SwapchainManagerStats SwapchainManager::GetStats() const {
    std::lock_guard lock(m_mutex);
    SwapchainManagerStats stats{};
    for (const auto& sc : m_swapchains) {
        if (sc.handle != 0) stats.activeSwapchains++;
    }
    stats.totalPresents = m_totalPresents;
    stats.recreations = m_recreations;
    return stats;
}

} // namespace nge::rhi
