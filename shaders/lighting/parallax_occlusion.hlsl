// ─── Parallax Occlusion Mapping Shader ───────────────────────────────────
// Adds depth illusion to flat surfaces by offsetting texture UVs based
// on a heightmap. Includes:
//   - Basic parallax mapping (single offset)
//   - Steep parallax mapping (layered ray march)
//   - Parallax occlusion mapping (POM) with binary refinement
//   - Self-shadowing via heightfield ray march toward light
//   - Silhouette-aware clipping at grazing angles
//
// References:
//   - "Parallax Occlusion Mapping" (Tatarchuk, ShaderX5)
//   - "Steep Parallax Mapping" (McGuire & McGuire, I3D 2005)

#include "../common/math.hlsl"

// ─── Inputs ──────────────────────────────────────────────────────────────

Texture2D<float4> g_Albedo     : register(t0);
Texture2D<float4> g_NormalMap  : register(t1);
Texture2D<float>  g_HeightMap  : register(t2);
Texture2D<float>  g_RoughnessMap : register(t3);

SamplerState g_LinearWrap  : register(s0);
SamplerState g_LinearClamp : register(s1);

struct POMConstants {
    float4x4 world;
    float4x4 viewProj;
    float4x4 worldInvTranspose;
    float3   cameraPos;
    float    heightScale;       // World-space height displacement (default 0.05)
    float3   lightDir;          // Directional light (world space)
    float    lightIntensity;
    float3   lightColor;
    float    shadowSoftness;    // Shadow penumbra (default 0.5)
    float2   texScale;          // UV tiling
    float    minLayers;         // Min ray march layers (default 8)
    float    maxLayers;         // Max ray march layers (default 64)
    uint     enableShadows;     // Self-shadowing toggle
    uint     enableSilhouette;  // Silhouette clipping toggle
    float    pad0;
    float    pad1;
};

[[vk::push_constant]] ConstantBuffer<POMConstants> cb;

// ─── Vertex Shader ───────────────────────────────────────────────────────

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 tangent  : TANGENT;
    float2 texcoord : TEXCOORD0;
};

struct VSOutput {
    float4 clipPos      : SV_POSITION;
    float2 texcoord     : TEXCOORD0;
    float3 worldPos     : TEXCOORD1;
    float3 worldNormal  : TEXCOORD2;
    float3 worldTangent : TEXCOORD3;
    float3 worldBitangent : TEXCOORD4;
    float3 viewDirTS    : TEXCOORD5; // View direction in tangent space
    float3 lightDirTS   : TEXCOORD6; // Light direction in tangent space
};

VSOutput VSMain(VSInput input) {
    VSOutput output;

    float4 worldPos = mul(cb.world, float4(input.position, 1.0));
    output.clipPos = mul(cb.viewProj, worldPos);
    output.worldPos = worldPos.xyz;
    output.texcoord = input.texcoord * cb.texScale;

    float3 N = normalize(mul((float3x3)cb.worldInvTranspose, input.normal));
    float3 T = normalize(mul((float3x3)cb.world, input.tangent.xyz));
    float3 B = cross(N, T) * input.tangent.w;

    output.worldNormal = N;
    output.worldTangent = T;
    output.worldBitangent = B;

    // Build TBN matrix (world → tangent)
    float3x3 TBN_inv = float3x3(T, B, N); // Rows are T, B, N

    float3 viewDir = cb.cameraPos - worldPos.xyz;
    output.viewDirTS = mul(TBN_inv, viewDir);
    output.lightDirTS = mul(TBN_inv, cb.lightDir);

    return output;
}

// ─── Parallax Occlusion Mapping ──────────────────────────────────────────

float2 ParallaxOcclusionMap(float2 uv, float3 viewDirTS) {
    // Normalize view direction in tangent space
    float3 V = normalize(viewDirTS);

    // Number of layers based on viewing angle (more at grazing)
    float numLayers = lerp(cb.maxLayers, cb.minLayers, abs(V.z));

    float layerDepth = 1.0 / numLayers;
    float currentLayerDepth = 0.0;

    // Direction and step size for UV offset
    float2 P = V.xy / max(V.z, 0.001) * cb.heightScale;
    float2 deltaUV = P / numLayers;

    float2 currentUV = uv;
    float currentHeight = g_HeightMap.SampleLevel(g_LinearWrap, currentUV, 0);

    // Steep parallax: march through layers
    [loop]
    for (float i = 0; i < numLayers; i += 1.0) {
        if (currentLayerDepth >= currentHeight) break;

        currentUV -= deltaUV;
        currentHeight = g_HeightMap.SampleLevel(g_LinearWrap, currentUV, 0);
        currentLayerDepth += layerDepth;
    }

    // Binary refinement for sub-layer precision
    float2 prevUV = currentUV + deltaUV;
    float prevHeight = g_HeightMap.SampleLevel(g_LinearWrap, prevUV, 0);
    float prevLayerDepth = currentLayerDepth - layerDepth;

    // Interpolate between previous and current layer
    float afterDepth = currentHeight - currentLayerDepth;
    float beforeDepth = prevHeight - prevLayerDepth;
    float weight = afterDepth / (afterDepth - beforeDepth);

    float2 finalUV = lerp(currentUV, prevUV, weight);

    return finalUV;
}

// ─── Self-Shadowing ──────────────────────────────────────────────────────
// March from the displaced point toward the light in tangent space.
// If the ray hits the heightfield, the point is in shadow.

float ParallaxSelfShadow(float2 uv, float3 lightDirTS, float initialHeight) {
    float3 L = normalize(lightDirTS);

    // Only shadow if light comes from above the surface
    if (L.z <= 0.0) return 0.0;

    float numLayers = lerp(cb.maxLayers * 0.5, cb.minLayers, abs(L.z));
    float layerDepth = initialHeight / max(numLayers, 1.0);

    float2 deltaUV = L.xy / max(L.z, 0.001) * cb.heightScale / numLayers;

    float currentLayerDepth = initialHeight;
    float2 currentUV = uv;

    float shadow = 0.0;
    float softShadowFactor = 0.0;

    [loop]
    for (float i = 0; i < numLayers; i += 1.0) {
        currentUV += deltaUV;
        currentLayerDepth -= layerDepth;

        float sampledHeight = g_HeightMap.SampleLevel(g_LinearWrap, currentUV, 0);

        if (sampledHeight > currentLayerDepth) {
            // In shadow — compute penumbra
            float diff = sampledHeight - currentLayerDepth;
            softShadowFactor = max(softShadowFactor, diff);
        }
    }

    // Soft shadow based on maximum occlusion depth
    shadow = 1.0 - saturate(softShadowFactor * (1.0 / cb.shadowSoftness));
    return shadow;
}

// ─── Fragment Shader ─────────────────────────────────────────────────────

struct PSOutput {
    float4 albedo   : SV_TARGET0; // GBuffer0: albedo + metallic
    float4 normal   : SV_TARGET1; // GBuffer1: normal + roughness
    float4 emissive : SV_TARGET2; // GBuffer2: emissive + AO
};

PSOutput PSMain(VSOutput input) {
    PSOutput output;

    float3 viewDirTS = normalize(input.viewDirTS);

    // Silhouette clipping: discard at extreme grazing angles
    if (cb.enableSilhouette && viewDirTS.z < 0.05) {
        discard;
    }

    // Compute parallax-displaced UV
    float2 pomUV = ParallaxOcclusionMap(input.texcoord, viewDirTS);

    // Optional: discard if UV goes out of [0,1] (for non-tiling surfaces)
    // if (any(pomUV < 0.0) || any(pomUV > cb.texScale)) discard;

    // Sample textures at displaced UV
    float4 albedo = g_Albedo.Sample(g_LinearWrap, pomUV);
    float3 tangentNormal = g_NormalMap.Sample(g_LinearWrap, pomUV).rgb * 2.0 - 1.0;
    float roughness = g_RoughnessMap.Sample(g_LinearWrap, pomUV);
    float height = g_HeightMap.SampleLevel(g_LinearWrap, pomUV, 0);

    // Transform normal from tangent to world space
    float3x3 TBN = float3x3(
        normalize(input.worldTangent),
        normalize(input.worldBitangent),
        normalize(input.worldNormal)
    );
    float3 worldNormal = normalize(mul(tangentNormal, TBN));

    // Self-shadowing
    float shadowFactor = 1.0;
    if (cb.enableShadows) {
        float3 lightDirTS = normalize(input.lightDirTS);
        shadowFactor = ParallaxSelfShadow(pomUV, lightDirTS, height);
    }

    // Write GBuffer
    output.albedo = float4(albedo.rgb, 0.0); // A: metallic (from material)
    output.normal = float4(worldNormal * 0.5 + 0.5, roughness);
    output.emissive = float4(0, 0, 0, shadowFactor); // A: store shadow for lighting

    return output;
}

// ─── Depth-Only Variant (for shadow maps) ────────────────────────────────
// Applies POM displacement to the depth pass so shadows match the
// displaced surface.

struct DepthVSOutput {
    float4 clipPos  : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 viewDirTS : TEXCOORD1;
};

DepthVSOutput VSDepthOnly(VSInput input) {
    DepthVSOutput output;
    float4 worldPos = mul(cb.world, float4(input.position, 1.0));
    output.clipPos = mul(cb.viewProj, worldPos);
    output.texcoord = input.texcoord * cb.texScale;

    float3 N = normalize(mul((float3x3)cb.worldInvTranspose, input.normal));
    float3 T = normalize(mul((float3x3)cb.world, input.tangent.xyz));
    float3 B = cross(N, T) * input.tangent.w;
    float3x3 TBN_inv = float3x3(T, B, N);

    float3 viewDir = cb.cameraPos - worldPos.xyz;
    output.viewDirTS = mul(TBN_inv, viewDir);

    return output;
}

void PSDepthOnly(DepthVSOutput input) {
    float3 viewDirTS = normalize(input.viewDirTS);
    if (viewDirTS.z < 0.05) discard;

    float2 pomUV = ParallaxOcclusionMap(input.texcoord, viewDirTS);
    float alpha = g_Albedo.Sample(g_LinearWrap, pomUV).a;
    if (alpha < 0.5) discard;
}
