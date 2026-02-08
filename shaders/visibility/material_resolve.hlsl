// ─── Material Resolve (Visibility Buffer → Shading) ──────────────────────
// Full-screen compute shader that reads the visibility buffer, reconstructs
// surface attributes from mesh data, evaluates materials, and outputs
// lit color to the scene HDR target.
//
// This replaces the traditional G-buffer pass. Instead of storing redundant
// per-pixel attributes, we store only triangle/instance IDs and reconstruct
// everything on demand — saving bandwidth significantly.
//
// Input:
//   - Visibility buffer (R32G32_UINT): meshletId | triangleId, materialId | instanceId
//   - Depth buffer (D32_FLOAT)
//   - Global vertex/index/meshlet buffers (bindless)
//   - Material buffer (bindless)
//   - Light buffer
//
// Output:
//   - Scene HDR color (RGBA16F)
//   - Motion vectors (RG16F) for TSR
//   - (Optional) G-buffer outputs for deferred passes

#include "../common/math.hlsl"
#include "../common/brdf.hlsl"
#include "../common/bindless.hlsl"

// ─── Structures ──────────────────────────────────────────────────────────

struct MaterialResolveConstants {
    float4x4 invViewProj;
    float4x4 prevViewProj;
    float4   cameraPos;
    float4   sunDirection;
    float4   sunColor;
    uint2    screenSize;
    uint     frameIndex;
    uint     lightCount;
    float    exposure;
    float    pad0;
    float    pad1;
    float    pad2;
};

[[vk::push_constant]] ConstantBuffer<MaterialResolveConstants> pc;

// ─── Buffers ─────────────────────────────────────────────────────────────

Texture2D<uint2>     g_VisBuffer     : register(t0, space5);
Texture2D<float>     g_DepthBuffer   : register(t1, space5);

struct Vertex {
    float3 position;
    float3 normal;
    float2 texcoord;
    float4 tangent;
};

struct MeshletDesc {
    uint vertexOffset;
    uint triangleOffset;
    uint vertexCount;
    uint triangleCount;
};

struct InstanceData {
    float4x4 worldMatrix;
    float4x4 worldMatrixInvT;
    float4   boundingSphere;
    uint     meshletOffset;
    uint     meshletCount;
    uint     materialId;
    uint     flags;
};

struct MaterialData {
    float4 baseColor;
    float  metallic;
    float  roughness;
    float  emissiveStrength;
    float  alphaCutoff;
    uint   baseColorTexIdx;
    uint   normalTexIdx;
    uint   metalRoughTexIdx;
    uint   emissiveTexIdx;
    uint   occlusionTexIdx;
    uint   flags;
    float  clearCoat;
    float  clearCoatRoughness;
};

struct LightData {
    float3 position;
    float  radius;
    float3 color;
    float  pad0;
    float3 direction;
    float  cosInner;
    float  cosOuter;
    uint   type;      // 0=point, 1=spot, 2=directional
    float  area;
    uint   shadowIdx;
};

StructuredBuffer<Vertex>      g_Vertices  : register(t2, space5);
ByteAddressBuffer             g_Indices   : register(t3, space5);
StructuredBuffer<MeshletDesc> g_Meshlets  : register(t4, space5);
StructuredBuffer<InstanceData> g_Instances : register(t5, space5);
StructuredBuffer<MaterialData> g_Materials : register(t6, space5);
StructuredBuffer<LightData>   g_Lights    : register(t7, space5);
ByteAddressBuffer             g_MeshletVertices  : register(t8, space5);
ByteAddressBuffer             g_MeshletTriangles : register(t9, space5);

RWTexture2D<float4> g_OutputColor   : register(u0, space5);
RWTexture2D<float2> g_OutputMotion  : register(u1, space5);

SamplerState g_Sampler : register(s0, space5);

// ─── Visibility Buffer Decoding ──────────────────────────────────────────

struct VisBufferData {
    uint meshletId;
    uint triangleId;
    uint materialId;
    uint instanceId;
    bool valid;
};

VisBufferData DecodeVisBuffer(uint2 raw) {
    VisBufferData d;
    d.valid = (raw.x != 0 || raw.y != 0);
    // Packing: x = (meshletId << 8) | triangleId, y = (materialId << 16) | instanceId
    d.meshletId  = raw.x >> 8;
    d.triangleId = raw.x & 0xFF;
    d.materialId = raw.y >> 16;
    d.instanceId = raw.y & 0xFFFF;
    return d;
}

// ─── Barycentric Reconstruction ──────────────────────────────────────────

struct SurfaceAttributes {
    float3 worldPos;
    float3 worldNormal;
    float2 texcoord;
    float4 tangent;
    float3 prevWorldPos; // For motion vectors
};

SurfaceAttributes ReconstructSurface(VisBufferData vis, float2 screenUV, float depth) {
    SurfaceAttributes surf;

    // Get meshlet and instance
    MeshletDesc meshlet = g_Meshlets[vis.meshletId];
    InstanceData inst = g_Instances[vis.instanceId];

    // Get triangle vertex indices from meshlet
    uint triBase = meshlet.triangleOffset + vis.triangleId * 3;
    uint li0 = g_MeshletTriangles.Load(triBase + 0);
    uint li1 = g_MeshletTriangles.Load(triBase + 1);
    uint li2 = g_MeshletTriangles.Load(triBase + 2);

    // Map local meshlet indices to global vertex indices
    uint gi0 = g_MeshletVertices.Load((meshlet.vertexOffset + li0) * 4);
    uint gi1 = g_MeshletVertices.Load((meshlet.vertexOffset + li1) * 4);
    uint gi2 = g_MeshletVertices.Load((meshlet.vertexOffset + li2) * 4);

    // Load vertices
    Vertex v0 = g_Vertices[gi0];
    Vertex v1 = g_Vertices[gi1];
    Vertex v2 = g_Vertices[gi2];

    // Transform to clip space to compute barycentrics
    float4 p0 = mul(inst.worldMatrix, float4(v0.position, 1.0));
    float4 p1 = mul(inst.worldMatrix, float4(v1.position, 1.0));
    float4 p2 = mul(inst.worldMatrix, float4(v2.position, 1.0));

    // Reconstruct world position from depth
    float2 ndc = screenUV * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 clipPos = float4(ndc, depth, 1.0);
    float4 worldPos4 = mul(pc.invViewProj, clipPos);
    surf.worldPos = worldPos4.xyz / worldPos4.w;

    // Compute barycentrics using the screen-space triangle
    float3 bary = BarycentricInterpolation(
        p0.xyz / p0.w, p1.xyz / p1.w, p2.xyz / p2.w,
        float3(ndc, depth));

    // Interpolate attributes
    float3 localNormal = v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z;
    surf.worldNormal = normalize(mul((float3x3)inst.worldMatrixInvT, localNormal));

    surf.texcoord = v0.texcoord * bary.x + v1.texcoord * bary.y + v2.texcoord * bary.z;
    surf.tangent = v0.tangent * bary.x + v1.tangent * bary.y + v2.tangent * bary.z;
    surf.tangent.xyz = normalize(surf.tangent.xyz);

    // Previous frame position for motion vectors
    // TODO: use previous frame's instance transform
    float4 prevClip = mul(pc.prevViewProj, float4(surf.worldPos, 1.0));
    surf.prevWorldPos = prevClip.xyz / prevClip.w;

    return surf;
}

// ─── Lighting ────────────────────────────────────────────────────────────

float3 EvaluateLighting(SurfaceAttributes surf, MaterialData mat) {
    float3 N = surf.worldNormal;
    float3 V = normalize(pc.cameraPos.xyz - surf.worldPos);

    // Sample textures
    float3 baseColor = mat.baseColor.rgb;
    float metallic = mat.metallic;
    float roughness = mat.roughness;

    if (mat.baseColorTexIdx != 0xFFFFFFFF) {
        baseColor *= SampleBindlessTexture(mat.baseColorTexIdx, surf.texcoord).rgb;
    }
    if (mat.metalRoughTexIdx != 0xFFFFFFFF) {
        float2 mr = SampleBindlessTexture(mat.metalRoughTexIdx, surf.texcoord).bg;
        metallic *= mr.x;
        roughness *= mr.y;
    }

    // Normal mapping
    if (mat.normalTexIdx != 0xFFFFFFFF) {
        float3 normalMap = SampleBindlessTexture(mat.normalTexIdx, surf.texcoord).rgb * 2.0 - 1.0;
        float3 T = normalize(surf.tangent.xyz);
        float3 B = cross(N, T) * surf.tangent.w;
        N = normalize(T * normalMap.x + B * normalMap.y + N * normalMap.z);
    }

    float3 totalRadiance = float3(0, 0, 0);

    // Directional sun light
    {
        float3 L = -pc.sunDirection.xyz;
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0) {
            float3 H = normalize(V + L);
            float3 brdf = EvaluatePBR_BRDF(baseColor, metallic, roughness, N, V, L, H);
            totalRadiance += brdf * pc.sunColor.rgb * NdotL;
        }
    }

    // Point/spot lights
    for (uint i = 1; i < pc.lightCount; ++i) {
        LightData light = g_Lights[i];

        float3 toLight = light.position - surf.worldPos;
        float dist = length(toLight);
        if (dist > light.radius) continue;

        float3 L = toLight / dist;

        // Spot light cone
        if (light.type == 1) {
            float cosAngle = dot(-L, light.direction);
            if (cosAngle < light.cosOuter) continue;
            float spotFade = saturate((cosAngle - light.cosOuter) / (light.cosInner - light.cosOuter));
            L *= spotFade; // Modulate by spot attenuation
        }

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;

        // Distance attenuation (inverse-square with radius falloff)
        float attenuation = 1.0 / (dist * dist + 1.0);
        float falloff = saturate(1.0 - pow(dist / light.radius, 4.0));
        falloff *= falloff;
        attenuation *= falloff;

        float3 H = normalize(V + L);
        float3 brdf = EvaluatePBR_BRDF(baseColor, metallic, roughness, N, V, L, H);
        totalRadiance += brdf * light.color * NdotL * attenuation;
    }

    // Emissive
    if (mat.emissiveStrength > 0.0 && mat.emissiveTexIdx != 0xFFFFFFFF) {
        float3 emissive = SampleBindlessTexture(mat.emissiveTexIdx, surf.texcoord).rgb;
        totalRadiance += emissive * mat.emissiveStrength;
    }

    // Ambient (placeholder — will be replaced by GI probes)
    totalRadiance += baseColor * 0.03;

    return totalRadiance;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.screenSize.x || DTid.y >= pc.screenSize.y) return;

    uint2 pixel = DTid.xy;
    float2 screenUV = (float2(pixel) + 0.5) / float2(pc.screenSize);

    // Read visibility buffer
    uint2 visRaw = g_VisBuffer[pixel];
    VisBufferData vis = DecodeVisBuffer(visRaw);

    if (!vis.valid) {
        // Sky pixel — handled by atmosphere pass
        g_OutputColor[pixel] = float4(0, 0, 0, 0);
        g_OutputMotion[pixel] = float2(0, 0);
        return;
    }

    // Read depth
    float depth = g_DepthBuffer[pixel];

    // Reconstruct surface
    SurfaceAttributes surf = ReconstructSurface(vis, screenUV, depth);

    // Get material
    MaterialData mat = g_Materials[vis.materialId];

    // Alpha test
    if ((mat.flags & 0x4) != 0) { // AlphaTest flag
        float alpha = mat.baseColor.a;
        if (mat.baseColorTexIdx != 0xFFFFFFFF) {
            alpha *= SampleBindlessTexture(mat.baseColorTexIdx, surf.texcoord).a;
        }
        if (alpha < mat.alphaCutoff) {
            g_OutputColor[pixel] = float4(0, 0, 0, 0);
            g_OutputMotion[pixel] = float2(0, 0);
            return;
        }
    }

    // Evaluate lighting
    float3 color = EvaluateLighting(surf, mat);

    // Exposure
    color *= exp2(pc.exposure);

    g_OutputColor[pixel] = float4(color, 1.0);

    // Motion vectors: current screen pos - previous screen pos
    float2 prevUV = surf.prevWorldPos.xy * 0.5 + 0.5;
    prevUV.y = 1.0 - prevUV.y;
    g_OutputMotion[pixel] = screenUV - prevUV;
}
