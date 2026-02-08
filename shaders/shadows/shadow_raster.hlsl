// ─── Virtual Shadow Map Rasterization ─────────────────────────────────────
// Renders shadow casters into virtual shadow map pages.
// Uses mesh shader pipeline for meshlet-level culling per shadow page.
//
// Each page has its own orthographic projection matrix derived from
// the clipmap level and light direction.

#include "../common/math.hlsl"

// ─── Per-Page Constants ──────────────────────────────────────────────────

struct ShadowPageConstants {
    float4x4 lightViewProj;   // Orthographic projection for this page
    uint     pageX;
    uint     pageY;
    uint     clipmapLevel;
    uint     physicalTileIdx; // Index into physical shadow atlas
    uint     atlasOffsetX;    // Pixel offset in atlas
    uint     atlasOffsetY;
    uint     tileSize;        // Pixels per tile (e.g., 128)
    uint     pad;
};

[[vk::push_constant]] ConstantBuffer<ShadowPageConstants> pc;

// ─── Instance / Vertex Data ──────────────────────────────────────────────

struct ShadowVertex {
    float3 position;
};

struct ShadowInstance {
    float4x4 worldMatrix;
    float4   boundingSphere;
    uint     meshletOffset;
    uint     meshletCount;
    uint     pad0;
    uint     pad1;
};

StructuredBuffer<ShadowVertex>   g_Vertices  : register(t0, space10);
StructuredBuffer<ShadowInstance> g_Instances  : register(t1, space10);
ByteAddressBuffer                g_Indices    : register(t2, space10);

// ─── Vertex Shader (traditional raster path) ─────────────────────────────

struct VSInput {
    float3 position : POSITION;
    uint   instanceId : SV_InstanceID;
};

struct VSOutput {
    float4 clipPos : SV_Position;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;

    ShadowInstance inst = g_Instances[input.instanceId];
    float4 worldPos = mul(inst.worldMatrix, float4(input.position, 1.0));
    output.clipPos = mul(pc.lightViewProj, worldPos);

    return output;
}

// ─── Pixel Shader (depth-only, no color output) ─────────────────────────

void PSMain(VSOutput input) {
    // Depth-only pass — nothing to write
    // Hardware writes depth automatically via SV_Position.z
}

// ─── Mesh Shader Path (for meshlet-level shadow culling) ─────────────────
// Amplification shader culls meshlets against the shadow frustum.
// Mesh shader outputs triangles for surviving meshlets.

// struct MeshletDesc {
//     uint vertexOffset;
//     uint triangleOffset;
//     uint vertexCount;
//     uint triangleCount;
// };
//
// StructuredBuffer<MeshletDesc> g_Meshlets : register(t3, space10);
//
// struct AmplPayload {
//     uint meshletIndices[32];
// };
//
// [numthreads(32, 1, 1)]
// void ASMain(uint GTid : SV_GroupThreadID, uint Gid : SV_GroupID) {
//     uint meshletIdx = Gid.x * 32 + GTid.x;
//     // Frustum cull meshlet bounding sphere against shadow frustum
//     // ...
//     // DispatchMesh(survivingCount, 1, 1, payload);
// }
//
// [outputtopology("triangle")]
// [numthreads(128, 1, 1)]
// void MSMain(uint GTid : SV_GroupThreadID, uint Gid : SV_GroupID,
//             in payload AmplPayload payload,
//             out vertices VSOutput verts[64],
//             out indices uint3 tris[124]) {
//     uint meshletIdx = payload.meshletIndices[Gid.x];
//     MeshletDesc meshlet = g_Meshlets[meshletIdx];
//     // Output vertices and triangles
//     // Transform positions by lightViewProj
// }
