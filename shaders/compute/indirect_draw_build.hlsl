// ─── Indirect Draw Argument Builder ───────────────────────────────────────
// Builds VkDrawIndexedIndirectCommand / VkDrawMeshTasksIndirectCommandEXT
// from the visible meshlet list produced by culling passes.
//
// Supports two modes:
//   1. Multi-Draw Indirect (MDI): one draw per mesh, batched by material
//   2. Mesh Shader Dispatch: one dispatch per visible meshlet group
//
// This shader runs after occlusion culling and meshlet LOD selection.

#include "../common/math.hlsl"

struct BuildConstants {
    uint visibleCount;       // Number of visible meshlet groups
    uint maxDrawCommands;    // Max output draw commands
    uint mode;               // 0 = MDI (indexed), 1 = mesh shader dispatch
    uint pad;
};

[[vk::push_constant]] ConstantBuffer<BuildConstants> pc;

// ─── Input: Selected Meshlets (from LOD selection + culling) ─────────────

struct SelectedMeshlet {
    uint groupIndex;
    uint lodLevel;
    uint meshletOffset;
    uint meshletCount;
};

struct MeshInfo {
    uint indexCount;
    uint firstIndex;
    int  vertexOffset;
    uint materialIndex;
};

StructuredBuffer<SelectedMeshlet> g_SelectedMeshlets : register(t0, space25);
StructuredBuffer<MeshInfo>        g_MeshInfos        : register(t1, space25);

// ─── Output: Draw Commands ───────────────────────────────────────────────

struct DrawIndexedCommand {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

struct DispatchMeshCommand {
    uint groupCountX;
    uint groupCountY;
    uint groupCountZ;
};

RWStructuredBuffer<DrawIndexedCommand> g_DrawCommands     : register(u0, space25);
RWStructuredBuffer<DispatchMeshCommand> g_DispatchCommands : register(u1, space25);
RWByteAddressBuffer                    g_DrawCount        : register(u2, space25); // [0] = count

// ─── Material Sort Key (for draw call batching) ──────────────────────────

RWStructuredBuffer<uint2> g_SortKeys : register(u3, space25); // x = material, y = draw index

// ─── MDI Build ───────────────────────────────────────────────────────────

[numthreads(64, 1, 1)]
void CSBuildMDI(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.visibleCount) return;

    SelectedMeshlet sel = g_SelectedMeshlets[DTid.x];

    // Look up mesh info for this group
    MeshInfo mesh = g_MeshInfos[sel.groupIndex];

    // Allocate a draw command slot
    uint drawIdx;
    g_DrawCount.InterlockedAdd(0, 1, drawIdx);
    if (drawIdx >= pc.maxDrawCommands) return;

    DrawIndexedCommand cmd;
    cmd.indexCount = mesh.indexCount;
    cmd.instanceCount = 1;
    cmd.firstIndex = mesh.firstIndex;
    cmd.vertexOffset = mesh.vertexOffset;
    cmd.firstInstance = sel.groupIndex; // Instance ID = group index for per-instance data

    g_DrawCommands[drawIdx] = cmd;

    // Store sort key for material batching
    g_SortKeys[drawIdx] = uint2(mesh.materialIndex, drawIdx);
}

// ─── Mesh Shader Dispatch Build ──────────────────────────────────────────

[numthreads(64, 1, 1)]
void CSBuildMeshDispatch(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.visibleCount) return;

    SelectedMeshlet sel = g_SelectedMeshlets[DTid.x];

    // Each meshlet group dispatches ceil(meshletCount / 32) workgroups
    // (assuming 32 meshlets per task shader workgroup)
    uint workgroups = (sel.meshletCount + 31) / 32;

    uint dispatchIdx;
    g_DrawCount.InterlockedAdd(0, 1, dispatchIdx);
    if (dispatchIdx >= pc.maxDrawCommands) return;

    DispatchMeshCommand cmd;
    cmd.groupCountX = workgroups;
    cmd.groupCountY = 1;
    cmd.groupCountZ = 1;

    g_DispatchCommands[dispatchIdx] = cmd;
}

// ─── Count Reset (run before build passes) ───────────────────────────────

[numthreads(1, 1, 1)]
void CSResetCounters(uint3 DTid : SV_DispatchThreadID) {
    g_DrawCount.Store(0, 0);
}
