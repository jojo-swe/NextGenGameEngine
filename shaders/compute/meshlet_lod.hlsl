// ─── Meshlet LOD Selection Compute Shader ─────────────────────────────────
// Selects the appropriate LOD level for each meshlet group based on
// screen-space projected error. Works with the virtual geometry system
// to stream and render only the necessary detail level.
//
// Each meshlet group has a DAG of LOD levels. This shader traverses
// the DAG and selects the coarsest level whose projected error is
// below the pixel threshold.

#include "../common/math.hlsl"

struct LODConstants {
    float4x4 viewProj;
    float4   cameraPos;
    float    screenHeight;      // Viewport height in pixels
    float    fovY;              // Vertical FOV in radians
    float    errorThreshold;    // Max allowed screen-space error in pixels (1.0 = 1px)
    uint     meshletGroupCount;
    uint     maxLODLevel;       // Maximum LOD level in the DAG
    uint     pad0;
    uint     pad1;
    uint     pad2;
};

[[vk::push_constant]] ConstantBuffer<LODConstants> pc;

// ─── Meshlet Group Data ──────────────────────────────────────────────────

struct MeshletGroup {
    float4 boundingSphere;  // xyz = center, w = radius
    float  lodError[8];     // Geometric error at each LOD level (world-space units)
    uint   lodMeshletOffset[8]; // Offset into meshlet buffer per LOD
    uint   lodMeshletCount[8];  // Number of meshlets per LOD
    uint   lodCount;        // Number of LOD levels available
    uint   pad0;
    uint   pad1;
    uint   pad2;
};

struct SelectedMeshlet {
    uint   groupIndex;
    uint   lodLevel;
    uint   meshletOffset;
    uint   meshletCount;
};

StructuredBuffer<MeshletGroup>    g_Groups     : register(t0, space23);
RWStructuredBuffer<SelectedMeshlet> g_Selected : register(u0, space23);
RWByteAddressBuffer               g_Counters   : register(u1, space23); // [0] = selected count

// ─── Screen-Space Error Projection ───────────────────────────────────────
// Converts world-space geometric error to screen-space pixel error.

float ProjectErrorToScreen(float worldError, float distance) {
    // Screen-space error = (worldError / distance) * (screenHeight / (2 * tan(fov/2)))
    float projFactor = pc.screenHeight / (2.0 * tan(pc.fovY * 0.5));
    return (worldError / max(distance, 0.001)) * projFactor;
}

// ─── Main LOD Selection Kernel ───────────────────────────────────────────

[numthreads(64, 1, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.meshletGroupCount) return;

    MeshletGroup group = g_Groups[DTid.x];

    // Distance from camera to group center
    float3 toGroup = group.boundingSphere.xyz - pc.cameraPos.xyz;
    float distance = length(toGroup);

    // Subtract radius for conservative distance estimate
    distance = max(distance - group.boundingSphere.w, 0.01);

    // Find the coarsest LOD whose screen error is acceptable
    uint selectedLOD = 0; // Start at finest (LOD 0)

    for (uint lod = group.lodCount - 1; lod > 0; --lod) {
        float screenError = ProjectErrorToScreen(group.lodError[lod], distance);

        if (screenError <= pc.errorThreshold) {
            selectedLOD = lod; // This coarser LOD is acceptable
            break;
        }
    }

    // Clamp to available LODs
    selectedLOD = min(selectedLOD, group.lodCount - 1);
    selectedLOD = min(selectedLOD, pc.maxLODLevel);

    // Skip if this LOD has no meshlets
    if (group.lodMeshletCount[selectedLOD] == 0) return;

    // Output selected meshlet range
    uint outIdx;
    g_Counters.InterlockedAdd(0, 1, outIdx);

    SelectedMeshlet sel;
    sel.groupIndex = DTid.x;
    sel.lodLevel = selectedLOD;
    sel.meshletOffset = group.lodMeshletOffset[selectedLOD];
    sel.meshletCount = group.lodMeshletCount[selectedLOD];
    g_Selected[outIdx] = sel;
}

// ─── LOD Statistics Kernel ───────────────────────────────────────────────
// Optional: collects LOD distribution stats for debugging.

// RWByteAddressBuffer g_Stats : register(u2, space23); // Per-LOD meshlet count
//
// [numthreads(64, 1, 1)]
// void CSStats(uint3 DTid : SV_DispatchThreadID) {
//     uint selectedCount = g_Counters.Load(0);
//     if (DTid.x >= selectedCount) return;
//     SelectedMeshlet sel = g_Selected[DTid.x];
//     g_Stats.InterlockedAdd(sel.lodLevel * 4, sel.meshletCount);
// }
