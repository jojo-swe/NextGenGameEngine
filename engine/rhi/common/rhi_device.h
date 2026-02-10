#pragma once

#include "engine/rhi/common/rhi_types.h"
#include "engine/core/math/math_types.h"
#include <memory>
#include <vector>
#include <string>

namespace nge::rhi {

// ─── Command List ────────────────────────────────────────────────────────
class ICommandList {
public:
    virtual ~ICommandList() = default;

    virtual void Begin() = 0;
    virtual void End() = 0;

    // ─── Resource barriers ────────────────────────────────────────────
    virtual void BufferBarrier(BufferHandle buffer, ResourceState before, ResourceState after) = 0;
    virtual void TextureBarrier(TextureHandle texture, ResourceState before, ResourceState after) = 0;

    // ─── Dynamic rendering (no render pass objects) ───────────────────
    virtual void BeginRendering(
        const TextureHandle* colorTargets, u32 colorCount,
        TextureHandle depthTarget,
        const ClearValue* clearValues,
        const Viewport& viewport, const Scissor& scissor,
        const LoadOp* colorLoadOps = nullptr, const LoadOp depthLoadOp = LoadOp::Clear) = 0;
    virtual void EndRendering() = 0;

    // ─── Pipeline binding ─────────────────────────────────────────────
    virtual void BindGraphicsPipeline(PipelineHandle pipeline) = 0;
    virtual void BindComputePipeline(PipelineHandle pipeline) = 0;
    virtual void BindRayTracingPipeline(PipelineHandle pipeline) = 0;

    // ─── Resource binding (bindless) ──────────────────────────────────
    virtual void SetPushConstants(const void* data, u32 size, u32 offset = 0) = 0;
    virtual void BindDescriptorSet(DescriptorSetHandle set, u32 setIndex = 0) = 0;

    // ─── Vertex/Index buffers ─────────────────────────────────────────
    virtual void BindVertexBuffer(BufferHandle buffer, u32 binding = 0, u64 offset = 0) = 0;
    virtual void BindIndexBuffer(BufferHandle buffer, IndexType type, u64 offset = 0) = 0;

    // ─── Draw commands ────────────────────────────────────────────────
    virtual void Draw(u32 vertexCount, u32 instanceCount = 1, u32 firstVertex = 0, u32 firstInstance = 0) = 0;
    virtual void DrawIndexed(u32 indexCount, u32 instanceCount = 1, u32 firstIndex = 0, i32 vertexOffset = 0, u32 firstInstance = 0) = 0;
    virtual void DrawIndirect(BufferHandle buffer, u64 offset, u32 drawCount, u32 stride) = 0;
    virtual void DrawIndexedIndirect(BufferHandle buffer, u64 offset, u32 drawCount, u32 stride) = 0;

    // ─── Mesh shader dispatch ─────────────────────────────────────────
    virtual void DrawMeshTasks(u32 groupCountX, u32 groupCountY = 1, u32 groupCountZ = 1) = 0;
    virtual void DrawMeshTasksIndirect(BufferHandle buffer, u64 offset, u32 drawCount, u32 stride) = 0;

    // ─── Compute dispatch ─────────────────────────────────────────────
    virtual void Dispatch(u32 groupCountX, u32 groupCountY = 1, u32 groupCountZ = 1) = 0;
    virtual void DispatchIndirect(BufferHandle buffer, u64 offset) = 0;

    // ─── Ray tracing ──────────────────────────────────────────────────
    virtual void TraceRays(u32 width, u32 height, u32 depth = 1) = 0;

    // ─── Transfer ─────────────────────────────────────────────────────
    virtual void CopyBuffer(BufferHandle src, u64 srcOffset, BufferHandle dst, u64 dstOffset, u64 size) = 0;
    virtual void CopyBufferToTexture(BufferHandle src, TextureHandle dst, u32 mipLevel = 0, u32 arrayLayer = 0) = 0;
    virtual void CopyTextureToBuffer(TextureHandle src, BufferHandle dst, u32 mipLevel = 0, u32 arrayLayer = 0) = 0;

    // ─── Acceleration structure ───────────────────────────────────────
    virtual void BuildAccelerationStructure(AccelStructHandle accelStruct) = 0;

    // ─── Buffer utilities ────────────────────────────────────────────
    virtual void FillBuffer(BufferHandle buffer, u64 offset, u64 size, u32 value) = 0;
    virtual void UpdateBuffer(BufferHandle buffer, u64 offset, u64 size, const void* data) = 0;
    virtual void BlitTexture(TextureHandle src, TextureHandle dst) = 0;

    // ─── Dynamic state ──────────────────────────────────────────────
    virtual void SetViewport(const Viewport& viewport) = 0;
    virtual void SetScissor(const Scissor& scissor) = 0;

    // ─── Synchronization ────────────────────────────────────────────
    virtual void WaitSemaphore(u64 semaphore, u64 value = 0) = 0;
    virtual void SignalSemaphore(u64 semaphore, u64 value = 0) = 0;

    // ─── Debug ────────────────────────────────────────────────────────
    virtual void BeginDebugLabel(const char* name, f32 r = 1, f32 g = 1, f32 b = 1) = 0;
    virtual void EndDebugLabel() = 0;
};

// ─── RHI Device ──────────────────────────────────────────────────────────
class IDevice {
public:
    virtual ~IDevice() = default;

    // ─── Lifecycle ────────────────────────────────────────────────────
    virtual bool Init(void* windowHandle, void* instanceHandle, u32 width, u32 height) = 0;
    virtual void Shutdown() = 0;
    virtual void WaitIdle() = 0;

    // ─── Swapchain ────────────────────────────────────────────────────
    virtual bool AcquireNextImage() = 0;
    virtual void Present() = 0;
    virtual void ResizeSwapchain(u32 width, u32 height) = 0;
    virtual TextureHandle GetSwapchainTexture() = 0;
    virtual Format GetSwapchainFormat() = 0;
    virtual u32 GetSwapchainWidth() = 0;
    virtual u32 GetSwapchainHeight() = 0;

    // ─── Resource creation ────────────────────────────────────────────
    virtual BufferHandle CreateBuffer(const BufferDesc& desc) = 0;
    virtual void DestroyBuffer(BufferHandle handle) = 0;
    virtual void* MapBuffer(BufferHandle handle) = 0;
    virtual void UnmapBuffer(BufferHandle handle) = 0;
    virtual void UpdateBuffer(BufferHandle handle, const void* data, usize size, usize offset = 0) = 0;

    virtual TextureHandle CreateTexture(const TextureDesc& desc) = 0;
    virtual void DestroyTexture(TextureHandle handle) = 0;

    virtual SamplerHandle CreateSampler(const SamplerDesc& desc) = 0;
    virtual void DestroySampler(SamplerHandle handle) = 0;

    virtual ShaderHandle CreateShader(const ShaderDesc& desc) = 0;
    virtual void DestroyShader(ShaderHandle handle) = 0;

    virtual PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) = 0;
    virtual PipelineHandle CreateComputePipeline(const ComputePipelineDesc& desc) = 0;
    virtual PipelineHandle CreateRayTracingPipeline(const RayTracingPipelineDesc& desc) = 0;
    virtual void DestroyPipeline(PipelineHandle handle) = 0;

    virtual AccelStructHandle CreateAccelerationStructure(const AccelStructDesc& desc) = 0;
    virtual void DestroyAccelerationStructure(AccelStructHandle handle) = 0;

    // ─── Bindless descriptors ─────────────────────────────────────────
    virtual u32 GetBindlessTextureIndex(TextureHandle handle) = 0;
    virtual u32 GetBindlessBufferIndex(BufferHandle handle) = 0;
    virtual DescriptorSetHandle GetGlobalDescriptorSet() = 0;

    // ─── Command lists ────────────────────────────────────────────────
    virtual ICommandList* GetCommandList(QueueType queue = QueueType::Graphics) = 0;
    virtual void SubmitCommandList(ICommandList* cmdList, QueueType queue = QueueType::Graphics) = 0;

    // ─── Capabilities ─────────────────────────────────────────────────
    virtual const DeviceCapabilities& GetCapabilities() const = 0;
    virtual FeatureTier GetFeatureTier() const = 0;
    virtual GraphicsAPI GetAPI() const = 0;
    virtual std::string GetDeviceName() const = 0;

    // ─── Frame management ─────────────────────────────────────────────
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual u32 GetFrameIndex() const = 0;

    // ─── Factory ──────────────────────────────────────────────────────
    static std::unique_ptr<IDevice> Create(GraphicsAPI api);
};

} // namespace nge::rhi
