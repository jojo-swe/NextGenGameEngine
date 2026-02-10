#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <mutex>

namespace nge::rhi {

// ─── GPU Indirect Argument Buffer Builder ────────────────────────────────
// Builds and manages indirect draw/dispatch argument buffers for GPU-driven
// rendering. Supports DrawIndexed, Draw, Dispatch, and DrawMeshTasks
// argument types with batched writes and validation.
//
// Use cases:
//   - GPU-driven indirect draw calls (MDI)
//   - Compute dispatch indirect arguments
//   - Mesh shader indirect dispatch
//   - CPU-side argument buffer construction before GPU upload
//   - Argument count tracking for VK_KHR_draw_indirect_count

enum class IndirectArgType : u8 {
    Draw,
    DrawIndexed,
    Dispatch,
    DrawMeshTasks,
};

struct DrawArgs {
    u32 vertexCount;
    u32 instanceCount;
    u32 firstVertex;
    u32 firstInstance;
};

struct DrawIndexedArgs {
    u32 indexCount;
    u32 instanceCount;
    u32 firstIndex;
    i32 vertexOffset;
    u32 firstInstance;
};

struct DispatchArgs {
    u32 groupCountX;
    u32 groupCountY;
    u32 groupCountZ;
};

struct DrawMeshTasksArgs {
    u32 groupCountX;
    u32 groupCountY;
    u32 groupCountZ;
};

struct IndirectArgBuilderConfig {
    u32  maxDrawArgs = 8192;
    u32  maxDispatchArgs = 1024;
    bool validateArgs = true;
    u32  maxInstancesPerDraw = 65536;
    u32  maxVerticesPerDraw = 16777216;  // 16M
};

struct IndirectArgBuilderStats {
    u32 drawArgsCount;
    u32 drawIndexedArgsCount;
    u32 dispatchArgsCount;
    u32 meshTaskArgsCount;
    u32 totalArgs;
    u32 totalInstances;
    u64 totalVertices;
    u32 validationErrors;
    u64 bufferSizeBytes;
};

class IndirectArgBuilder {
public:
    bool Init(const IndirectArgBuilderConfig& config = {});
    void Shutdown();

    // Add draw arguments
    bool AddDraw(u32 vertexCount, u32 instanceCount, u32 firstVertex = 0, u32 firstInstance = 0);
    bool AddDrawIndexed(u32 indexCount, u32 instanceCount, u32 firstIndex = 0,
                         i32 vertexOffset = 0, u32 firstInstance = 0);
    bool AddDispatch(u32 groupX, u32 groupY, u32 groupZ);
    bool AddDrawMeshTasks(u32 groupX, u32 groupY, u32 groupZ);

    // Get argument buffers (raw data for GPU upload)
    const std::vector<DrawArgs>& GetDrawArgs() const;
    const std::vector<DrawIndexedArgs>& GetDrawIndexedArgs() const;
    const std::vector<DispatchArgs>& GetDispatchArgs() const;
    const std::vector<DrawMeshTasksArgs>& GetMeshTaskArgs() const;

    // Get counts
    u32 GetDrawCount() const;
    u32 GetDrawIndexedCount() const;
    u32 GetDispatchCount() const;
    u32 GetMeshTaskCount() const;
    u32 GetTotalArgCount() const;

    // Get buffer size needed for GPU upload
    u64 GetDrawBufferSize() const;
    u64 GetDrawIndexedBufferSize() const;
    u64 GetDispatchBufferSize() const;
    u64 GetMeshTaskBufferSize() const;
    u64 GetTotalBufferSize() const;

    // Sort draw args by instance count (descending) for better GPU utilization
    void SortDrawsByInstanceCount();

    // Merge compatible consecutive draws (same firstVertex, adjacent ranges)
    u32 MergeCompatibleDraws();

    // Clear all arguments
    void Clear();

    void Reset();

    IndirectArgBuilderStats GetStats() const;

private:
    bool ValidateDraw(u32 vertexCount, u32 instanceCount) const;
    bool ValidateDispatch(u32 groupX, u32 groupY, u32 groupZ) const;

    IndirectArgBuilderConfig m_config;

    std::vector<DrawArgs> m_drawArgs;
    std::vector<DrawIndexedArgs> m_drawIndexedArgs;
    std::vector<DispatchArgs> m_dispatchArgs;
    std::vector<DrawMeshTasksArgs> m_meshTaskArgs;

    u32 m_validationErrors = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
