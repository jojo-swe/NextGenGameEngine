#include "engine/rhi/vulkan/vulkan_device.h"
#include "engine/core/assert.h"

// ─── Volk: Vulkan meta-loader (loads all function pointers) ──────────────
// In production we use volk. For now, we dynamically load the Vulkan library.
// This file links against vulkan-1.lib and uses the Vulkan SDK's headers.
// volk will replace this when vcpkg dependencies are installed.

#if defined(NGE_PLATFORM_WINDOWS)
    #include <windows.h>
    #include <vulkan/vulkan_win32.h>
#endif

namespace nge::rhi::vulkan {

// ─── Debug callback ──────────────────────────────────────────────────────
#if defined(NGE_DEBUG) || defined(NGE_PROFILE)
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*userData*/)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        NGE_LOG_ERROR("[Vulkan] {}", data->pMessage);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        NGE_LOG_WARN("[Vulkan] {}", data->pMessage);
    } else {
        NGE_LOG_DEBUG("[Vulkan] {}", data->pMessage);
    }
    return VK_FALSE;
}
#endif

// ─── VulkanDevice Lifecycle ──────────────────────────────────────────────

VulkanDevice::~VulkanDevice() {
    if (m_initialized) Shutdown();
}

bool VulkanDevice::Init(void* windowHandle, void* instanceHandle, u32 width, u32 height) {
    NGE_LOG_INFO("Initializing Vulkan 1.3 RHI backend");

    m_buffers.resize(MAX_BUFFERS);
    m_textures.resize(MAX_TEXTURES);
    m_samplers.resize(MAX_SAMPLERS);
    m_shaders.resize(MAX_SHADERS);
    m_pipelines.resize(MAX_PIPELINES);
    m_accelStructs.resize(MAX_ACCEL_STRUCTS);

    if (!CreateInstance()) return false;

    // Create surface
#if defined(NGE_PLATFORM_WINDOWS)
    VkWin32SurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hinstance = static_cast<HINSTANCE>(instanceHandle);
    surfaceInfo.hwnd      = static_cast<HWND>(windowHandle);

    if (vkCreateWin32SurfaceKHR(m_instance, &surfaceInfo, nullptr, &m_surface) != VK_SUCCESS) {
        NGE_LOG_ERROR("Failed to create Win32 Vulkan surface");
        return false;
    }
#endif

    if (!SelectPhysicalDevice()) return false;
    if (!CreateLogicalDevice()) return false;
    if (!CreateSwapchain(width, height)) return false;
    if (!CreateSyncObjects()) return false;
    if (!CreateDescriptorPool()) return false;

    ProbeCapabilities();

    m_commandList.SetDevice(this);
    m_initialized = true;

    NGE_LOG_INFO("Vulkan RHI initialized — Device: {} | Tier: {}",
                 m_deviceName, static_cast<int>(m_featureTier));
    return true;
}

void VulkanDevice::Shutdown() {
    if (!m_initialized) return;
    WaitIdle();

    // Destroy sync objects
    for (auto& frame : m_frames) {
        if (frame.commandPool)    vkDestroyCommandPool(m_device, frame.commandPool, nullptr);
        if (frame.imageAvailable) vkDestroySemaphore(m_device, frame.imageAvailable, nullptr);
        if (frame.renderFinished) vkDestroySemaphore(m_device, frame.renderFinished, nullptr);
        if (frame.inFlightFence)  vkDestroyFence(m_device, frame.inFlightFence, nullptr);
    }

    // Destroy resources
    for (auto& buf : m_buffers) {
        if (buf.alive) {
            if (buf.buffer) vkDestroyBuffer(m_device, buf.buffer, nullptr);
            if (buf.memory) vkFreeMemory(m_device, buf.memory, nullptr);
        }
    }
    for (auto& tex : m_textures) {
        if (tex.alive && !tex.isSwapchainImage) {
            if (tex.view)   vkDestroyImageView(m_device, tex.view, nullptr);
            if (tex.image)  vkDestroyImage(m_device, tex.image, nullptr);
            if (tex.memory) vkFreeMemory(m_device, tex.memory, nullptr);
        }
    }
    for (auto& s : m_samplers) {
        if (s.alive && s.sampler) vkDestroySampler(m_device, s.sampler, nullptr);
    }
    for (auto& s : m_shaders) {
        if (s.alive && s.module) vkDestroyShaderModule(m_device, s.module, nullptr);
    }
    for (auto& p : m_pipelines) {
        if (p.alive) {
            if (p.pipeline) vkDestroyPipeline(m_device, p.pipeline, nullptr);
            if (p.layout)   vkDestroyPipelineLayout(m_device, p.layout, nullptr);
        }
    }

    if (m_descriptorPool) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    if (m_bindlessLayout) vkDestroyDescriptorSetLayout(m_device, m_bindlessLayout, nullptr);
    if (m_globalPipelineLayout) vkDestroyPipelineLayout(m_device, m_globalPipelineLayout, nullptr);

    CleanupSwapchain();

    if (m_device)  vkDestroyDevice(m_device, nullptr);
    if (m_surface) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);

#if defined(NGE_DEBUG) || defined(NGE_PROFILE)
    if (m_debugMessenger) {
        auto destroyFunc = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyFunc) destroyFunc(m_instance, m_debugMessenger, nullptr);
    }
#endif

    if (m_instance) vkDestroyInstance(m_instance, nullptr);

    m_initialized = false;
    NGE_LOG_INFO("Vulkan RHI shut down");
}

void VulkanDevice::WaitIdle() {
    if (m_device) vkDeviceWaitIdle(m_device);
}

// ─── Instance Creation ───────────────────────────────────────────────────

bool VulkanDevice::CreateInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName   = "NextGen Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName        = "NextGen";
    appInfo.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion         = VK_API_VERSION_1_3;

    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#if defined(NGE_PLATFORM_WINDOWS)
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
    };

    std::vector<const char*> layers;

#if defined(NGE_DEBUG) || defined(NGE_PROFILE)
    layers.push_back("VK_LAYER_KHRONOS_validation");
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

    VkInstanceCreateInfo createInfo{};
    createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo        = &appInfo;
    createInfo.enabledExtensionCount   = static_cast<u32>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount       = static_cast<u32>(layers.size());
    createInfo.ppEnabledLayerNames     = layers.data();

    if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
        NGE_LOG_ERROR("Failed to create Vulkan instance");
        return false;
    }

#if defined(NGE_DEBUG) || defined(NGE_PROFILE)
    // Setup debug messenger
    auto createFunc = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
    if (createFunc) {
        VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
        debugInfo.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugInfo.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugInfo.pfnUserCallback = DebugCallback;
        createFunc(m_instance, &debugInfo, nullptr, &m_debugMessenger);
    }
#endif

    return true;
}

// ─── Physical Device Selection ───────────────────────────────────────────

bool VulkanDevice::SelectPhysicalDevice() {
    u32 deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        NGE_LOG_ERROR("No Vulkan-capable GPUs found");
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // Prefer discrete GPU
    VkPhysicalDevice best = VK_NULL_HANDLE;
    u32 bestScore = 0;

    for (auto& dev : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);

        u32 score = 0;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 10000;
        score += props.limits.maxImageDimension2D;

        if (score > bestScore) {
            bestScore = score;
            best = dev;
        }
    }

    m_physicalDevice = best;
    NGE_ASSERT(m_physicalDevice != VK_NULL_HANDLE);

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    m_deviceName = props.deviceName;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memProperties);

    NGE_LOG_INFO("Selected GPU: {}", m_deviceName);
    return true;
}

// ─── Logical Device + Queues ─────────────────────────────────────────────

bool VulkanDevice::CreateLogicalDevice() {
    // Find queue families
    u32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

    m_graphicsQueueFamily = UINT32_MAX;
    m_computeQueueFamily  = UINT32_MAX;
    m_transferQueueFamily = UINT32_MAX;

    for (u32 i = 0; i < queueFamilyCount; ++i) {
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &presentSupport);

        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport) {
            m_graphicsQueueFamily = i;
        }
        if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            m_computeQueueFamily = i;
        }
        if ((queueFamilies[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            !(queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
            m_transferQueueFamily = i;
        }
    }

    if (m_graphicsQueueFamily == UINT32_MAX) {
        NGE_LOG_ERROR("No graphics queue family found");
        return false;
    }
    // Fallback: use graphics family for compute/transfer if no dedicated ones found
    if (m_computeQueueFamily == UINT32_MAX)  m_computeQueueFamily = m_graphicsQueueFamily;
    if (m_transferQueueFamily == UINT32_MAX) m_transferQueueFamily = m_graphicsQueueFamily;

    // Deduplicate queue families for device creation
    std::vector<u32> uniqueFamilies = { m_graphicsQueueFamily };
    if (m_computeQueueFamily != m_graphicsQueueFamily)
        uniqueFamilies.push_back(m_computeQueueFamily);
    if (m_transferQueueFamily != m_graphicsQueueFamily && m_transferQueueFamily != m_computeQueueFamily)
        uniqueFamilies.push_back(m_transferQueueFamily);

    f32 priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (u32 family : uniqueFamilies) {
        VkDeviceQueueCreateInfo qi{};
        qi.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = family;
        qi.queueCount       = 1;
        qi.pQueuePriorities = &priority;
        queueInfos.push_back(qi);
    }

    // Device extensions
    std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
    };

    // Check for optional extensions
    u32 extCount = 0;
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> availableExts(extCount);
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, availableExts.data());

    auto hasExtension = [&](const char* name) {
        for (auto& ext : availableExts) {
            if (std::string(ext.extensionName) == name) return true;
        }
        return false;
    };

    bool hasMeshShaders = hasExtension(VK_EXT_MESH_SHADER_EXTENSION_NAME);
    bool hasRayTracing  = hasExtension(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
                          hasExtension(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
                          hasExtension(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    bool hasDescriptorIndexing = hasExtension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);

    if (hasMeshShaders) deviceExtensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
    if (hasDescriptorIndexing) deviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    if (hasRayTracing) {
        deviceExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        deviceExtensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
    }

    // ─── Feature chain ────────────────────────────────────────────────
    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.features.samplerAnisotropy    = VK_TRUE;
    features2.features.fillModeNonSolid     = VK_TRUE;
    features2.features.multiDrawIndirect    = VK_TRUE;
    features2.features.drawIndirectFirstInstance = VK_TRUE;
    features2.features.fragmentStoresAndAtomics  = VK_TRUE;
    features2.features.shaderInt64          = VK_TRUE;

    VkPhysicalDeviceVulkan12Features vk12Features{};
    vk12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    vk12Features.descriptorIndexing                    = VK_TRUE;
    vk12Features.runtimeDescriptorArray                = VK_TRUE;
    vk12Features.descriptorBindingPartiallyBound       = VK_TRUE;
    vk12Features.descriptorBindingVariableDescriptorCount = VK_TRUE;
    vk12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    vk12Features.shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
    vk12Features.bufferDeviceAddress                   = VK_TRUE;
    vk12Features.timelineSemaphore                     = VK_TRUE;
    vk12Features.scalarBlockLayout                     = VK_TRUE;
    features2.pNext = &vk12Features;

    VkPhysicalDeviceVulkan13Features vk13Features{};
    vk13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    vk13Features.dynamicRendering  = VK_TRUE;
    vk13Features.synchronization2  = VK_TRUE;
    vk13Features.maintenance4      = VK_TRUE;
    vk12Features.pNext = &vk13Features;

    void** pNextTail = &vk13Features.pNext;

    VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures{};
    if (hasMeshShaders) {
        meshFeatures.sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
        meshFeatures.meshShader = VK_TRUE;
        meshFeatures.taskShader = VK_TRUE;
        *pNextTail = &meshFeatures;
        pNextTail = &meshFeatures.pNext;
    }

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rtPipelineFeatures{};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelFeatures{};
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{};
    if (hasRayTracing) {
        rtPipelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
        rtPipelineFeatures.rayTracingPipeline = VK_TRUE;
        *pNextTail = &rtPipelineFeatures;
        pNextTail = &rtPipelineFeatures.pNext;

        accelFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        accelFeatures.accelerationStructure = VK_TRUE;
        *pNextTail = &accelFeatures;
        pNextTail = &accelFeatures.pNext;

        rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        rayQueryFeatures.rayQuery = VK_TRUE;
        *pNextTail = &rayQueryFeatures;
        pNextTail = &rayQueryFeatures.pNext;
    }

    *pNextTail = nullptr;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext                   = &features2;
    deviceInfo.queueCreateInfoCount    = static_cast<u32>(queueInfos.size());
    deviceInfo.pQueueCreateInfos       = queueInfos.data();
    deviceInfo.enabledExtensionCount   = static_cast<u32>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (vkCreateDevice(m_physicalDevice, &deviceInfo, nullptr, &m_device) != VK_SUCCESS) {
        NGE_LOG_ERROR("Failed to create Vulkan logical device");
        return false;
    }

    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, m_computeQueueFamily, 0, &m_computeQueue);
    vkGetDeviceQueue(m_device, m_transferQueueFamily, 0, &m_transferQueue);

    // Store capability flags
    m_capabilities.meshShaders        = hasMeshShaders;
    m_capabilities.rayTracing         = hasRayTracing;
    m_capabilities.descriptorIndexing = hasDescriptorIndexing;
    m_capabilities.dynamicRendering   = true; // Required by Vulkan 1.3

    return true;
}

// ─── Swapchain ───────────────────────────────────────────────────────────

bool VulkanDevice::CreateSwapchain(u32 width, u32 height) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &caps);

    // Choose format
    u32 formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());

    m_swapchainFormat = VK_FORMAT_B8G8R8A8_SRGB;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    for (auto& fmt : formats) {
        if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            m_swapchainFormat = fmt.format;
            colorSpace = fmt.colorSpace;
            break;
        }
    }

    // Choose extent
    if (caps.currentExtent.width != UINT32_MAX) {
        m_swapchainExtent = caps.currentExtent;
    } else {
        m_swapchainExtent.width  = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
        m_swapchainExtent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    u32 imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR swapInfo{};
    swapInfo.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapInfo.surface          = m_surface;
    swapInfo.minImageCount    = imageCount;
    swapInfo.imageFormat      = m_swapchainFormat;
    swapInfo.imageColorSpace  = colorSpace;
    swapInfo.imageExtent      = m_swapchainExtent;
    swapInfo.imageArrayLayers = 1;
    swapInfo.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapInfo.preTransform     = caps.currentTransform;
    swapInfo.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapInfo.presentMode      = VK_PRESENT_MODE_MAILBOX_KHR; // Triple buffering
    swapInfo.clipped          = VK_TRUE;
    swapInfo.oldSwapchain     = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(m_device, &swapInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        // Fallback to FIFO (always available)
        swapInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        if (vkCreateSwapchainKHR(m_device, &swapInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
            NGE_LOG_ERROR("Failed to create swapchain");
            return false;
        }
    }

    // Get swapchain images
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);
    m_swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, m_swapchainImages.data());

    // Create image views and register as textures
    m_swapchainViews.resize(imageCount);
    m_swapchainTextureHandles.resize(imageCount);

    for (u32 i = 0; i < imageCount; ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image                           = m_swapchainImages[i];
        viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format                          = m_swapchainFormat;
        viewInfo.components                      = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                                    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;

        vkCreateImageView(m_device, &viewInfo, nullptr, &m_swapchainViews[i]);

        // Register as texture handle
        u32 texIdx = i; // Reserve first N texture slots for swapchain
        m_textures[texIdx].image  = m_swapchainImages[i];
        m_textures[texIdx].view   = m_swapchainViews[i];
        m_textures[texIdx].width  = m_swapchainExtent.width;
        m_textures[texIdx].height = m_swapchainExtent.height;
        m_textures[texIdx].format = Format::BGRA8_SRGB;
        m_textures[texIdx].isSwapchainImage = true;
        m_textures[texIdx].alive = true;
        m_swapchainTextureHandles[i] = TextureHandle{texIdx};
    }

    NGE_LOG_INFO("Swapchain created: {}x{}, {} images", m_swapchainExtent.width, m_swapchainExtent.height, imageCount);
    return true;
}

void VulkanDevice::CleanupSwapchain() {
    for (auto view : m_swapchainViews) {
        if (view) vkDestroyImageView(m_device, view, nullptr);
    }
    m_swapchainViews.clear();
    m_swapchainImages.clear();

    for (auto& h : m_swapchainTextureHandles) {
        if (h.IsValid()) m_textures[h.index].alive = false;
    }
    m_swapchainTextureHandles.clear();

    if (m_swapchain) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

void VulkanDevice::ResizeSwapchain(u32 width, u32 height) {
    WaitIdle();
    CleanupSwapchain();
    CreateSwapchain(width, height);
}

// ─── Sync Objects ────────────────────────────────────────────────────────

bool VulkanDevice::CreateSyncObjects() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_graphicsQueueFamily;
    poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        auto& frame = m_frames[i];

        if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &frame.commandPool) != VK_SUCCESS)
            return false;

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = frame.commandPool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(m_device, &allocInfo, &frame.commandBuffer);

        vkCreateSemaphore(m_device, &semInfo, nullptr, &frame.imageAvailable);
        vkCreateSemaphore(m_device, &semInfo, nullptr, &frame.renderFinished);
        vkCreateFence(m_device, &fenceInfo, nullptr, &frame.inFlightFence);
    }

    return true;
}

// ─── Descriptor Pool (Bindless) ──────────────────────────────────────────

bool VulkanDevice::CreateDescriptorPool() {
    // Bindless descriptor set layout: one large array of each resource type
    VkDescriptorSetLayoutBinding bindings[3] = {};

    // Binding 0: sampled images (textures)
    bindings[0].binding         = 0;
    bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[0].descriptorCount = MAX_TEXTURES;
    bindings[0].stageFlags      = VK_SHADER_STAGE_ALL;

    // Binding 1: storage buffers
    bindings[1].binding         = 1;
    bindings[1].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = MAX_BUFFERS;
    bindings[1].stageFlags      = VK_SHADER_STAGE_ALL;

    // Binding 2: samplers
    bindings[2].binding         = 2;
    bindings[2].descriptorType  = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[2].descriptorCount = MAX_SAMPLERS;
    bindings[2].stageFlags      = VK_SHADER_STAGE_ALL;

    VkDescriptorBindingFlags bindingFlags[3] = {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
    flagsInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flagsInfo.bindingCount  = 3;
    flagsInfo.pBindingFlags = bindingFlags;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext        = &flagsInfo;
    layoutInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings    = bindings;

    if (vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_bindlessLayout) != VK_SUCCESS) {
        NGE_LOG_ERROR("Failed to create bindless descriptor set layout");
        return false;
    }

    // Pool
    VkDescriptorPoolSize poolSizes[3] = {
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,  MAX_TEXTURES },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_BUFFERS },
        { VK_DESCRIPTOR_TYPE_SAMPLER,        MAX_SAMPLERS },
    };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags         = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.maxSets       = 1;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes    = poolSizes;

    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS)
        return false;

    // Allocate the one global bindless set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &m_bindlessLayout;

    if (vkAllocateDescriptorSets(m_device, &allocInfo, &m_bindlessSet) != VK_SUCCESS)
        return false;

    // Global pipeline layout: push constants (128 bytes) + bindless set
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_ALL;
    pushRange.offset     = 0;
    pushRange.size       = 128;

    VkPipelineLayoutCreateInfo pipeLayoutInfo{};
    pipeLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeLayoutInfo.setLayoutCount         = 1;
    pipeLayoutInfo.pSetLayouts            = &m_bindlessLayout;
    pipeLayoutInfo.pushConstantRangeCount = 1;
    pipeLayoutInfo.pPushConstantRanges    = &pushRange;

    if (vkCreatePipelineLayout(m_device, &pipeLayoutInfo, nullptr, &m_globalPipelineLayout) != VK_SUCCESS)
        return false;

    NGE_LOG_INFO("Bindless descriptor pool created");
    return true;
}

// ─── Capability Probing ──────────────────────────────────────────────────

void VulkanDevice::ProbeCapabilities() {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);

    m_capabilities.maxBindlessTextures    = MAX_TEXTURES;
    m_capabilities.maxBindlessBuffers     = MAX_BUFFERS;
    m_capabilities.maxBufferSize          = props.limits.maxStorageBufferRange;
    m_capabilities.maxTextureDimension2D  = props.limits.maxImageDimension2D;
    m_capabilities.maxComputeWorkGroupSize[0] = props.limits.maxComputeWorkGroupSize[0];
    m_capabilities.maxComputeWorkGroupSize[1] = props.limits.maxComputeWorkGroupSize[1];
    m_capabilities.maxComputeWorkGroupSize[2] = props.limits.maxComputeWorkGroupSize[2];

    // Determine feature tier
    m_featureTier = FeatureTier::Tier0_Baseline;
    if (m_capabilities.meshShaders && m_capabilities.descriptorIndexing)
        m_featureTier = FeatureTier::Tier1_GPUDriven;
    if (m_capabilities.rayTracing)
        m_featureTier = FeatureTier::Tier2_RayTracing;

    NGE_LOG_INFO("Feature tier: {} | Mesh shaders: {} | Ray tracing: {} | Bindless: {}",
                 static_cast<int>(m_featureTier),
                 m_capabilities.meshShaders,
                 m_capabilities.rayTracing,
                 m_capabilities.descriptorIndexing);
}

// ─── Frame Management ────────────────────────────────────────────────────

void VulkanDevice::BeginFrame() {
    auto& frame = m_frames[m_frameIndex];
    vkWaitForFences(m_device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);
    vkResetFences(m_device, 1, &frame.inFlightFence);
    vkResetCommandPool(m_device, frame.commandPool, 0);
}

bool VulkanDevice::AcquireNextImage() {
    auto& frame = m_frames[m_frameIndex];
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, UINT64_MAX,
                                             frame.imageAvailable, VK_NULL_HANDLE, &m_currentImageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) return false;
    return result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR;
}

void VulkanDevice::Present() {
    auto& frame = m_frames[m_frameIndex];

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores    = &frame.renderFinished;
    presentInfo.swapchainCount     = 1;
    presentInfo.pSwapchains        = &m_swapchain;
    presentInfo.pImageIndices      = &m_currentImageIndex;

    vkQueuePresentKHR(m_graphicsQueue, &presentInfo);
}

void VulkanDevice::EndFrame() {
    m_frameIndex = (m_frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}

TextureHandle VulkanDevice::GetSwapchainTexture() {
    return m_swapchainTextureHandles[m_currentImageIndex];
}

Format VulkanDevice::GetSwapchainFormat() { return Format::BGRA8_SRGB; }
u32 VulkanDevice::GetSwapchainWidth() { return m_swapchainExtent.width; }
u32 VulkanDevice::GetSwapchainHeight() { return m_swapchainExtent.height; }

// ─── Resource Creation ───────────────────────────────────────────────────

BufferHandle VulkanDevice::CreateBuffer(const BufferDesc& desc) {
    // Find free slot
    u32 idx = UINT32_MAX;
    for (u32 i = 0; i < MAX_BUFFERS; ++i) {
        if (!m_buffers[i].alive) { idx = i; break; }
    }
    if (idx == UINT32_MAX) return BufferHandle{};

    VkBufferUsageFlags vkUsage = 0;
    if (desc.usage & BufferUsage::Vertex)    vkUsage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (desc.usage & BufferUsage::Index)     vkUsage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (desc.usage & BufferUsage::Uniform)   vkUsage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (desc.usage & BufferUsage::Storage)   vkUsage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (desc.usage & BufferUsage::Indirect)  vkUsage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (desc.usage & BufferUsage::TransferSrc) vkUsage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (desc.usage & BufferUsage::TransferDst) vkUsage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    vkUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size        = desc.size;
    bufInfo.usage       = vkUsage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkBuffer buffer;
    if (vkCreateBuffer(m_device, &bufInfo, nullptr, &buffer) != VK_SUCCESS) return BufferHandle{};

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(m_device, buffer, &memReqs);

    VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (desc.memoryUsage == MemoryUsage::CPU_To_GPU)
        memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    else if (desc.memoryUsage == MemoryUsage::GPU_To_CPU)
        memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

    VkDeviceMemory memory = AllocateMemory(memReqs, memProps);
    if (!memory) {
        vkDestroyBuffer(m_device, buffer, nullptr);
        return BufferHandle{};
    }

    vkBindBufferMemory(m_device, buffer, memory, 0);

    auto& buf = m_buffers[idx];
    buf.buffer   = buffer;
    buf.memory   = memory;
    buf.size     = desc.size;
    buf.usage    = desc.usage;
    buf.memUsage = desc.memoryUsage;
    buf.alive    = true;

    // Auto-map CPU-visible buffers
    if (desc.memoryUsage == MemoryUsage::CPU_To_GPU) {
        vkMapMemory(m_device, memory, 0, desc.size, 0, &buf.mapped);
    }

    return BufferHandle{idx};
}

void VulkanDevice::DestroyBuffer(BufferHandle handle) {
    if (!handle.IsValid()) return;
    auto& buf = m_buffers[handle.index];
    if (!buf.alive) return;
    if (buf.mapped) { vkUnmapMemory(m_device, buf.memory); buf.mapped = nullptr; }
    vkDestroyBuffer(m_device, buf.buffer, nullptr);
    vkFreeMemory(m_device, buf.memory, nullptr);
    buf.alive = false;
}

void* VulkanDevice::MapBuffer(BufferHandle handle) {
    auto& buf = m_buffers[handle.index];
    if (buf.mapped) return buf.mapped;
    vkMapMemory(m_device, buf.memory, 0, buf.size, 0, &buf.mapped);
    return buf.mapped;
}

void VulkanDevice::UnmapBuffer(BufferHandle handle) {
    auto& buf = m_buffers[handle.index];
    if (buf.mapped) {
        vkUnmapMemory(m_device, buf.memory);
        buf.mapped = nullptr;
    }
}

void VulkanDevice::UpdateBuffer(BufferHandle handle, const void* data, usize size, usize offset) {
    auto& buf = m_buffers[handle.index];
    if (buf.mapped) {
        std::memcpy(static_cast<byte*>(buf.mapped) + offset, data, size);
    }
}

TextureHandle VulkanDevice::CreateTexture(const TextureDesc& desc) {
    u32 idx = UINT32_MAX;
    // Start after swapchain images
    u32 startIdx = static_cast<u32>(m_swapchainImages.size());
    for (u32 i = startIdx; i < MAX_TEXTURES; ++i) {
        if (!m_textures[i].alive) { idx = i; break; }
    }
    if (idx == UINT32_MAX) return TextureHandle{};

    VkImageCreateInfo imgInfo{};
    imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgInfo.format        = ToVkFormat(desc.format);
    imgInfo.extent        = { desc.width, desc.height, desc.depth };
    imgInfo.mipLevels     = desc.mipLevels;
    imgInfo.arrayLayers   = desc.arrayLayers;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageUsageFlags usage = 0;
    if (desc.usage & TextureUsage::ShaderRead)      usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (desc.usage & TextureUsage::ShaderWrite)      usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (desc.usage & TextureUsage::RenderTarget)     usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (desc.usage & TextureUsage::DepthStencil)     usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (desc.usage & TextureUsage::TransferSrc)      usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (desc.usage & TextureUsage::TransferDst)      usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imgInfo.usage = usage;

    if (desc.type == TextureType::TexCube || desc.type == TextureType::TexCubeArray)
        imgInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    VkImage image;
    if (vkCreateImage(m_device, &imgInfo, nullptr, &image) != VK_SUCCESS) return TextureHandle{};

    VkMemoryRequirements memReqs;
    vkGetImageMemoryRequirements(m_device, image, &memReqs);
    VkDeviceMemory memory = AllocateMemory(memReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkBindImageMemory(m_device, image, memory, 0);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image    = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format   = ToVkFormat(desc.format);
    viewInfo.subresourceRange.aspectMask = (desc.usage & TextureUsage::DepthStencil)
        ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel   = 0;
    viewInfo.subresourceRange.levelCount     = desc.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount     = desc.arrayLayers;

    VkImageView view;
    vkCreateImageView(m_device, &viewInfo, nullptr, &view);

    auto& tex = m_textures[idx];
    tex.image  = image;
    tex.view   = view;
    tex.memory = memory;
    tex.format = desc.format;
    tex.width  = desc.width;
    tex.height = desc.height;
    tex.depth  = desc.depth;
    tex.mipLevels   = desc.mipLevels;
    tex.arrayLayers = desc.arrayLayers;
    tex.type   = desc.type;
    tex.alive  = true;

    return TextureHandle{idx};
}

void VulkanDevice::DestroyTexture(TextureHandle handle) {
    if (!handle.IsValid()) return;
    auto& tex = m_textures[handle.index];
    if (!tex.alive || tex.isSwapchainImage) return;
    vkDestroyImageView(m_device, tex.view, nullptr);
    vkDestroyImage(m_device, tex.image, nullptr);
    vkFreeMemory(m_device, tex.memory, nullptr);
    tex.alive = false;
}

SamplerHandle VulkanDevice::CreateSampler(const SamplerDesc& desc) {
    u32 idx = UINT32_MAX;
    for (u32 i = 0; i < MAX_SAMPLERS; ++i) {
        if (!m_samplers[i].alive) { idx = i; break; }
    }
    if (idx == UINT32_MAX) return SamplerHandle{};

    auto toVkFilter = [](FilterMode f) -> VkFilter {
        return f == FilterMode::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
    };
    auto toVkAddress = [](AddressMode a) -> VkSamplerAddressMode {
        switch (a) {
            case AddressMode::Repeat:         return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            case AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
            case AddressMode::ClampToEdge:    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            case AddressMode::ClampToBorder:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
        }
    };

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType            = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter        = toVkFilter(desc.magFilter);
    samplerInfo.minFilter        = toVkFilter(desc.minFilter);
    samplerInfo.mipmapMode       = desc.mipFilter == FilterMode::Nearest
                                     ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU     = toVkAddress(desc.addressU);
    samplerInfo.addressModeV     = toVkAddress(desc.addressV);
    samplerInfo.addressModeW     = toVkAddress(desc.addressW);
    samplerInfo.mipLodBias       = desc.mipLodBias;
    samplerInfo.anisotropyEnable = desc.enableAnisotropy ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy    = desc.maxAnisotropy;
    samplerInfo.maxLod           = VK_LOD_CLAMP_NONE;

    VkSampler sampler;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
        return SamplerHandle{};

    m_samplers[idx].sampler = sampler;
    m_samplers[idx].alive   = true;
    return SamplerHandle{idx};
}

void VulkanDevice::DestroySampler(SamplerHandle handle) {
    if (!handle.IsValid()) return;
    auto& s = m_samplers[handle.index];
    if (!s.alive) return;
    vkDestroySampler(m_device, s.sampler, nullptr);
    s.alive = false;
}

ShaderHandle VulkanDevice::CreateShader(const ShaderDesc& desc) {
    u32 idx = UINT32_MAX;
    for (u32 i = 0; i < MAX_SHADERS; ++i) {
        if (!m_shaders[i].alive) { idx = i; break; }
    }
    if (idx == UINT32_MAX) return ShaderHandle{};

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = desc.bytecodeSize;
    moduleInfo.pCode    = reinterpret_cast<const u32*>(desc.bytecode);

    VkShaderModule module;
    if (vkCreateShaderModule(m_device, &moduleInfo, nullptr, &module) != VK_SUCCESS)
        return ShaderHandle{};

    m_shaders[idx].module = module;
    m_shaders[idx].stage  = desc.stage;
    m_shaders[idx].alive  = true;
    return ShaderHandle{idx};
}

void VulkanDevice::DestroyShader(ShaderHandle handle) {
    if (!handle.IsValid()) return;
    auto& s = m_shaders[handle.index];
    if (!s.alive) return;
    vkDestroyShaderModule(m_device, s.module, nullptr);
    s.alive = false;
}

PipelineHandle VulkanDevice::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    u32 idx = UINT32_MAX;
    for (u32 i = 0; i < MAX_PIPELINES; ++i) {
        if (!m_pipelines[i].alive) { idx = i; break; }
    }
    if (idx == UINT32_MAX) return PipelineHandle{};

    // Shader stages
    std::vector<VkPipelineShaderStageCreateInfo> stages;

    auto addStage = [&](ShaderHandle handle, VkShaderStageFlagBits vkStage) {
        if (!handle.IsValid()) return;
        VkPipelineShaderStageCreateInfo si{};
        si.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        si.stage  = vkStage;
        si.module = m_shaders[handle.index].module;
        si.pName  = "main";
        stages.push_back(si);
    };

    if (desc.isMeshShaderPipeline) {
        addStage(desc.amplificationShader, VK_SHADER_STAGE_TASK_BIT_EXT);
        addStage(desc.meshShader, VK_SHADER_STAGE_MESH_BIT_EXT);
    } else {
        addStage(desc.vertexShader, VK_SHADER_STAGE_VERTEX_BIT);
    }
    addStage(desc.fragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT);

    // Dynamic rendering format info (Vulkan 1.3, no render pass objects)
    std::vector<VkFormat> colorFormats;
    for (u32 i = 0; i < desc.renderTargetCount; ++i) {
        colorFormats.push_back(ToVkFormat(desc.renderTargets[i].format));
    }

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount    = static_cast<u32>(colorFormats.size());
    renderingInfo.pColorAttachmentFormats = colorFormats.data();
    if (desc.hasDepthStencil) {
        renderingInfo.depthAttachmentFormat = ToVkFormat(desc.depthStencil.format);
    }

    // Vertex input (not needed for mesh shader pipelines)
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    std::vector<VkVertexInputBindingDescription> vkBindings;
    std::vector<VkVertexInputAttributeDescription> vkAttributes;

    if (!desc.isMeshShaderPipeline) {
        for (u32 i = 0; i < desc.vertexBindingCount; ++i) {
            VkVertexInputBindingDescription bd{};
            bd.binding   = desc.vertexBindings[i].binding;
            bd.stride    = desc.vertexBindings[i].stride;
            bd.inputRate = desc.vertexBindings[i].perInstance
                             ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
            vkBindings.push_back(bd);
        }
        for (u32 i = 0; i < desc.vertexAttributeCount; ++i) {
            VkVertexInputAttributeDescription ad{};
            ad.location = desc.vertexAttributes[i].location;
            ad.binding  = desc.vertexAttributes[i].binding;
            ad.format   = ToVkFormat(desc.vertexAttributes[i].format);
            ad.offset   = desc.vertexAttributes[i].offset;
            vkAttributes.push_back(ad);
        }
        vertexInput.vertexBindingDescriptionCount   = static_cast<u32>(vkBindings.size());
        vertexInput.pVertexBindingDescriptions      = vkBindings.data();
        vertexInput.vertexAttributeDescriptionCount = static_cast<u32>(vkAttributes.size());
        vertexInput.pVertexAttributeDescriptions    = vkAttributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Dynamic state: viewport + scissor
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates    = dynamicStates;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = desc.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth   = 1.0f;
    switch (desc.cullMode) {
        case CullMode::None:         rasterizer.cullMode = VK_CULL_MODE_NONE; break;
        case CullMode::Front:        rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT; break;
        case CullMode::Back:         rasterizer.cullMode = VK_CULL_MODE_BACK_BIT; break;
        case CullMode::FrontAndBack: rasterizer.cullMode = VK_CULL_MODE_FRONT_AND_BACK; break;
    }
    rasterizer.frontFace = desc.frontFace == FrontFace::CounterClockwise
                             ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencilState{};
    depthStencilState.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencilState.depthTestEnable  = desc.depthStencil.depthTest ? VK_TRUE : VK_FALSE;
    depthStencilState.depthWriteEnable = desc.depthStencil.depthWrite ? VK_TRUE : VK_FALSE;
    switch (desc.depthStencil.depthCompare) {
        case CompareOp::Less:         depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS; break;
        case CompareOp::LessEqual:    depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL; break;
        case CompareOp::Greater:      depthStencilState.depthCompareOp = VK_COMPARE_OP_GREATER; break;
        case CompareOp::GreaterEqual: depthStencilState.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL; break;
        default:                      depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS; break;
    }

    // Blend state
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(desc.renderTargetCount);
    for (u32 i = 0; i < desc.renderTargetCount; ++i) {
        blendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (i < desc.blendAttachmentCount && desc.blendAttachments[i].enable) {
            blendAttachments[i].blendEnable = VK_TRUE;
            // Simplified: premultiplied alpha
            blendAttachments[i].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blendAttachments[i].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachments[i].colorBlendOp        = VK_BLEND_OP_ADD;
            blendAttachments[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachments[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            blendAttachments[i].alphaBlendOp        = VK_BLEND_OP_ADD;
        }
    }

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = static_cast<u32>(blendAttachments.size());
    colorBlend.pAttachments    = blendAttachments.data();

    // Create pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext               = &renderingInfo;
    pipelineInfo.stageCount          = static_cast<u32>(stages.size());
    pipelineInfo.pStages             = stages.data();
    pipelineInfo.pVertexInputState   = desc.isMeshShaderPipeline ? nullptr : &vertexInput;
    pipelineInfo.pInputAssemblyState = desc.isMeshShaderPipeline ? nullptr : &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pDepthStencilState  = &depthStencilState;
    pipelineInfo.pColorBlendState    = &colorBlend;
    pipelineInfo.pDynamicState       = &dynamicState;
    pipelineInfo.layout              = m_globalPipelineLayout;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        return PipelineHandle{};
    }

    m_pipelines[idx].pipeline  = pipeline;
    m_pipelines[idx].layout    = m_globalPipelineLayout;
    m_pipelines[idx].bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    m_pipelines[idx].alive     = true;

    return PipelineHandle{idx};
}

PipelineHandle VulkanDevice::CreateComputePipeline(const ComputePipelineDesc& desc) {
    u32 idx = UINT32_MAX;
    for (u32 i = 0; i < MAX_PIPELINES; ++i) {
        if (!m_pipelines[i].alive) { idx = i; break; }
    }
    if (idx == UINT32_MAX) return PipelineHandle{};

    VkComputePipelineCreateInfo pipeInfo{};
    pipeInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeInfo.layout = m_globalPipelineLayout;
    pipeInfo.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeInfo.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeInfo.stage.module = m_shaders[desc.computeShader.index].module;
    pipeInfo.stage.pName  = "main";

    VkPipeline pipeline;
    if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &pipeline) != VK_SUCCESS)
        return PipelineHandle{};

    m_pipelines[idx].pipeline  = pipeline;
    m_pipelines[idx].layout    = m_globalPipelineLayout;
    m_pipelines[idx].bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    m_pipelines[idx].alive     = true;
    return PipelineHandle{idx};
}

PipelineHandle VulkanDevice::CreateRayTracingPipeline(const RayTracingPipelineDesc& /*desc*/) {
    // TODO: Implement ray tracing pipeline creation
    NGE_LOG_WARN("Ray tracing pipeline creation not yet implemented");
    return PipelineHandle{};
}

void VulkanDevice::DestroyPipeline(PipelineHandle handle) {
    if (!handle.IsValid()) return;
    auto& p = m_pipelines[handle.index];
    if (!p.alive) return;
    vkDestroyPipeline(m_device, p.pipeline, nullptr);
    // Don't destroy layout — it's the global shared layout
    p.alive = false;
}

AccelStructHandle VulkanDevice::CreateAccelerationStructure(const AccelStructDesc& /*desc*/) {
    NGE_LOG_WARN("Acceleration structure creation not yet implemented");
    return AccelStructHandle{};
}

void VulkanDevice::DestroyAccelerationStructure(AccelStructHandle /*handle*/) {}

u32 VulkanDevice::GetBindlessTextureIndex(TextureHandle handle) {
    if (!handle.IsValid()) return UINT32_MAX;
    return m_textures[handle.index].bindlessIndex;
}

u32 VulkanDevice::GetBindlessBufferIndex(BufferHandle handle) {
    if (!handle.IsValid()) return UINT32_MAX;
    return m_buffers[handle.index].bindlessIndex;
}

DescriptorSetHandle VulkanDevice::GetGlobalDescriptorSet() {
    return DescriptorSetHandle{0};
}

ICommandList* VulkanDevice::GetCommandList(QueueType /*queue*/) {
    m_commandList.SetCommandBuffer(m_frames[m_frameIndex].commandBuffer);
    return &m_commandList;
}

void VulkanDevice::SubmitCommandList(ICommandList* /*cmdList*/, QueueType /*queue*/) {
    auto& frame = m_frames[m_frameIndex];

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = &frame.imageAvailable;
    submitInfo.pWaitDstStageMask    = &waitStage;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &frame.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = &frame.renderFinished;

    vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, frame.inFlightFence);
}

// ─── Utility ─────────────────────────────────────────────────────────────

u32 VulkanDevice::FindMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties) const {
    for (u32 i = 0; i < m_memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) &&
            (m_memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    NGE_ASSERT_MSG(false, "Failed to find suitable memory type");
    return 0;
}

VkDeviceMemory VulkanDevice::AllocateMemory(VkMemoryRequirements reqs, VkMemoryPropertyFlags props) {
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize  = reqs.size;
    allocInfo.memoryTypeIndex = FindMemoryType(reqs.memoryTypeBits, props);

    VkMemoryAllocateFlagsInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    flagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    allocInfo.pNext = &flagsInfo;

    VkDeviceMemory memory;
    if (vkAllocateMemory(m_device, &allocInfo, nullptr, &memory) != VK_SUCCESS) return VK_NULL_HANDLE;
    return memory;
}

VkFormat VulkanDevice::ToVkFormat(Format fmt) const {
    switch (fmt) {
        case Format::RGBA8_UNORM:  return VK_FORMAT_R8G8B8A8_UNORM;
        case Format::RGBA8_SRGB:   return VK_FORMAT_R8G8B8A8_SRGB;
        case Format::BGRA8_UNORM:  return VK_FORMAT_B8G8R8A8_UNORM;
        case Format::BGRA8_SRGB:   return VK_FORMAT_B8G8R8A8_SRGB;
        case Format::R8_UNORM:     return VK_FORMAT_R8_UNORM;
        case Format::RG8_UNORM:    return VK_FORMAT_R8G8_UNORM;
        case Format::R16_FLOAT:    return VK_FORMAT_R16_SFLOAT;
        case Format::RG16_FLOAT:   return VK_FORMAT_R16G16_SFLOAT;
        case Format::RGBA16_FLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case Format::R32_FLOAT:    return VK_FORMAT_R32_SFLOAT;
        case Format::RG32_FLOAT:   return VK_FORMAT_R32G32_SFLOAT;
        case Format::RGB32_FLOAT:  return VK_FORMAT_R32G32B32_SFLOAT;
        case Format::RGBA32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::R32_UINT:     return VK_FORMAT_R32_UINT;
        case Format::RG32_UINT:    return VK_FORMAT_R32G32_UINT;
        case Format::RGBA32_UINT:  return VK_FORMAT_R32G32B32A32_UINT;
        case Format::D16_UNORM:    return VK_FORMAT_D16_UNORM;
        case Format::D32_FLOAT:    return VK_FORMAT_D32_SFLOAT;
        case Format::D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;
        case Format::D32_FLOAT_S8_UINT: return VK_FORMAT_D32_SFLOAT_S8_UINT;
        case Format::BC7_UNORM:    return VK_FORMAT_BC7_UNORM_BLOCK;
        case Format::BC7_SRGB:     return VK_FORMAT_BC7_SRGB_BLOCK;
        case Format::BC6H_UFLOAT:  return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        default: return VK_FORMAT_UNDEFINED;
    }
}

VkImageLayout VulkanDevice::ToVkLayout(ResourceState state) const {
    switch (state) {
        case ResourceState::Undefined:          return VK_IMAGE_LAYOUT_UNDEFINED;
        case ResourceState::ShaderRead:         return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        case ResourceState::ShaderWrite:        return VK_IMAGE_LAYOUT_GENERAL;
        case ResourceState::RenderTarget:       return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case ResourceState::ColorAttachment:    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        case ResourceState::DepthWrite:         return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case ResourceState::DepthAttachment:    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case ResourceState::DepthStencilWrite:  return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        case ResourceState::DepthStencilRead:   return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        case ResourceState::TransferSrc:        return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        case ResourceState::TransferDst:        return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        case ResourceState::Present:            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        default:                                return VK_IMAGE_LAYOUT_GENERAL;
    }
}

VkAccessFlags2 VulkanDevice::ToVkAccess(ResourceState state) const {
    switch (state) {
        case ResourceState::VertexBuffer:       return VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT;
        case ResourceState::IndexBuffer:        return VK_ACCESS_2_INDEX_READ_BIT;
        case ResourceState::UniformBuffer:      return VK_ACCESS_2_UNIFORM_READ_BIT;
        case ResourceState::ShaderRead:         return VK_ACCESS_2_SHADER_READ_BIT;
        case ResourceState::ShaderWrite:        return VK_ACCESS_2_SHADER_WRITE_BIT;
        case ResourceState::RenderTarget:       return VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        case ResourceState::ColorAttachment:    return VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        case ResourceState::DepthWrite:         return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case ResourceState::DepthAttachment:    return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case ResourceState::DepthStencilWrite:  return VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        case ResourceState::TransferSrc:        return VK_ACCESS_2_TRANSFER_READ_BIT;
        case ResourceState::TransferDst:        return VK_ACCESS_2_TRANSFER_WRITE_BIT;
        case ResourceState::IndirectArgument:   return VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        default:                                return VK_ACCESS_2_NONE;
    }
}

VkPipelineStageFlags2 VulkanDevice::ToVkStage(ResourceState state) const {
    switch (state) {
        case ResourceState::VertexBuffer:
        case ResourceState::IndexBuffer:        return VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
        case ResourceState::UniformBuffer:
        case ResourceState::ShaderRead:
        case ResourceState::ShaderWrite:        return VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        case ResourceState::RenderTarget:       return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        case ResourceState::ColorAttachment:    return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        case ResourceState::DepthWrite:
        case ResourceState::DepthAttachment:    return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        case ResourceState::DepthStencilWrite:
        case ResourceState::DepthStencilRead:   return VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        case ResourceState::TransferSrc:
        case ResourceState::TransferDst:        return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        case ResourceState::IndirectArgument:   return VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        default:                                return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }
}

} // namespace nge::rhi::vulkan

// ─── Device Factory ──────────────────────────────────────────────────────

namespace nge::rhi {

std::unique_ptr<IDevice> IDevice::Create(GraphicsAPI api) {
    switch (api) {
        case GraphicsAPI::Vulkan:
            return std::make_unique<vulkan::VulkanDevice>();
        case GraphicsAPI::DirectX12:
            NGE_LOG_ERROR("DX12 backend not yet implemented");
            return nullptr;
    }
    return nullptr;
}

} // namespace nge::rhi
