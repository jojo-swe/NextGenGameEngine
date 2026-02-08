// ─── Visibility Buffer Resolve Shader ─────────────────────────────────────
// Reads the visibility buffer (material ID + triangle ID + barycentrics)
// and reconstructs the full GBuffer: albedo, normals, roughness, metallic,
// emissive, motion vectors.
//
// This is the "deferred material" pass in a visibility buffer renderer.
// Each pixel fetches its triangle data, interpolates vertex attributes
// using barycentrics, samples material textures, and writes GBuffer outputs.

#define THREAD_GROUP_SIZE 8

// ─── Structures ──────────────────────────────────────────────────────────

struct VertexAttributes {
    float3 position;
    float3 normal;
    float4 tangent;
    float2 uv0;
    float2 uv1;
};

struct MaterialData {
    float4 baseColorFactor;
    float  metallicFactor;
    float  roughnessFactor;
    float  normalScale;
    float  occlusionStrength;
    float3 emissiveFactor;
    float  alphaCutoff;
    uint   baseColorTexIndex;
    uint   normalTexIndex;
    uint   metallicRoughnessTexIndex;
    uint   emissiveTexIndex;
    uint   occlusionTexIndex;
    uint   flags; // bit 0: alpha test, bit 1: double sided
    float2 pad;
};

// ─── Constant Buffer ─────────────────────────────────────────────────────

cbuffer ResolveConstants : register(b0) {
    float4x4 g_View;
    float4x4 g_Proj;
    float4x4 g_ViewProj;
    float4x4 g_PrevViewProj;
    float4x4 g_InvView;
    float2   g_TexelSize;
    float2   g_JitterOffset;    // TAA jitter
    uint     g_FrameIndex;
    uint     g_Pad[3];
};

// ─── Resources ───────────────────────────────────────────────────────────

// Visibility buffer: R32G32_UINT — x = triangleId | meshletId, y = barycentrics packed
Texture2D<uint2>    g_VisBuffer         : register(t0);

// Index/vertex buffers (global, bindless)
ByteAddressBuffer   g_GlobalIndexBuffer  : register(t1);
ByteAddressBuffer   g_GlobalVertexBuffer : register(t2);

// Material buffer
StructuredBuffer<MaterialData> g_Materials : register(t3);

// Instance transforms
struct InstanceData {
    float4x4 worldMatrix;
    float4x4 prevWorldMatrix;
    uint     materialId;
    uint     meshId;
    uint     flags;
    uint     pad;
};
StructuredBuffer<InstanceData> g_Instances : register(t4);

// Bindless texture array
Texture2D g_Textures[]       : register(t5, space1);
SamplerState g_AnisoSampler  : register(s0);

// GBuffer outputs
RWTexture2D<float4> g_GBufferAlbedo    : register(u0); // RGB = albedo, A = alpha
RWTexture2D<float4> g_GBufferNormal    : register(u1); // RG = octahedral normal, B = metallic, A = roughness
RWTexture2D<float4> g_GBufferMotion    : register(u2); // RG = motion vectors, B = meshlet ID, A = depth
RWTexture2D<float4> g_GBufferEmissive  : register(u3); // RGB = emissive, A = AO

// ─── Helpers ─────────────────────────────────────────────────────────────

// Decode packed barycentrics from uint
float3 DecodeBarycentrics(uint packed) {
    float u = float((packed >> 0)  & 0xFFFF) / 65535.0;
    float v = float((packed >> 16) & 0xFFFF) / 65535.0;
    return float3(1.0 - u - v, u, v);
}

// Decode triangle and instance IDs from visibility buffer
void DecodeVisBuffer(uint2 visData, out uint triangleId, out uint instanceId, out uint meshletId) {
    triangleId = visData.x & 0xFFFFF;       // 20 bits: triangle within meshlet
    meshletId  = (visData.x >> 20) & 0xFFF; // 12 bits: meshlet index
    instanceId = visData.y >> 16;            // Upper 16 bits of .y for instance
}

// Load vertex attributes from global vertex buffer
VertexAttributes LoadVertex(uint vertexIndex) {
    // Vertex layout: position(12) + normal(12) + tangent(16) + uv0(8) + uv1(8) = 56 bytes
    uint offset = vertexIndex * 56;

    VertexAttributes v;
    v.position = asfloat(g_GlobalVertexBuffer.Load3(offset));
    v.normal   = asfloat(g_GlobalVertexBuffer.Load3(offset + 12));
    v.tangent  = asfloat(g_GlobalVertexBuffer.Load4(offset + 24));
    v.uv0      = asfloat(g_GlobalVertexBuffer.Load2(offset + 40));
    v.uv1      = asfloat(g_GlobalVertexBuffer.Load2(offset + 48));
    return v;
}

// Interpolate vertex attributes using barycentrics
VertexAttributes InterpolateAttributes(VertexAttributes v0, VertexAttributes v1, VertexAttributes v2, float3 bary) {
    VertexAttributes result;
    result.position = v0.position * bary.x + v1.position * bary.y + v2.position * bary.z;
    result.normal   = normalize(v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z);
    result.tangent  = normalize(v0.tangent * bary.x + v1.tangent * bary.y + v2.tangent * bary.z);
    result.uv0      = v0.uv0 * bary.x + v1.uv0 * bary.y + v2.uv0 * bary.z;
    result.uv1      = v0.uv1 * bary.x + v1.uv1 * bary.y + v2.uv1 * bary.z;
    return result;
}

// Octahedral normal encoding (R16G16)
float2 OctahedralEncode(float3 n) {
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0) {
        float2 wrapped = (1.0 - abs(n.yx)) * (n.xy >= 0.0 ? 1.0 : -1.0);
        n.xy = wrapped;
    }
    return n.xy * 0.5 + 0.5;
}

// TBN matrix for normal mapping
float3 ApplyNormalMap(float3 normalMap, float3 N, float4 T) {
    float3 bitangent = cross(N, T.xyz) * T.w;
    float3x3 TBN = float3x3(T.xyz, bitangent, N);
    float3 mappedNormal = normalMap * 2.0 - 1.0;
    return normalize(mul(mappedNormal, TBN));
}

// ─── Main Resolve Kernel ─────────────────────────────────────────────────

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void CSMain(uint3 dispatchId : SV_DispatchThreadID) {
    uint2 pixel = dispatchId.xy;
    uint2 visData = g_VisBuffer[pixel];

    // Empty pixel (no geometry)
    if (visData.x == 0 && visData.y == 0) {
        g_GBufferAlbedo[pixel]   = float4(0, 0, 0, 0);
        g_GBufferNormal[pixel]   = float4(0.5, 0.5, 0, 0);
        g_GBufferMotion[pixel]   = float4(0, 0, 0, 0);
        g_GBufferEmissive[pixel] = float4(0, 0, 0, 1);
        return;
    }

    // Decode visibility buffer
    uint triangleId, instanceId, meshletId;
    DecodeVisBuffer(visData, triangleId, instanceId, meshletId);
    float3 bary = DecodeBarycentrics(visData.y & 0xFFFF);

    // Load instance data
    InstanceData instance = g_Instances[instanceId];
    MaterialData material = g_Materials[instance.materialId];

    // Load triangle indices from global index buffer
    uint baseIndex = triangleId * 3; // TODO: add meshlet/instance index offsets
    uint i0 = g_GlobalIndexBuffer.Load(baseIndex * 4);
    uint i1 = g_GlobalIndexBuffer.Load((baseIndex + 1) * 4);
    uint i2 = g_GlobalIndexBuffer.Load((baseIndex + 2) * 4);

    // Load and interpolate vertex attributes
    VertexAttributes v0 = LoadVertex(i0);
    VertexAttributes v1 = LoadVertex(i1);
    VertexAttributes v2 = LoadVertex(i2);
    VertexAttributes interp = InterpolateAttributes(v0, v1, v2, bary);

    // Transform to world space
    float3 worldPos = mul(instance.worldMatrix, float4(interp.position, 1.0)).xyz;
    float3 worldNormal = normalize(mul((float3x3)instance.worldMatrix, interp.normal));
    float4 worldTangent = float4(normalize(mul((float3x3)instance.worldMatrix, interp.tangent.xyz)), interp.tangent.w);

    // ── Sample material textures ─────────────────────────────────────

    // Base color
    float4 albedo = material.baseColorFactor;
    if (material.baseColorTexIndex != 0xFFFFFFFF) {
        albedo *= g_Textures[material.baseColorTexIndex].SampleLevel(g_AnisoSampler, interp.uv0, 0);
    }

    // Alpha test
    if ((material.flags & 1) && albedo.a < material.alphaCutoff) {
        g_GBufferAlbedo[pixel] = float4(0, 0, 0, 0);
        return;
    }

    // Normal map
    float3 shadingNormal = worldNormal;
    if (material.normalTexIndex != 0xFFFFFFFF) {
        float3 normalSample = g_Textures[material.normalTexIndex].SampleLevel(g_AnisoSampler, interp.uv0, 0).rgb;
        shadingNormal = ApplyNormalMap(normalSample, worldNormal, worldTangent);
    }

    // Metallic-roughness
    float metallic = material.metallicFactor;
    float roughness = material.roughnessFactor;
    if (material.metallicRoughnessTexIndex != 0xFFFFFFFF) {
        float4 mr = g_Textures[material.metallicRoughnessTexIndex].SampleLevel(g_AnisoSampler, interp.uv0, 0);
        roughness *= mr.g; // Green channel
        metallic *= mr.b;  // Blue channel
    }

    // Emissive
    float3 emissive = material.emissiveFactor;
    if (material.emissiveTexIndex != 0xFFFFFFFF) {
        emissive *= g_Textures[material.emissiveTexIndex].SampleLevel(g_AnisoSampler, interp.uv0, 0).rgb;
    }

    // Ambient occlusion
    float ao = 1.0;
    if (material.occlusionTexIndex != 0xFFFFFFFF) {
        ao = g_Textures[material.occlusionTexIndex].SampleLevel(g_AnisoSampler, interp.uv0, 0).r;
        ao = lerp(1.0, ao, material.occlusionStrength);
    }

    // ── Motion vectors ───────────────────────────────────────────────

    float4 clipPos = mul(g_ViewProj, float4(worldPos, 1.0));
    float4 prevWorldPos = mul(instance.prevWorldMatrix, float4(interp.position, 1.0));
    float4 prevClipPos = mul(g_PrevViewProj, prevWorldPos);

    float2 screenPos = (clipPos.xy / clipPos.w) * 0.5 + 0.5;
    float2 prevScreenPos = (prevClipPos.xy / prevClipPos.w) * 0.5 + 0.5;
    float2 motionVector = screenPos - prevScreenPos;

    // Remove TAA jitter from motion vectors
    motionVector -= g_JitterOffset;

    // ── Write GBuffer ────────────────────────────────────────────────

    g_GBufferAlbedo[pixel]   = float4(albedo.rgb, albedo.a);
    g_GBufferNormal[pixel]   = float4(OctahedralEncode(shadingNormal), metallic, roughness);
    g_GBufferMotion[pixel]   = float4(motionVector, float(meshletId) / 4096.0, clipPos.z / clipPos.w);
    g_GBufferEmissive[pixel] = float4(emissive, ao);
}
