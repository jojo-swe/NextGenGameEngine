// ─── Mesh Shader Pipeline (Amplification + Mesh Shader) ──────────────────
// GPU-driven rendering: the amplification shader culls meshlets,
// the mesh shader processes surviving meshlets into triangles.
//
// Pipeline: Amplification → Mesh → Fragment → Visibility Buffer

#include "../common/math.hlsl"
#include "../common/bindless.hlsl"

// ─── Meshlet Data Structures ─────────────────────────────────────────────

struct MeshletDescriptor {
    uint    vertexOffset;      // Offset into global vertex buffer
    uint    indexOffset;       // Offset into global index buffer
    uint    vertexCount;       // Number of vertices in this meshlet (max 64)
    uint    triangleCount;     // Number of triangles (max 124)
    float4  boundingSphere;    // xyz = center (object space), w = radius
    float4  coneApex;          // Backface cone apex (object space)
    float4  coneAxisCutoff;    // xyz = axis, w = cutoff (cos of half-angle)
};

struct InstanceData {
    float4x4 worldMatrix;
    float4x4 worldMatrixInvT; // For normal transform
    uint     meshletOffset;
    uint     meshletCount;
    uint     materialId;
    uint     instanceId;
};

// ─── Buffers ─────────────────────────────────────────────────────────────
StructuredBuffer<MeshletDescriptor> g_Meshlets    : register(t0, space5);
StructuredBuffer<InstanceData>      g_Instances   : register(t1, space5);
StructuredBuffer<float3>            g_Positions   : register(t2, space5);
StructuredBuffer<float3>            g_Normals     : register(t3, space5);
StructuredBuffer<float2>            g_UVs         : register(t4, space5);
ByteAddressBuffer                   g_Indices     : register(t5, space5);
StructuredBuffer<uint>              g_VisibleList : register(t6, space5);

struct MeshPushConstants {
    float4x4 viewProj;
    float4   cameraPos;
    float4   frustumPlanes[6];
    uint     meshletCount;
    uint     pad0;
    uint     pad1;
    uint     pad2;
};

[[vk::push_constant]] ConstantBuffer<MeshPushConstants> meshPC;

// ─── Amplification (Task) Shader ─────────────────────────────────────────
// One thread per meshlet. Performs per-meshlet culling:
//   1. Frustum cull (bounding sphere)
//   2. Backface cone cull
//   3. Occlusion cull (HZB, optional)
// Survivors are dispatched to the mesh shader.

struct TaskPayload {
    uint meshletIndices[32]; // Indices of surviving meshlets
    uint count;
};

groupshared TaskPayload s_Payload;

bool FrustumCullMeshlet(float3 center, float radius) {
    [unroll]
    for (uint i = 0; i < 6; ++i) {
        float dist = dot(meshPC.frustumPlanes[i].xyz, center) + meshPC.frustumPlanes[i].w;
        if (dist < -radius) return true;
    }
    return false;
}

bool BackfaceConeCull(float3 coneApex, float3 coneAxis, float coneCutoff, float3 cameraPos) {
    float3 viewDir = normalize(coneApex - cameraPos);
    return dot(viewDir, coneAxis) >= coneCutoff;
}

[numthreads(32, 1, 1)]
void AmplificationMain(uint GTid : SV_GroupThreadID, uint DTid : SV_DispatchThreadID) {
    bool visible = false;

    if (DTid < meshPC.meshletCount) {
        MeshletDescriptor meshlet = g_Meshlets[DTid];

        // TODO: transform bounding sphere to world space using instance data
        float3 center = meshlet.boundingSphere.xyz;
        float radius = meshlet.boundingSphere.w;

        visible = !FrustumCullMeshlet(center, radius);

        if (visible) {
            float3 coneApex = meshlet.coneApex.xyz;
            float3 coneAxis = meshlet.coneAxisCutoff.xyz;
            float coneCutoff = meshlet.coneAxisCutoff.w;
            visible = !BackfaceConeCull(coneApex, coneAxis, coneCutoff, meshPC.cameraPos.xyz);
        }
    }

    // Compact surviving meshlets
    if (GTid == 0) s_Payload.count = 0;
    GroupMemoryBarrierWithGroupSync();

    if (visible) {
        uint outIdx;
        InterlockedAdd(s_Payload.count, 1, outIdx);
        if (outIdx < 32) {
            s_Payload.meshletIndices[outIdx] = DTid;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Dispatch mesh shader groups for surviving meshlets
    DispatchMesh(s_Payload.count, 1, 1, s_Payload);
}

// ─── Mesh Shader ─────────────────────────────────────────────────────────
// Processes one meshlet per workgroup.
// Reads vertices/indices from global buffers, transforms to clip space,
// outputs to rasterizer.

struct MeshOutput {
    float4 position   : SV_Position;
    float3 worldPos   : WORLD_POS;
    float3 worldNorm  : WORLD_NORMAL;
    float2 uv         : TEXCOORD0;
    uint   meshletId  : MESHLET_ID;
    uint   materialId : MATERIAL_ID;
    uint   instanceId : INSTANCE_ID;
};

[outputtopology("triangle")]
[numthreads(64, 1, 1)]
void MeshMain(
    uint GTid : SV_GroupThreadID,
    uint GId  : SV_GroupID,
    in payload TaskPayload taskPayload,
    out vertices MeshOutput verts[64],
    out indices uint3 tris[124])
{
    uint meshletIdx = taskPayload.meshletIndices[GId];
    MeshletDescriptor meshlet = g_Meshlets[meshletIdx];

    SetMeshOutputCounts(meshlet.vertexCount, meshlet.triangleCount);

    // Output vertices
    if (GTid < meshlet.vertexCount) {
        uint vertIdx = meshlet.vertexOffset + GTid;
        float3 localPos = g_Positions[vertIdx];
        float3 localNorm = g_Normals[vertIdx];
        float2 texcoord = g_UVs[vertIdx];

        // TODO: use instance world matrix for transform
        float3 worldPos = localPos;
        float3 worldNorm = localNorm;

        verts[GTid].position   = mul(meshPC.viewProj, float4(worldPos, 1.0));
        verts[GTid].worldPos   = worldPos;
        verts[GTid].worldNorm  = worldNorm;
        verts[GTid].uv         = texcoord;
        verts[GTid].meshletId  = meshletIdx;
        verts[GTid].materialId = 0; // TODO: from instance
        verts[GTid].instanceId = 0; // TODO: from instance
    }

    // Output triangles
    if (GTid < meshlet.triangleCount) {
        uint indexBase = meshlet.indexOffset + GTid * 3;
        uint i0 = g_Indices.Load(indexBase * 4);
        uint i1 = g_Indices.Load((indexBase + 1) * 4);
        uint i2 = g_Indices.Load((indexBase + 2) * 4);
        tris[GTid] = uint3(i0, i1, i2);
    }
}
