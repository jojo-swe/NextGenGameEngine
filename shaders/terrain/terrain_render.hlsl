// ─── Terrain CDLOD Rendering ─────────────────────────────────────────────
// Vertex shader displaces a flat grid patch using heightmap sampling.
// Fragment shader blends material layers via splatmap weights.
// Supports LOD morphing for seamless transitions between clipmap levels.

#include "../common/math.hlsl"
#include "../common/brdf.hlsl"

struct TerrainConstants {
    float4   worldOrigin;    // xyz = origin, w = worldSize
    float4   heightParams;   // x = heightScale, y = texelSize, z = morphRange, w = unused
    float4   lodParams;      // x = lodDistanceScale, y = patchSize, z = lodLevels, w = unused
    float4   cameraPos;
    float4x4 viewProj;
    float4   sunDirection;
    float4   sunColor;
};

struct PatchData {
    float2 worldOffset;
    float  scale;
    uint   lodLevel;
};

ConstantBuffer<TerrainConstants> g_Constants : register(b0, space15);
StructuredBuffer<PatchData>      g_Patches   : register(t0, space15);

Texture2D<float>  g_Heightmap   : register(t1, space15);
Texture2D<float4> g_Splatmap    : register(t2, space15);

// Layer textures (up to 4 layers using splatmap RGBA)
Texture2D<float4> g_LayerAlbedo[4] : register(t3, space15);
Texture2D<float4> g_LayerNormal[4] : register(t7, space15);
Texture2D<float2> g_LayerARM[4]    : register(t11, space15); // Roughness + Metallic

SamplerState g_LinearWrap  : register(s0, space15);
SamplerState g_LinearClamp : register(s1, space15);

// ─── Vertex Shader ───────────────────────────────────────────────────────

struct VSInput {
    float2 gridPos   : POSITION;    // [0,1] grid position
    uint   instanceId : SV_InstanceID;
};

struct VSOutput {
    float4 clipPos   : SV_Position;
    float3 worldPos  : TEXCOORD0;
    float2 heightUV  : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
    float  morphFactor : TEXCOORD3;
};

float SampleHeight(float2 uv) {
    return g_Heightmap.SampleLevel(g_LinearClamp, uv, 0) * g_Constants.heightParams.x;
}

float3 ComputeNormal(float2 uv) {
    float texel = g_Constants.heightParams.y;
    float hL = SampleHeight(uv + float2(-texel, 0));
    float hR = SampleHeight(uv + float2( texel, 0));
    float hD = SampleHeight(uv + float2(0, -texel));
    float hU = SampleHeight(uv + float2(0,  texel));
    return normalize(float3(hL - hR, 2.0 * texel * g_Constants.worldOrigin.w, hD - hU));
}

float ComputeMorphFactor(float3 worldPos, uint lodLevel) {
    float dist = length(worldPos - g_Constants.cameraPos.xyz);
    float lodDist = g_Constants.lodParams.x * pow(2.0, float(lodLevel));
    float morphRange = g_Constants.heightParams.z;
    return saturate((dist - lodDist * (1.0 - morphRange)) / (lodDist * morphRange));
}

VSOutput VSMain(VSInput input) {
    VSOutput output;

    PatchData patch = g_Patches[input.instanceId];

    // World XZ position
    float patchWorldSize = g_Constants.worldOrigin.w / g_Constants.lodParams.y * patch.scale;
    float2 worldXZ = patch.worldOffset + input.gridPos * patchWorldSize;

    // Heightmap UV
    float halfWorld = g_Constants.worldOrigin.w * 0.5;
    float2 heightUV = (worldXZ + halfWorld) / g_Constants.worldOrigin.w;
    heightUV = clamp(heightUV, 0.0, 1.0);

    // Sample height
    float height = SampleHeight(heightUV);

    // LOD morphing: snap even vertices to odd grid for smooth LOD transition
    float morphFactor = ComputeMorphFactor(float3(worldXZ.x, height, worldXZ.y), patch.lodLevel);

    // Morph grid position toward coarser LOD
    float gridStep = 1.0 / g_Constants.lodParams.y;
    float2 morphedGrid = input.gridPos;
    float2 fracGrid = frac(input.gridPos / (gridStep * 2.0)) * 2.0;
    morphedGrid -= fracGrid * gridStep * morphFactor;

    // Recompute world position with morphed grid
    worldXZ = patch.worldOffset + morphedGrid * patchWorldSize;
    heightUV = (worldXZ + halfWorld) / g_Constants.worldOrigin.w;
    heightUV = clamp(heightUV, 0.0, 1.0);
    height = SampleHeight(heightUV);

    float3 worldPos = float3(worldXZ.x, height, worldXZ.y);

    output.clipPos = mul(g_Constants.viewProj, float4(worldPos, 1.0));
    output.worldPos = worldPos;
    output.heightUV = heightUV;
    output.worldNormal = ComputeNormal(heightUV);
    output.morphFactor = morphFactor;

    return output;
}

// ─── Fragment Shader ─────────────────────────────────────────────────────

struct PSOutput {
    float4 color : SV_Target0;
};

PSOutput PSMain(VSOutput input) {
    PSOutput output;

    float3 N = normalize(input.worldNormal);
    float3 V = normalize(g_Constants.cameraPos.xyz - input.worldPos);

    // Sample splatmap weights
    float4 weights = g_Splatmap.Sample(g_LinearClamp, input.heightUV);
    float totalWeight = weights.x + weights.y + weights.z + weights.w;
    if (totalWeight > 0.001) weights /= totalWeight;
    else weights = float4(1, 0, 0, 0);

    // Material tiling (world-space UV for detail textures)
    float2 detailUV = input.worldPos.xz * 0.1; // 10m tiling

    // Blend layers
    float3 albedo = float3(0, 0, 0);
    float roughness = 0;
    float metallic = 0;
    float3 blendedNormal = float3(0, 0, 0);

    [unroll]
    for (uint i = 0; i < 4; ++i) {
        float w = weights[i];
        if (w < 0.001) continue;

        float3 layerAlbedo = g_LayerAlbedo[i].Sample(g_LinearWrap, detailUV).rgb;
        float4 layerNormal = g_LayerNormal[i].Sample(g_LinearWrap, detailUV);
        float2 layerARM    = g_LayerARM[i].Sample(g_LinearWrap, detailUV).rg;

        albedo += layerAlbedo * w;
        roughness += layerARM.x * w;
        metallic += layerARM.y * w;
        blendedNormal += (layerNormal.xyz * 2.0 - 1.0) * w;
    }

    // Apply normal map in tangent space
    // For terrain, tangent is approximately along world X, bitangent along world Z
    float3 T = normalize(float3(1, 0, 0) - N * dot(float3(1, 0, 0), N));
    float3 B = cross(N, T);
    blendedNormal = normalize(blendedNormal);
    N = normalize(T * blendedNormal.x + B * blendedNormal.y + N * blendedNormal.z);

    // Lighting (sun)
    float3 L = -g_Constants.sunDirection.xyz;
    float NdotL = max(dot(N, L), 0.0);
    float3 H = normalize(V + L);
    float3 brdf = EvaluatePBR_BRDF(albedo, metallic, roughness, N, V, L, H);

    float3 color = brdf * g_Constants.sunColor.rgb * NdotL;

    // Ambient
    color += albedo * 0.05;

    // Distance fog (simple exponential)
    float dist = length(input.worldPos - g_Constants.cameraPos.xyz);
    float fogFactor = 1.0 - exp(-dist * 0.0003);
    float3 fogColor = float3(0.6, 0.7, 0.9);
    color = lerp(color, fogColor, saturate(fogFactor));

    output.color = float4(color, 1.0);
    return output;
}
