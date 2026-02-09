// ─── GPU-Driven Indirect Draw Argument Builder ───────────────────────────
// Builds VkDrawIndexedIndirectCommand arguments entirely on the GPU
// from visibility results and instance data. Eliminates CPU readback
// for draw call setup in GPU-driven rendering pipelines.
//
// Pipeline:
//   1. Frustum/occlusion cull produces a visibility bitmask or compacted list
//   2. This shader reads visibility + per-instance mesh info
//   3. Outputs packed indirect draw commands + instance ID remap buffer
//   4. Optional: multi-draw indirect (MDI) with per-draw-call instance count
//
// Compatible with VK_KHR_draw_indirect_count for variable draw count.
//
// References:
//   - "GPU-Driven Rendering Pipelines" (Wihlidal, SIGGRAPH 2015)
//   - "Optimizing the Graphics Pipeline with Compute" (Ubisoft, GDC 2016)

// ─── Structures ──────────────────────────────────────────────────────────

// Per-instance mesh metadata (from CPU upload)
struct MeshInfo {
    uint indexCount;
    uint firstIndex;
    int  vertexOffset;
    uint meshletCount;      // For mesh shader path
    uint lodLevel;
    uint materialId;
    float2 pad;
};

// VkDrawIndexedIndirectCommand layout
struct DrawIndexedIndirectCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

// VkDrawMeshTasksIndirectCommandEXT layout
struct DrawMeshTasksIndirectCommand {
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
};

// ─── Buffers ─────────────────────────────────────────────────────────────

StructuredBuffer<MeshInfo>       g_MeshInfos       : register(t0); // Per-instance mesh data
StructuredBuffer<uint>           g_VisibilityMask   : register(t1); // Bitmask: 1 bit per instance
StructuredBuffer<uint>           g_CompactedIds     : register(t2); // Alternative: pre-compacted visible IDs

RWStructuredBuffer<DrawIndexedIndirectCommand>   g_DrawCommands    : register(u0);
RWStructuredBuffer<DrawMeshTasksIndirectCommand> g_MeshTaskCommands: register(u1);
RWStructuredBuffer<uint>                         g_InstanceRemap   : register(u2); // Maps draw instance → original instance
RWByteAddressBuffer                              g_DrawCount       : register(u3); // Atomic draw count

struct IndirectBuildConstants {
    uint totalInstances;
    uint maxDrawCommands;
    uint useBitmask;          // 1: read from visibility bitmask, 0: read from compacted list
    uint compactedCount;      // Only valid if useBitmask == 0
    uint enableMeshShaders;   // 1: also emit mesh task commands
    uint meshletGroupSize;    // Meshlets per task workgroup (default 32)
    uint enableMaterialBatch; // 1: batch by material ID
    uint pad0;
};

[[vk::push_constant]] ConstantBuffer<IndirectBuildConstants> cb;

// ─── Visibility Check ────────────────────────────────────────────────────

bool IsVisible(uint instanceId) {
    if (cb.useBitmask) {
        uint wordIdx = instanceId / 32;
        uint bitIdx = instanceId % 32;
        return (g_VisibilityMask[wordIdx] & (1u << bitIdx)) != 0;
    } else {
        // Compacted path: all IDs in the list are visible
        return true;
    }
}

// ─── Main: Build Draw Commands from Bitmask ──────────────────────────────
// Each thread processes one instance. Visible instances atomically
// append a draw command.

[numthreads(64, 1, 1)]
void CSBuildFromBitmask(uint3 DTid : SV_DispatchThreadID) {
    uint instanceId = DTid.x;
    if (instanceId >= cb.totalInstances) return;

    if (!IsVisible(instanceId)) return;

    // Atomically allocate a draw slot
    uint drawIdx;
    g_DrawCount.InterlockedAdd(0, 1, drawIdx);

    if (drawIdx >= cb.maxDrawCommands) return;

    MeshInfo mesh = g_MeshInfos[instanceId];

    DrawIndexedIndirectCommand cmd;
    cmd.indexCount = mesh.indexCount;
    cmd.instanceCount = 1;
    cmd.firstIndex = mesh.firstIndex;
    cmd.vertexOffset = mesh.vertexOffset;
    cmd.firstInstance = drawIdx; // Remap index

    g_DrawCommands[drawIdx] = cmd;
    g_InstanceRemap[drawIdx] = instanceId;

    // Optional: mesh shader path
    if (cb.enableMeshShaders) {
        DrawMeshTasksIndirectCommand meshCmd;
        meshCmd.groupCountX = (mesh.meshletCount + cb.meshletGroupSize - 1) / cb.meshletGroupSize;
        meshCmd.groupCountY = 1;
        meshCmd.groupCountZ = 1;
        g_MeshTaskCommands[drawIdx] = meshCmd;
    }
}

// ─── Main: Build from Compacted List ─────────────────────────────────────
// Reads from a pre-compacted visible instance ID buffer.

[numthreads(64, 1, 1)]
void CSBuildFromCompacted(uint3 DTid : SV_DispatchThreadID) {
    uint compactIdx = DTid.x;
    if (compactIdx >= cb.compactedCount) return;

    uint instanceId = g_CompactedIds[compactIdx];

    // Allocate draw slot
    uint drawIdx;
    g_DrawCount.InterlockedAdd(0, 1, drawIdx);

    if (drawIdx >= cb.maxDrawCommands) return;

    MeshInfo mesh = g_MeshInfos[instanceId];

    DrawIndexedIndirectCommand cmd;
    cmd.indexCount = mesh.indexCount;
    cmd.instanceCount = 1;
    cmd.firstIndex = mesh.firstIndex;
    cmd.vertexOffset = mesh.vertexOffset;
    cmd.firstInstance = drawIdx;

    g_DrawCommands[drawIdx] = cmd;
    g_InstanceRemap[drawIdx] = instanceId;

    if (cb.enableMeshShaders) {
        DrawMeshTasksIndirectCommand meshCmd;
        meshCmd.groupCountX = (mesh.meshletCount + cb.meshletGroupSize - 1) / cb.meshletGroupSize;
        meshCmd.groupCountY = 1;
        meshCmd.groupCountZ = 1;
        g_MeshTaskCommands[drawIdx] = meshCmd;
    }
}

// ─── Material Batching Pass ──────────────────────────────────────────────
// After draw commands are built, sort/batch them by material ID to
// minimize PSO switches. Uses a counting sort approach.

RWStructuredBuffer<DrawIndexedIndirectCommand> g_SortedDrawCommands : register(u4);
RWStructuredBuffer<uint>                       g_SortedRemap        : register(u5);
RWStructuredBuffer<uint>                       g_MaterialOffsets     : register(u6); // Prefix sum of counts per material
StructuredBuffer<uint>                         g_MaterialCounts      : register(t3); // Pre-computed counts

[numthreads(64, 1, 1)]
void CSMaterialBatch(uint3 DTid : SV_DispatchThreadID) {
    uint drawIdx = DTid.x;

    // Read current draw count
    uint totalDraws = g_DrawCount.Load(0);
    if (drawIdx >= totalDraws) return;

    uint instanceId = g_InstanceRemap[drawIdx];
    MeshInfo mesh = g_MeshInfos[instanceId];

    // Atomically get slot within the material's batch
    uint materialSlot;
    g_MaterialOffsets.InterlockedAdd(mesh.materialId, 1, materialSlot);

    // Write to sorted position
    g_SortedDrawCommands[materialSlot] = g_DrawCommands[drawIdx];
    g_SortedRemap[materialSlot] = instanceId;
}

// ─── Reset Draw Count ────────────────────────────────────────────────────

[numthreads(1, 1, 1)]
void CSResetDrawCount(uint3 DTid : SV_DispatchThreadID) {
    g_DrawCount.Store(0, 0);
}
