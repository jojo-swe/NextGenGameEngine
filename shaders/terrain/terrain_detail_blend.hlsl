// ─── Terrain Detail Blending Shader ──────────────────────────────────────
// Multi-layer terrain material blending with:
//   - Triplanar projection for cliff/steep surfaces
//   - Height-based blending with smooth transitions
//   - Slope-based material selection (grass→rock→snow)
//   - Splat map (4-layer RGBA weight per texel)
//   - Detail normal overlay at close range
//   - Macro variation to break tiling
//
// References:
//   - "Advanced Terrain Texture-Splatting" (GPU Gems 3)
//   - "Far Cry 5 Terrain Technology" (Ubisoft, GDC 2018)
//   - "Ghost of Tsushima Terrain Rendering" (Sucker Punch, SIGGRAPH 2021)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

// Per-layer material arrays (4 layers in one draw call)
Texture2DArray<float4> g_AlbedoArray    : register(t0); // [layer][albedo]
Texture2DArray<float4> g_NormalArray    : register(t1); // [layer][normal]
Texture2DArray<float>  g_RoughnessArray : register(t2); // [layer][roughness]
Texture2DArray<float>  g_HeightArray    : register(t3); // [layer][height for blending]
Texture2D<float>       g_AOMap          : register(t4); // Baked terrain AO
Texture2D<float4>      g_SplatMap       : register(t5); // RGBA = weights for 4 layers
Texture2D<float>       g_MacroVariation : register(t6); // Large-scale color variation
Texture2D<float4>      g_DetailNormal   : register(t7); // Close-range detail overlay

SamplerState g_LinearWrap  : register(s0);
SamplerState g_LinearClamp : register(s1);
SamplerState g_Aniso8Wrap  : register(s2); // Anisotropic for terrain textures

struct TerrainBlendConstants {
    float4x4 world;
    float4x4 viewProj;
    float4x4 worldInvTranspose;
    float3   cameraPos;
    float    heightBlendSharpness;   // Height-blend transition (default 10.0)
    float3   lightDir;
    float    lightIntensity;
    float3   lightColor;
    float    triplanarSharpness;     // Triplanar blend exponent (default 8.0)
    float    slopeThresholdGrass;    // Max slope for grass (default 0.7)
    float    slopeThresholdRock;     // Max slope for rock (default 0.3)
    float    snowHeight;             // World-Y above which snow appears (default 100.0)
    float    snowBlendWidth;         // Snow transition width (default 10.0)
    float    uvScale;                // Base UV tiling (default 0.1)
    float    detailUVScale;          // Detail normal tiling (default 2.0)
    float    detailFadeDistance;     // Distance at which detail fades (default 50.0)
    float    macroUVScale;           // Macro variation tiling (default 0.001)
    float    macroStrength;          // Macro variation intensity (default 0.3)
    float    pad0;
    float    pad1;
    float    pad2;
};

[[vk::push_constant]] ConstantBuffer<TerrainBlendConstants> cb;

// ─── Vertex Shader ───────────────────────────────────────────────────────

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 texcoord : TEXCOORD0;
};

struct VSOutput {
    float4 clipPos    : SV_POSITION;
    float3 worldPos   : TEXCOORD0;
    float3 worldNormal: TEXCOORD1;
    float2 texcoord   : TEXCOORD2;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;
    float4 worldPos = mul(cb.world, float4(input.position, 1.0));
    output.clipPos = mul(cb.viewProj, worldPos);
    output.worldPos = worldPos.xyz;
    output.worldNormal = normalize(mul((float3x3)cb.worldInvTranspose, input.normal));
    output.texcoord = input.texcoord;
    return output;
}

// ─── Triplanar Projection ────────────────────────────────────────────────
// Projects textures from 3 axes, blended by surface normal.

struct TriplanarUVs {
    float2 uvX; // YZ plane
    float2 uvY; // XZ plane (top-down)
    float2 uvZ; // XY plane
    float3 weights;
};

TriplanarUVs ComputeTriplanar(float3 worldPos, float3 normal, float scale) {
    TriplanarUVs tri;
    tri.uvX = worldPos.yz * scale;
    tri.uvY = worldPos.xz * scale;
    tri.uvZ = worldPos.xy * scale;

    float3 blend = abs(normal);
    blend = pow(blend, cb.triplanarSharpness);
    blend /= (blend.x + blend.y + blend.z + 0.0001);
    tri.weights = blend;

    return tri;
}

float4 SampleTriplanar(Texture2DArray<float4> tex, SamplerState samp,
                         TriplanarUVs tri, uint layer) {
    float4 sX = tex.Sample(samp, float3(tri.uvX, layer));
    float4 sY = tex.Sample(samp, float3(tri.uvY, layer));
    float4 sZ = tex.Sample(samp, float3(tri.uvZ, layer));
    return sX * tri.weights.x + sY * tri.weights.y + sZ * tri.weights.z;
}

float SampleTriplanarScalar(Texture2DArray<float> tex, SamplerState samp,
                              TriplanarUVs tri, uint layer) {
    float sX = tex.Sample(samp, float3(tri.uvX, layer));
    float sY = tex.Sample(samp, float3(tri.uvY, layer));
    float sZ = tex.Sample(samp, float3(tri.uvZ, layer));
    return sX * tri.weights.x + sY * tri.weights.y + sZ * tri.weights.z;
}

// ─── Height-Based Blending ───────────────────────────────────────────────
// Prevents hard splat-map edges by using per-texel height to determine
// which material is "on top" at the boundary.

float4 HeightBlend(float4 weights, float4 heights) {
    float maxHeight = max(max(heights.x * weights.x, heights.y * weights.y),
                          max(heights.z * weights.z, heights.w * weights.w));

    float4 adjusted = heights * weights;
    float4 d = maxHeight - adjusted;
    float4 blended = saturate(1.0 - d * cb.heightBlendSharpness);

    float total = dot(blended, float4(1, 1, 1, 1));
    return total > 0.001 ? blended / total : weights;
}

// ─── Slope/Height Auto-Blending ──────────────────────────────────────────

float4 SlopeHeightWeights(float3 worldNormal, float worldY, float4 splatWeights) {
    float slope = 1.0 - saturate(worldNormal.y); // 0=flat, 1=vertical

    // Grass: flat areas
    float grassWeight = saturate((cb.slopeThresholdGrass - slope) * 5.0);

    // Rock: steep areas
    float rockWeight = saturate((slope - cb.slopeThresholdRock) * 5.0);

    // Snow: high altitude + relatively flat
    float snowBlend = saturate((worldY - cb.snowHeight) / cb.snowBlendWidth);
    float snowWeight = snowBlend * grassWeight; // Snow only on flat high areas
    grassWeight *= (1.0 - snowBlend);

    // Combine with splat map weights
    float4 autoWeights = float4(grassWeight, rockWeight, snowWeight, 0.0);
    float4 combined = splatWeights * 0.5 + autoWeights * 0.5;

    float total = dot(combined, float4(1, 1, 1, 1));
    return total > 0.001 ? combined / total : splatWeights;
}

// ─── Normal Blending (UDN) ───────────────────────────────────────────────
// Unreal-style detail normal blending.

float3 BlendNormalsUDN(float3 base, float3 detail) {
    return normalize(float3(base.xy + detail.xy, base.z));
}

// ─── Fragment Shader ─────────────────────────────────────────────────────

struct PSOutput {
    float4 albedo   : SV_TARGET0;
    float4 normal   : SV_TARGET1;
    float4 emissive : SV_TARGET2;
};

PSOutput PSMain(VSOutput input) {
    PSOutput output;

    float3 N = normalize(input.worldNormal);
    float3 V = normalize(cb.cameraPos - input.worldPos);
    float3 L = normalize(-cb.lightDir);
    float viewDist = length(cb.cameraPos - input.worldPos);

    // Splat weights from texture
    float4 splatWeights = g_SplatMap.Sample(g_LinearClamp, input.texcoord);

    // Apply slope/height auto-blending
    splatWeights = SlopeHeightWeights(N, input.worldPos.y, splatWeights);

    // Compute triplanar UVs
    TriplanarUVs tri = ComputeTriplanar(input.worldPos, N, cb.uvScale);

    // Sample per-layer heights for height-based blending
    float4 layerHeights;
    layerHeights.x = SampleTriplanarScalar(g_HeightArray, g_Aniso8Wrap, tri, 0);
    layerHeights.y = SampleTriplanarScalar(g_HeightArray, g_Aniso8Wrap, tri, 1);
    layerHeights.z = SampleTriplanarScalar(g_HeightArray, g_Aniso8Wrap, tri, 2);
    layerHeights.w = SampleTriplanarScalar(g_HeightArray, g_Aniso8Wrap, tri, 3);

    // Refine weights with height blending
    float4 finalWeights = HeightBlend(splatWeights, layerHeights);

    // Sample and blend materials
    float3 albedo = float3(0, 0, 0);
    float3 blendedNormal = float3(0, 0, 1);
    float roughness = 0.0;

    for (uint i = 0; i < 4; ++i) {
        float w = finalWeights[i];
        if (w < 0.001) continue;

        float4 layerAlbedo = SampleTriplanar(g_AlbedoArray, g_Aniso8Wrap, tri, i);
        float4 layerNormal = SampleTriplanar(g_NormalArray, g_Aniso8Wrap, tri, i);
        float layerRough = SampleTriplanarScalar(g_RoughnessArray, g_Aniso8Wrap, tri, i);

        albedo += layerAlbedo.rgb * w;
        blendedNormal += (layerNormal.rgb * 2.0 - 1.0) * w;
        roughness += layerRough * w;
    }

    blendedNormal = normalize(blendedNormal);

    // Macro variation (breaks large-scale tiling)
    float macro = g_MacroVariation.Sample(g_LinearWrap, input.worldPos.xz * cb.macroUVScale);
    albedo *= lerp(1.0, macro, cb.macroStrength);

    // Detail normal overlay (close range only)
    float detailFade = saturate(1.0 - viewDist / cb.detailFadeDistance);
    if (detailFade > 0.01) {
        float3 detail = g_DetailNormal.Sample(g_LinearWrap,
            input.worldPos.xz * cb.detailUVScale).rgb * 2.0 - 1.0;
        blendedNormal = BlendNormalsUDN(blendedNormal, detail * detailFade);
    }

    // Transform blended normal to world space (simplified: assume terrain is mostly Y-up)
    float3 worldTangent = normalize(cross(float3(0, 0, 1), N));
    float3 worldBitangent = cross(N, worldTangent);
    float3x3 TBN = float3x3(worldTangent, worldBitangent, N);
    float3 finalNormal = normalize(mul(blendedNormal, TBN));

    // AO
    float ao = g_AOMap.Sample(g_LinearClamp, input.texcoord);

    // Simple direct lighting
    float NdotL = saturate(dot(finalNormal, L));
    float3 lighting = albedo * NdotL * cb.lightColor * cb.lightIntensity * ao;
    float3 ambient = albedo * 0.03 * ao;

    // GBuffer output
    output.albedo = float4(albedo, 0.0);
    output.normal = float4(finalNormal * 0.5 + 0.5, roughness);
    output.emissive = float4(lighting + ambient, 1.0);

    return output;
}
