// ─── Screen-Space Decal Projection Shader ────────────────────────────────
// Projects decals onto the GBuffer in screen space using an oriented
// bounding box (OBB) volume. Modifies albedo, normal, and roughness
// channels of the GBuffer without re-rendering geometry.
//
// Technique:
//   1. Rasterize decal OBB as a box mesh
//   2. For each pixel, reconstruct world position from depth
//   3. Transform world position into decal's local space
//   4. If inside [-1,1]³, sample decal textures and blend into GBuffer
//   5. Apply angle-based fade to avoid stretching on steep surfaces
//
// References:
//   - "Screen Space Decals in Warhammer 40,000: Space Marine" (Pope, SIGGRAPH 2012)
//   - "Practical Clustered Shading" (Olsson et al., 2012) for decal clustering

#include "../common/math.hlsl"

// ─── GBuffer (read for depth/normal, write for modification) ─────────────

Texture2D<float>  g_DepthBuffer : register(t0);
Texture2D<float4> g_GBuffer1    : register(t1); // RGB: World normal, A: roughness

// Decal textures
Texture2D<float4> g_DecalAlbedo  : register(t2); // RGBA (A = opacity)
Texture2D<float4> g_DecalNormal  : register(t3); // RG: tangent-space normal (optional)
Texture2D<float>  g_DecalMask    : register(t4); // Opacity mask (optional)

// GBuffer outputs (modified)
RWTexture2D<float4> g_OutAlbedo   : register(u0); // GBuffer0
RWTexture2D<float4> g_OutNormal   : register(u1); // GBuffer1
RWTexture2D<float4> g_OutEmissive : register(u2); // GBuffer2

SamplerState g_LinearClamp : register(s0);
SamplerState g_PointClamp  : register(s1);

// ─── Decal Instance Data ─────────────────────────────────────────────────

struct DecalInstance {
    float4x4 worldToDecal;     // Inverse of decal world transform
    float4x4 decalToWorld;     // Decal world transform
    float4   albedoTint;       // RGBA color tint
    float3   emissiveColor;    // Emissive contribution
    float    emissiveIntensity;
    float    normalStrength;   // Normal map blend strength (0-1)
    float    roughnessOverride;// -1 = don't modify, 0-1 = override
    float    metallicOverride; // -1 = don't modify, 0-1 = override
    float    angleFadeStart;   // Dot product threshold for fade start (default 0.5)
    float    angleFadeEnd;     // Dot product threshold for full fade (default 0.1)
    float    opacity;          // Global opacity multiplier
    float    depthFade;        // Fade near depth boundaries (default 0.1)
    uint     flags;            // Bit 0: modify albedo, Bit 1: modify normal, Bit 2: modify roughness, Bit 3: emissive
};

StructuredBuffer<DecalInstance> g_Decals : register(t5);

struct DecalProjectionConstants {
    float4x4 invViewProj;
    float3   cameraPos;
    float    pad0;
    float2   resolution;
    float2   invResolution;
    uint     decalCount;
    uint     maxDecalsPerPixel;
    float    pad1;
    float    pad2;
};

[[vk::push_constant]] ConstantBuffer<DecalProjectionConstants> cb;

// ─── Utility Functions ───────────────────────────────────────────────────

float3 ReconstructWorldPos(float2 uv, float depth) {
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;
    float4 worldPos = mul(cb.invViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}

float3 DecodeNormal(float3 encoded) {
    return normalize(encoded * 2.0 - 1.0);
}

float3 EncodeNormal(float3 n) {
    return n * 0.5 + 0.5;
}

// Reconstruct TBN from world normal for normal map blending
float3x3 BuildTBN(float3 worldNormal, float3 decalForward) {
    float3 T = normalize(cross(worldNormal, decalForward));
    float3 B = cross(worldNormal, T);
    return float3x3(T, B, worldNormal);
}

// ─── Single Decal Projection ─────────────────────────────────────────────

void ProjectDecal(uint2 pixelCoord, float2 uv, float3 worldPos, float3 worldNormal,
                   DecalInstance decal, inout float4 albedo, inout float4 normalRoughness,
                   inout float4 emissive) {
    // Transform world position to decal local space
    float4 localPos = mul(decal.worldToDecal, float4(worldPos, 1.0));
    float3 lp = localPos.xyz;

    // Check if inside decal OBB [-1,1]³
    if (any(abs(lp) > 1.0)) return;

    // UV from local XZ (decal projects along local Y axis)
    float2 decalUV = lp.xz * 0.5 + 0.5;

    // Angle-based fade: dot between surface normal and decal projection direction
    float3 decalUp = normalize(float3(decal.decalToWorld[1][0],
                                       decal.decalToWorld[1][1],
                                       decal.decalToWorld[1][2]));
    float angleDot = abs(dot(worldNormal, decalUp));
    float angleFade = smoothstep(decal.angleFadeEnd, decal.angleFadeStart, angleDot);
    if (angleFade <= 0.0) return;

    // Edge fade (soft edges at OBB boundaries)
    float3 edgeDist = 1.0 - abs(lp);
    float edgeFade = saturate(min(edgeDist.x, min(edgeDist.y, edgeDist.z)) / decal.depthFade);

    float totalOpacity = angleFade * edgeFade * decal.opacity;

    // Sample decal textures
    float4 decalColor = g_DecalAlbedo.SampleLevel(g_LinearClamp, decalUV, 0);
    totalOpacity *= decalColor.a;

    if (totalOpacity <= 0.001) return;

    // Optional mask
    float mask = g_DecalMask.SampleLevel(g_LinearClamp, decalUV, 0);
    totalOpacity *= mask;

    // ── Modify Albedo ────────────────────────────────────────────────
    if (decal.flags & 0x1) {
        float3 tintedColor = decalColor.rgb * decal.albedoTint.rgb;
        albedo.rgb = lerp(albedo.rgb, tintedColor, totalOpacity);
        if (decal.metallicOverride >= 0.0) {
            albedo.a = lerp(albedo.a, decal.metallicOverride, totalOpacity);
        }
    }

    // ── Modify Normal ────────────────────────────────────────────────
    if (decal.flags & 0x2) {
        float4 decalNormalSample = g_DecalNormal.SampleLevel(g_LinearClamp, decalUV, 0);
        float3 tangentNormal = decalNormalSample.rgb * 2.0 - 1.0;
        tangentNormal.xy *= decal.normalStrength;
        tangentNormal = normalize(tangentNormal);

        float3 decalForward = normalize(float3(decal.decalToWorld[2][0],
                                                decal.decalToWorld[2][1],
                                                decal.decalToWorld[2][2]));
        float3x3 TBN = BuildTBN(worldNormal, decalForward);
        float3 blendedNormal = normalize(mul(tangentNormal, TBN));

        normalRoughness.rgb = lerp(normalRoughness.rgb, EncodeNormal(blendedNormal),
                                    totalOpacity * decal.normalStrength);
    }

    // ── Modify Roughness ─────────────────────────────────────────────
    if (decal.flags & 0x4) {
        if (decal.roughnessOverride >= 0.0) {
            normalRoughness.a = lerp(normalRoughness.a, decal.roughnessOverride, totalOpacity);
        }
    }

    // ── Emissive ─────────────────────────────────────────────────────
    if (decal.flags & 0x8) {
        emissive.rgb += decal.emissiveColor * decal.emissiveIntensity * totalOpacity;
    }
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSDecalProjection(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float depth = g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0);
    if (depth >= 1.0) return; // Sky pixel

    float3 worldPos = ReconstructWorldPos(uv, depth);
    float4 gb1 = g_GBuffer1.SampleLevel(g_PointClamp, uv, 0);
    float3 worldNormal = DecodeNormal(gb1.rgb);

    // Read current GBuffer values
    float4 albedo = g_OutAlbedo[DTid.xy];
    float4 normalRoughness = g_OutNormal[DTid.xy];
    float4 emissiveAO = g_OutEmissive[DTid.xy];

    // Project all decals (front-to-back for correct blending)
    uint count = min(cb.decalCount, cb.maxDecalsPerPixel);
    for (uint i = 0; i < count; ++i) {
        ProjectDecal(DTid.xy, uv, worldPos, worldNormal, g_Decals[i],
                      albedo, normalRoughness, emissiveAO);
    }

    // Write modified GBuffer
    g_OutAlbedo[DTid.xy] = albedo;
    g_OutNormal[DTid.xy] = normalRoughness;
    g_OutEmissive[DTid.xy] = emissiveAO;
}

// ─── Clustered Decal Variant ─────────────────────────────────────────────
// For scenes with many decals, use a clustered approach where each
// screen tile has a list of affecting decals (similar to light culling).

struct DecalCluster {
    uint offset;
    uint count;
};

StructuredBuffer<DecalCluster> g_DecalClusters : register(t6);
StructuredBuffer<uint>         g_DecalIndices  : register(t7);

[numthreads(8, 8, 1)]
void CSDecalProjectionClustered(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float depth = g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0);
    if (depth >= 1.0) return;

    float3 worldPos = ReconstructWorldPos(uv, depth);
    float4 gb1 = g_GBuffer1.SampleLevel(g_PointClamp, uv, 0);
    float3 worldNormal = DecodeNormal(gb1.rgb);

    float4 albedo = g_OutAlbedo[DTid.xy];
    float4 normalRoughness = g_OutNormal[DTid.xy];
    float4 emissiveAO = g_OutEmissive[DTid.xy];

    // Tile-based lookup
    uint tileX = DTid.x / 16;
    uint tileY = DTid.y / 16;
    uint tilesPerRow = (uint(cb.resolution.x) + 15) / 16;
    uint tileIdx = tileX + tileY * tilesPerRow;

    DecalCluster cluster = g_DecalClusters[tileIdx];

    for (uint i = 0; i < cluster.count && i < cb.maxDecalsPerPixel; ++i) {
        uint decalIdx = g_DecalIndices[cluster.offset + i];
        ProjectDecal(DTid.xy, uv, worldPos, worldNormal, g_Decals[decalIdx],
                      albedo, normalRoughness, emissiveAO);
    }

    g_OutAlbedo[DTid.xy] = albedo;
    g_OutNormal[DTid.xy] = normalRoughness;
    g_OutEmissive[DTid.xy] = emissiveAO;
}
