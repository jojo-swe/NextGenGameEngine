#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <mutex>

namespace nge::rhi {

// ─── Vulkan Swapchain Image Manager ──────────────────────────────────────
// Multi-swapchain support for editor viewports. Each window/surface gets
// its own swapchain with independent present mode, image count, and
// resize handling. The editor may have multiple viewports rendering to
// different surfaces simultaneously.

struct SwapchainDesc {
    u64    surface = 0;          // VkSurfaceKHR
    u32    width = 0;
    u32    height = 0;
    Format format = Format::BGRA8_UNORM;
    u32    imageCount = 3;       // Triple buffering
    bool   vsync = true;
    std::string debugName;
};

struct SwapchainImage {
    TextureHandle handle;
    u32           index;
    bool          acquired = false;
};

struct SwapchainState {
    u64    handle = 0;           // VkSwapchainKHR
    SwapchainDesc desc;
    std::vector<SwapchainImage> images;
    u32    currentImageIndex = 0;
    u64    acquireSemaphore = 0; // VkSemaphore for image acquire
    u64    presentSemaphore = 0; // VkSemaphore for present
    bool   needsRecreate = false;
    bool   suboptimal = false;
};

struct SwapchainManagerStats {
    u32 activeSwapchains;
    u32 totalPresents;
    u32 recreations;
};

class SwapchainManager {
public:
    bool Init(IDevice* device);
    void Shutdown();

    // Create a swapchain for a surface
    u32 CreateSwapchain(const SwapchainDesc& desc);

    // Destroy a swapchain
    void DestroySwapchain(u32 swapchainId);

    // Acquire the next image from a swapchain
    bool AcquireNextImage(u32 swapchainId, u64 timeoutNs = UINT64_MAX);

    // Present the current image
    bool Present(u32 swapchainId, u64 queueHandle);

    // Handle resize (marks for recreation)
    void OnResize(u32 swapchainId, u32 newWidth, u32 newHeight);

    // Recreate swapchain (after resize or suboptimal)
    bool Recreate(u32 swapchainId);

    // Get the current image for rendering
    TextureHandle GetCurrentImage(u32 swapchainId) const;
    u32 GetCurrentImageIndex(u32 swapchainId) const;

    // Get swapchain properties
    const SwapchainDesc& GetDesc(u32 swapchainId) const;
    u32 GetImageCount(u32 swapchainId) const;

    // Get semaphores for synchronization
    u64 GetAcquireSemaphore(u32 swapchainId) const;
    u64 GetPresentSemaphore(u32 swapchainId) const;

    SwapchainManagerStats GetStats() const;

private:
    IDevice* m_device = nullptr;
    std::vector<SwapchainState> m_swapchains;
    u32 m_nextId = 0;
    u32 m_totalPresents = 0;
    u32 m_recreations = 0;
    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
