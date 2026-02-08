#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>

namespace nge::rhi {

// ─── GPU Indirect Dispatch/Draw Builder ──────────────────────────────────
// Manages indirect command buffers for variable-size GPU workloads.
// The GPU writes dispatch/draw counts, and the CPU never reads them back.
//
// Supports:
//   - DrawIndexedIndirectCount (MDI with GPU-written count)
//   - DispatchIndirect (compute dispatch from GPU-written args)
//   - DrawMeshTasksIndirect (mesh shader dispatch)

struct DrawIndexedIndirectCommand {
    u32 indexCount;
    u32 instanceCount;
    u32 firstIndex;
    i32 vertexOffset;
    u32 firstInstance;
};

struct DrawMeshTasksIndirectCommand {
    u32 groupCountX;
    u32 groupCountY;
    u32 groupCountZ;
};

struct DispatchIndirectCommand {
    u32 groupCountX;
    u32 groupCountY;
    u32 groupCountZ;
};

class IndirectBufferBuilder {
public:
    bool Init(IDevice* device, u32 maxDrawCommands = 65536, u32 maxDispatchCommands = 256);
    void Shutdown();

    // Per-frame reset
    void BeginFrame();

    // ── CPU-side command building ────────────────────────────────────

    // Add a draw command (CPU-side, uploaded to GPU)
    u32 AddDrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex,
                         i32 vertexOffset, u32 firstInstance);

    // Add a mesh tasks dispatch command
    u32 AddMeshTasksDispatch(u32 groupX, u32 groupY = 1, u32 groupZ = 1);

    // Add a compute dispatch command
    u32 AddComputeDispatch(u32 groupX, u32 groupY = 1, u32 groupZ = 1);

    // Upload CPU commands to GPU
    void Upload(ICommandList* cmd);

    // ── GPU-side command buffers (written by GPU, read by indirect draw) ─

    // Get the draw command buffer (for vkCmdDrawIndexedIndirectCount)
    BufferHandle GetDrawCommandBuffer() const { return m_drawBuffer; }

    // Get the draw count buffer (single u32, written by GPU culling)
    BufferHandle GetDrawCountBuffer() const { return m_drawCountBuffer; }

    // Get the dispatch command buffer (for vkCmdDispatchIndirect)
    BufferHandle GetDispatchBuffer() const { return m_dispatchBuffer; }

    // Get mesh tasks buffer
    BufferHandle GetMeshTasksBuffer() const { return m_meshTasksBuffer; }

    // Reset draw count to zero (call before GPU writes new counts)
    void ResetDrawCount(ICommandList* cmd);

    // Stats
    u32 GetDrawCommandCount() const { return m_drawCount; }
    u32 GetDispatchCommandCount() const { return m_dispatchCount; }
    u32 GetMeshTasksCommandCount() const { return m_meshTasksCount; }
    u32 GetMaxDrawCommands() const { return m_maxDrawCommands; }

private:
    IDevice* m_device = nullptr;
    u32 m_maxDrawCommands = 0;
    u32 m_maxDispatchCommands = 0;

    // Draw indexed
    std::vector<DrawIndexedIndirectCommand> m_cpuDrawCommands;
    BufferHandle m_drawBuffer;
    BufferHandle m_drawCountBuffer;
    BufferHandle m_drawStagingBuffer;
    u32 m_drawCount = 0;

    // Mesh tasks
    std::vector<DrawMeshTasksIndirectCommand> m_cpuMeshTasksCommands;
    BufferHandle m_meshTasksBuffer;
    u32 m_meshTasksCount = 0;

    // Compute dispatch
    std::vector<DispatchIndirectCommand> m_cpuDispatchCommands;
    BufferHandle m_dispatchBuffer;
    u32 m_dispatchCount = 0;
};

} // namespace nge::rhi
