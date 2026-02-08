#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/rhi/common/rhi_timeline_fence.h"
#include <vector>

namespace nge::rhi::vulkan {

// ─── Swapchain Presenter ─────────────────────────────────────────────────
// Manages VkSwapchainKHR lifecycle and final blit-to-screen.
// Handles acquire → blit → present with proper synchronization.
//
// Usage:
//   presenter.Init(device, surface, width, height);
//   u32 imageIdx = presenter.AcquireNextImage();
//   // ... render to offscreen target ...
//   presenter.BlitToSwapchain(cmd, sourceTexture);
//   presenter.Present();

struct SwapchainConfig {
    u32  width = 1920;
    u32  height = 1080;
    bool vsync = true;
    bool hdr = false;           // HDR10 output if supported
    u32  imageCount = 3;        // Triple buffering
    Format preferredFormat = Format::BGRA8_SRGB;
};

class SwapchainPresenter {
public:
    bool Init(IDevice* device, void* surface, const SwapchainConfig& config = {});
    void Shutdown();

    // Acquire the next swapchain image (returns image index)
    u32 AcquireNextImage();

    // Blit the rendered output to the current swapchain image
    void BlitToSwapchain(ICommandList* cmd, TextureHandle sourceTexture);

    // Present the current swapchain image
    bool Present();

    // Recreate swapchain (call on window resize)
    bool Recreate(u32 newWidth, u32 newHeight);

    // Getters
    u32 GetWidth() const { return m_config.width; }
    u32 GetHeight() const { return m_config.height; }
    u32 GetImageCount() const { return static_cast<u32>(m_images.size()); }
    u32 GetCurrentImageIndex() const { return m_currentImageIndex; }
    Format GetFormat() const { return m_actualFormat; }
    bool IsVSync() const { return m_config.vsync; }
    bool NeedsRecreate() const { return m_needsRecreate; }

    // Toggle vsync at runtime
    void SetVSync(bool enable);

private:
    void CreateSwapchain();
    void DestroySwapchain();
    void CreateSyncObjects();
    void DestroySyncObjects();

    IDevice* m_device = nullptr;
    void* m_surface = nullptr;  // VkSurfaceKHR
    SwapchainConfig m_config;
    Format m_actualFormat = Format::BGRA8_SRGB;

    // Swapchain handle (VkSwapchainKHR as u64)
    u64 m_swapchain = 0;

    // Swapchain images
    struct SwapchainImage {
        TextureHandle texture;
        u64 imageView;  // VkImageView as u64
    };
    std::vector<SwapchainImage> m_images;
    u32 m_currentImageIndex = 0;

    // Synchronization
    struct FrameSync {
        u64 imageAvailableSemaphore;   // VkSemaphore: signaled when image acquired
        u64 renderFinishedSemaphore;   // VkSemaphore: signaled when rendering done
        u64 inFlightFence;             // VkFence: CPU wait for frame completion
    };
    std::vector<FrameSync> m_frameSyncs;
    u32 m_currentFrame = 0;

    bool m_needsRecreate = false;
};

} // namespace nge::rhi::vulkan
