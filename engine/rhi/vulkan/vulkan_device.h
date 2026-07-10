#pragma once

#include "engine/rhi/common/rhi_device.h"
#include "engine/core/logging/log.h"

#include <vulkan/vulkan.h>

#include <vector>
#include <array>

namespace nge::rhi::vulkan {

static constexpr u32 MAX_FRAMES_IN_FLIGHT = 2;
static constexpr u32 MAX_BUFFERS          = 65536;
static constexpr u32 MAX_TEXTURES         = 65536;
static constexpr u32 MAX_SAMPLERS         = 1024;
static constexpr u32 MAX_PIPELINES        = 4096;
static constexpr u32 MAX_SHADERS          = 4096;
static constexpr u32 MAX_ACCEL_STRUCTS    = 4096;

// ─── Vulkan Resource Wrappers ────────────────────────────────────────────

struct VulkanBuffer {
    VkBuffer       buffer     = VK_NULL_HANDLE;
    VkDeviceMemory memory     = VK_NULL_HANDLE;  // Will use VMA in production
    VkDeviceSize   size       = 0;
    BufferUsage    usage      = BufferUsage::None;
    MemoryUsage    memUsage   = MemoryUsage::GPU_Only;
    void*          mapped     = nullptr;
    u32            bindlessIndex = UINT32_MAX;
    bool           alive      = false;
};

struct VulkanTexture {
    VkImage        image      = VK_NULL_HANDLE;
    VkImageView    view       = VK_NULL_HANDLE;
    VkDeviceMemory memory     = VK_NULL_HANDLE;
    Format         format     = Format::Unknown;
    u32            width      = 0;
    u32            height     = 0;
    u32            depth      = 1;
    u32            mipLevels  = 1;
    u32            arrayLayers = 1;
    TextureType    type       = TextureType::Tex2D;
    u32            bindlessIndex = UINT32_MAX;
    bool           isSwapchainImage = false;
    bool           alive      = false;
};

struct VulkanSampler {
    VkSampler sampler = VK_NULL_HANDLE;
    bool      alive   = false;
};

struct VulkanShader {
    VkShaderModule module = VK_NULL_HANDLE;
    ShaderStage    stage  = ShaderStage::Vertex;
    bool           alive  = false;
};

struct VulkanPipeline {
    VkPipeline       pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout   = VK_NULL_HANDLE;
    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    bool             alive    = false;
};

struct VulkanAccelStruct {
    VkAccelerationStructureKHR accelStruct = VK_NULL_HANDLE;
    VkBuffer                   buffer      = VK_NULL_HANDLE;
    VkDeviceMemory             memory      = VK_NULL_HANDLE;
    bool                       alive       = false;
};

// ─── Per-Frame Data ──────────────────────────────────────────────────────

struct FrameData {
    VkCommandPool   commandPool   = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore     imageAvailable = VK_NULL_HANDLE;
    VkFence         inFlightFence  = VK_NULL_HANDLE;
};

// ─── Vulkan Command List ─────────────────────────────────────────────────

class VulkanCommandList final : public ICommandList {
public:
    VulkanCommandList() = default;
    void SetCommandBuffer(VkCommandBuffer cb) { m_cb = cb; }
    void SetDevice(class VulkanDevice* dev) { m_device = dev; }

    void Begin() override;
    void End() override;

    void BufferBarrier(BufferHandle buffer, ResourceState before, ResourceState after) override;
    void TextureBarrier(TextureHandle texture, ResourceState before, ResourceState after) override;

    void BeginRendering(
        const TextureHandle* colorTargets, u32 colorCount,
        TextureHandle depthTarget,
        const ClearValue* clearValues,
        const Viewport& viewport, const Scissor& scissor,
        const LoadOp* colorLoadOps, const LoadOp depthLoadOp) override;
    void EndRendering() override;

    void BindGraphicsPipeline(PipelineHandle pipeline) override;
    void BindComputePipeline(PipelineHandle pipeline) override;
    void BindRayTracingPipeline(PipelineHandle pipeline) override;

    void SetPushConstants(const void* data, u32 size, u32 offset) override;
    void BindDescriptorSet(DescriptorSetHandle set, u32 setIndex) override;

    void BindVertexBuffer(BufferHandle buffer, u32 binding, u64 offset) override;
    void BindIndexBuffer(BufferHandle buffer, IndexType type, u64 offset) override;

    void Draw(u32 vertexCount, u32 instanceCount, u32 firstVertex, u32 firstInstance) override;
    void DrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex, i32 vertexOffset, u32 firstInstance) override;
    void DrawIndirect(BufferHandle buffer, u64 offset, u32 drawCount, u32 stride) override;
    void DrawIndexedIndirect(BufferHandle buffer, u64 offset, u32 drawCount, u32 stride) override;

    void DrawMeshTasks(u32 groupCountX, u32 groupCountY, u32 groupCountZ) override;
    void DrawMeshTasksIndirect(BufferHandle buffer, u64 offset, u32 drawCount, u32 stride) override;

    void Dispatch(u32 groupCountX, u32 groupCountY, u32 groupCountZ) override;
    void DispatchIndirect(BufferHandle buffer, u64 offset) override;

    void TraceRays(u32 width, u32 height, u32 depth) override;

    void CopyBuffer(BufferHandle src, u64 srcOffset, BufferHandle dst, u64 dstOffset, u64 size) override;
    void CopyBufferToTexture(BufferHandle src, TextureHandle dst, u32 mipLevel, u32 arrayLayer) override;
    void CopyTextureToBuffer(TextureHandle src, BufferHandle dst, u32 mipLevel, u32 arrayLayer) override;

    void BuildAccelerationStructure(AccelStructHandle accelStruct) override;

    void FillBuffer(BufferHandle buffer, u64 offset, u64 size, u32 value) override;
    void UpdateBuffer(BufferHandle buffer, u64 offset, u64 size, const void* data) override;
    void BlitTexture(TextureHandle src, TextureHandle dst, u32 dstWidth, u32 dstHeight) override;

    void SetViewport(const Viewport& viewport) override;
    void SetScissor(const Scissor& scissor) override;

    void WaitSemaphore(u64 semaphore, u64 value) override;
    void SignalSemaphore(u64 semaphore, u64 value) override;

    void BeginDebugLabel(const char* name, f32 r, f32 g, f32 b) override;
    void EndDebugLabel() override;

    VkCommandBuffer GetVkCommandBuffer() const { return m_cb; }

private:
    VkCommandBuffer m_cb     = VK_NULL_HANDLE;
    VulkanDevice*   m_device = nullptr;
    PipelineHandle  m_currentPipeline;
};

// ─── Vulkan Device ───────────────────────────────────────────────────────

class VulkanDevice final : public IDevice {
public:
    VulkanDevice() = default;
    ~VulkanDevice() override;

    bool Init(void* windowHandle, void* instanceHandle, u32 width, u32 height) override;
    void Shutdown() override;
    void WaitIdle() override;

    bool AcquireNextImage() override;
    void Present() override;
    void ResizeSwapchain(u32 width, u32 height) override;
    TextureHandle GetSwapchainTexture() override;
    Format GetSwapchainFormat() override;
    u32 GetSwapchainWidth() override;
    u32 GetSwapchainHeight() override;

    BufferHandle CreateBuffer(const BufferDesc& desc) override;
    void DestroyBuffer(BufferHandle handle) override;
    void* MapBuffer(BufferHandle handle) override;
    void UnmapBuffer(BufferHandle handle) override;
    void UpdateBuffer(BufferHandle handle, const void* data, usize size, usize offset) override;

    TextureHandle CreateTexture(const TextureDesc& desc) override;
    void DestroyTexture(TextureHandle handle) override;

    SamplerHandle CreateSampler(const SamplerDesc& desc) override;
    void DestroySampler(SamplerHandle handle) override;

    ShaderHandle CreateShader(const ShaderDesc& desc) override;
    void DestroyShader(ShaderHandle handle) override;

    PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    PipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc) override;
    PipelineHandle CreateRayTracingPipeline(const RayTracingPipelineDesc& desc) override;
    void DestroyPipeline(PipelineHandle handle) override;

    AccelStructHandle CreateAccelerationStructure(const AccelStructDesc& desc) override;
    void DestroyAccelerationStructure(AccelStructHandle handle) override;

    u32 GetBindlessTextureIndex(TextureHandle handle) override;
    u32 GetBindlessBufferIndex(BufferHandle handle) override;
    DescriptorSetHandle GetGlobalDescriptorSet() override;

    ICommandList* GetCommandList(QueueType queue) override;
    void SubmitCommandList(ICommandList* cmdList, QueueType queue) override;

    const DeviceCapabilities& GetCapabilities() const override { return m_capabilities; }
    FeatureTier GetFeatureTier() const override { return m_featureTier; }
    GraphicsAPI GetAPI() const override { return GraphicsAPI::Vulkan; }
    std::string GetDeviceName() const override { return m_deviceName; }

    void BeginFrame() override;
    void EndFrame() override;
    u32 GetFrameIndex() const override { return m_frameIndex; }

    // ─── Internal Vulkan accessors (used by VulkanCommandList) ────────
    VkInstance GetVkInstance() const { return m_instance; }
    VkPhysicalDevice GetVkPhysicalDevice() const { return m_physicalDevice; }
    VkDevice GetVkDevice() const { return m_device; }
    VkQueue GetGraphicsQueue() const { return m_graphicsQueue; }
    u32 GetGraphicsQueueFamily() const { return m_graphicsQueueFamily; }
    u32 GetSwapchainImageCount() const { return static_cast<u32>(m_swapchainImages.size()); }
    VkFormat GetVkSwapchainFormat() const { return m_swapchainFormat; }
    VkCommandBuffer GetCurrentCommandBuffer() const;
    const VulkanBuffer& GetBuffer(BufferHandle h) const { return m_buffers[h.index]; }
    const VulkanTexture& GetTexture(TextureHandle h) const { return m_textures[h.index]; }
    const VulkanPipeline& GetPipeline(PipelineHandle h) const { return m_pipelines[h.index]; }

    VkFormat ToVkFormat(Format fmt) const;
    VkImageLayout ToVkLayout(ResourceState state) const;
    VkAccessFlags2 ToVkAccess(ResourceState state) const;
    VkPipelineStageFlags2 ToVkStage(ResourceState state) const;

    VkDescriptorSetLayout GetBindlessLayout() const { return m_bindlessLayout; }
    VkPipelineLayout GetGlobalPipelineLayout() const { return m_globalPipelineLayout; }
    bool IsSwapchainImageInitialized(u32 imageIndex) const;
    void MarkSwapchainImageInitialized(u32 imageIndex);

private:
    bool CreateInstance();
    bool SelectPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateSwapchain(u32 width, u32 height);
    void CleanupSwapchain();
    bool CreateSyncObjects();
    bool CreateDescriptorPool();
    void ProbeCapabilities();

    u32 FindMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties) const;
    VkBuffer CreateVkBuffer(VkDeviceSize size, VkBufferUsageFlags usage);
    VkDeviceMemory AllocateMemory(VkMemoryRequirements reqs, VkMemoryPropertyFlags props);

    // ─── Vulkan objects ───────────────────────────────────────────────
    VkInstance               m_instance       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice         m_physicalDevice = VK_NULL_HANDLE;
    VkDevice                 m_device         = VK_NULL_HANDLE;
    VkSurfaceKHR             m_surface        = VK_NULL_HANDLE;

    // Queues
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue  = VK_NULL_HANDLE;
    VkQueue m_transferQueue = VK_NULL_HANDLE;
    u32     m_graphicsQueueFamily = 0;
    u32     m_computeQueueFamily  = 0;
    u32     m_transferQueueFamily = 0;

    // Swapchain
    VkSwapchainKHR           m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage>     m_swapchainImages;
    std::vector<VkImageView> m_swapchainViews;
    std::vector<TextureHandle> m_swapchainTextureHandles;
    std::vector<VkSemaphore> m_swapchainRenderFinishedSemaphores;
    std::vector<bool>        m_swapchainImageInitialized;
    VkFormat                 m_swapchainFormat = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D               m_swapchainExtent = {};
    u32                      m_currentImageIndex = 0;

    // Per-frame
    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> m_frames;
    u32 m_frameIndex = 0;

    // Command list wrapper
    VulkanCommandList m_commandList;

    // Resource pools
    std::vector<VulkanBuffer>      m_buffers;
    std::vector<VulkanTexture>     m_textures;
    std::vector<VulkanSampler>     m_samplers;
    std::vector<VulkanShader>      m_shaders;
    std::vector<VulkanPipeline>    m_pipelines;
    std::vector<VulkanAccelStruct> m_accelStructs;

    // Bindless descriptors
    VkDescriptorPool      m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_bindlessLayout = VK_NULL_HANDLE;
    VkDescriptorSet       m_bindlessSet    = VK_NULL_HANDLE;
    VkPipelineLayout      m_globalPipelineLayout = VK_NULL_HANDLE;
    u32 m_nextBindlessTexture = 0;
    u32 m_nextBindlessBuffer  = 0;

    // Capabilities
    DeviceCapabilities m_capabilities{};
    FeatureTier        m_featureTier = FeatureTier::Tier0_Baseline;
    std::string        m_deviceName;

    VkPhysicalDeviceMemoryProperties m_memProperties{};
    bool m_initialized = false;
};

} // namespace nge::rhi::vulkan
