#include "engine/rhi/common/rhi_device.h"
#include "engine/core/logging/log.h"

#if NGE_ENABLE_VULKAN
#include "engine/rhi/vulkan/vulkan_device.h"
#endif

// Device factory. Lives in rhi/common (always compiled) so executables that
// link the engine without the Vulkan backend still resolve IDevice::Create —
// they get a clear error and nullptr instead of a link failure.

namespace nge::rhi {

std::unique_ptr<IDevice> IDevice::Create(GraphicsAPI api) {
    switch (api) {
        case GraphicsAPI::Vulkan:
#if NGE_ENABLE_VULKAN
            return std::make_unique<vulkan::VulkanDevice>();
#else
            NGE_LOG_ERROR("Vulkan backend not compiled in (NGE_ENABLE_VULKAN=OFF)");
            return nullptr;
#endif
        case GraphicsAPI::DirectX12:
            NGE_LOG_ERROR("DX12 backend not yet implemented");
            return nullptr;
    }
    return nullptr;
}

} // namespace nge::rhi
