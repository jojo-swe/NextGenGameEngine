#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <mutex>

namespace nge::rhi {

// ─── GPU Multi-Draw Indirect Count Builder ───────────────────────────────
// Builds indirect draw/dispatch command buffers with GPU-determined count
// using VK_KHR_draw_indirect_count. The draw count is written by a GPU
// culling pass, enabling fully GPU-driven rendering with zero CPU readback.
//
// Supports:
//   - vkCmdDrawIndirectCount
//   - vkCmdDrawIndexedIndirectCount
//   - vkCmdDrawMeshTasksIndirectCountEXT
//   - vkCmdDispatchIndirect

enum class IndirectCountType : u8 {
    Draw,
    DrawIndexed,
    MeshTasks,
    Dispatch,
};

struct IndirectDrawArgs {
    u32 vertexCount;
    u32 instanceCount;
    u32 firstVertex;
    u32 firstInstance;
};

struct IndirectDrawIndexedArgs {
    u32 indexCount;
    u32 instanceCount;
    u32 firstIndex;
    i32 vertexOffset;
    u32 firstInstance;
};

struct IndirectMeshTasksArgs {
    u32 groupCountX;
    u32 groupCountY;
    u32 groupCountZ;
};

struct IndirectDispatchArgs {
    u32 groupCountX;
    u32 groupCountY;
    u32 groupCountZ;
};

struct IndirectCountBufferDesc {
    IndirectCountType type;
    u32               maxDrawCount;    // Upper bound for safety
    std::string       debugName;
};

struct IndirectCountBufferStats {
    u32 activeBuffers;
    u32 totalMaxDraws;
    u64 totalMemoryBytes;
};

class IndirectCountBuilder {
public:
    bool Init(IDevice* device);
    void Shutdown();

    // Create an indirect command buffer + count buffer pair
    u32 CreateBuffer(const IndirectCountBufferDesc& desc);

    // Destroy a buffer pair
    void DestroyBuffer(u32 bufferId);

    // Get the argument buffer handle (for GPU culling pass to write draw args)
    BufferHandle GetArgBuffer(u32 bufferId) const;

    // Get the count buffer handle (for GPU culling pass to write draw count)
    BufferHandle GetCountBuffer(u32 bufferId) const;

    // Clear the count to zero (call at frame start before culling)
    void ClearCount(ICommandList* cmd, u32 bufferId);

    // Issue indirect draw with count
    void DrawIndirectCount(ICommandList* cmd, u32 bufferId);
    void DrawIndexedIndirectCount(ICommandList* cmd, u32 bufferId);
    void DrawMeshTasksIndirectCount(ICommandList* cmd, u32 bufferId);
    void DispatchIndirect(ICommandList* cmd, u32 bufferId);

    // Get the max draw count for a buffer
    u32 GetMaxDrawCount(u32 bufferId) const;

    IndirectCountBufferStats GetStats() const;

private:
    struct IndirectCountBuffer {
        IndirectCountBufferDesc desc;
        BufferHandle argBuffer;
        BufferHandle countBuffer;
        u32 stride;
        bool alive = true;
    };

    IDevice* m_device = nullptr;
    std::vector<IndirectCountBuffer> m_buffers;
    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
