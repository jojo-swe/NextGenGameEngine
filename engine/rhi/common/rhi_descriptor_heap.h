#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <mutex>
#include <bitset>

namespace nge::rhi {

// ─── GPU Descriptor Heap Manager ─────────────────────────────────────────
// Manages a large, persistent descriptor heap for bindless rendering.
// Provides slot allocation/deallocation with free-list recycling.
// Supports both shader-visible and non-visible (staging) heaps.
//
// Design mirrors D3D12-style descriptor heaps but maps to Vulkan's
// VK_EXT_descriptor_indexing / descriptor buffer.

enum class DescriptorHeapType : u8 {
    CBV_SRV_UAV,   // Constant buffer / shader resource / unordered access
    Sampler,
    RTV,            // Render target (non-shader-visible)
    DSV,            // Depth stencil (non-shader-visible)
};

struct DescriptorHandle {
    u32 heapIndex = UINT32_MAX;
    u64 cpuHandle = 0;
    u64 gpuHandle = 0;

    bool IsValid() const { return heapIndex != UINT32_MAX; }
};

struct DescriptorHeapConfig {
    DescriptorHeapType type = DescriptorHeapType::CBV_SRV_UAV;
    u32 capacity = 65536;
    bool shaderVisible = true;
    const char* debugName = nullptr;
};

class DescriptorHeap {
public:
    bool Init(IDevice* device, const DescriptorHeapConfig& config);
    void Shutdown();

    // Allocate a single descriptor slot
    DescriptorHandle Allocate();

    // Allocate a contiguous range of descriptors
    DescriptorHandle AllocateRange(u32 count);

    // Free a descriptor slot (returns to free list)
    void Free(DescriptorHandle handle);

    // Free a contiguous range
    void FreeRange(DescriptorHandle handle, u32 count);

    // Copy descriptors from staging to shader-visible heap
    void CopyDescriptor(const DescriptorHandle& src, const DescriptorHandle& dst);

    // Query
    u32 GetAllocatedCount() const { return m_allocatedCount; }
    u32 GetCapacity() const { return m_config.capacity; }
    f32 GetOccupancy() const;
    bool IsFull() const { return m_freeList.empty() && m_nextFreeSlot >= m_config.capacity; }

    DescriptorHeapType GetType() const { return m_config.type; }
    bool IsShaderVisible() const { return m_config.shaderVisible; }

private:
    IDevice* m_device = nullptr;
    DescriptorHeapConfig m_config;

    u64 m_heapHandle = 0;           // VkDescriptorPool or descriptor buffer
    u32 m_descriptorSize = 0;       // Bytes per descriptor
    u32 m_nextFreeSlot = 0;
    u32 m_allocatedCount = 0;

    std::vector<u32> m_freeList;    // Recycled slots
    std::mutex m_mutex;
};

// ─── Descriptor Ring Buffer ──────────────────────────────────────────────
// For per-frame dynamic descriptors (constant buffers, dynamic textures).
// Wraps around each frame; no explicit free needed.

class DescriptorRingBuffer {
public:
    bool Init(IDevice* device, DescriptorHeapType type, u32 capacity, u32 framesInFlight);
    void Shutdown();

    // Advance to next frame (reclaims oldest frame's descriptors)
    void BeginFrame(u32 frameIndex);

    // Allocate from current frame's region
    DescriptorHandle Allocate();
    DescriptorHandle AllocateRange(u32 count);

    u32 GetAllocatedThisFrame() const { return m_currentFrameCount; }
    u32 GetCapacityPerFrame() const { return m_capacityPerFrame; }

private:
    IDevice* m_device = nullptr;

    u64 m_heapHandle = 0;
    u32 m_descriptorSize = 0;
    u32 m_totalCapacity = 0;
    u32 m_capacityPerFrame = 0;
    u32 m_framesInFlight = 0;
    u32 m_currentFrame = 0;
    u32 m_currentFrameOffset = 0;
    u32 m_currentFrameCount = 0;

    std::mutex m_mutex;
};

} // namespace nge::rhi
