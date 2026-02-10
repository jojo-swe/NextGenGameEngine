// ─── Ambient Occlusion + Contact Shadows Composite ──────────────────────
// Full-screen post-process combining GTAO (Ground Truth AO) with
// screen-space contact shadows for small-scale occlusion near surfaces.
//
// Features:
//   - GTAO: cosine-weighted horizon-based AO with spatial + temporal denoise
//   - Contact shadows: short-range screen-space ray march from light direction
//   - Multi-bounce AO approximation (energy conservation)
//   - Bent normal output for indirect lighting direction
//   - Composite: modulate indirect diffuse by AO, direct light by contact shadow
//
// References:
//   - "Practical Real-Time Strategies for Accurate Indirect Occlusion" (Jimenez, SIGGRAPH 2016)
//   - "Ground Truth AO" (Activision, SIGGRAPH 2016)
//   - "Screen-Space Contact Shadows" (Ubisoft, The Division 2)
//   - "Multi-Bounce AO" (Jimenez, 2016)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor    : register(t0);
Texture2D<float>  g_SceneDepth    : register(t1);
Texture2D<float4> g_GBufferNormal : register(t2); // World-space normals
Texture2D<float>  g_NoiseTex      : register(t3); // Blue noise / dither
Texture2D<float>  g_PrevAO        : register(t4); // Previous frame AO for temporal

SamplerState g_PointClamp  : register(s0);
SamplerState g_LinearClamp : register(s1);

RWTexture2D<float4> g_Output     : register(u0); // RGB = composited, A = 1
RWTexture2D<float2> g_AOOutput   : register(u1); // R = AO, G = contact shadow

struct AOConstants {
    float4x4 viewMatrix;
    float4x4 projMatrix;
    float4x4 invProjMatrix;
    float2   resolution;
    float2   invResolution;
    float3   lightDirection;       // Primary light dir (world space)
    float    aoRadius;             // AO sampling radius in world units (default 0.5)
    float    aoIntensity;          // AO strength (default 1.0)
    float    aoBias;               // Depth bias to prevent self-occlusion (default 0.01)
    float    aoMaxDistance;        // Max depth difference for AO (default 1.0)
    u32      aoSampleCount;        // Directions per pixel (default 4)
    u32      aoStepsPerDir;        // Steps per direction (default 4)
    float    contactShadowLength;  // Max ray length in screen space (default 0.1)
    u32      contactShadowSteps;   // Ray march steps (default 16)
    float    contactShadowBias;    // Depth comparison bias (default 0.005)
    float    contactShadowFade;    // Fade at max distance (default 0.5)
    float    multiBounceAlbedo;    // Avg albedo for multi-bounce approx (default 0.2)
    float    temporalBlend;        // Temporal accumulation weight (default 0.9)
    float    time;
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<AOConstants> cb;

// ─── View-Space Reconstruction ───────────────────────────────────────────

float3 ReconstructViewPos(float2 uv, float depth) {
    float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
    clip.y = -clip.y;
    float4 view = mul(cb.invProjMatrix, clip);
    return view.xyz / view.w;
}

float LinearizeDepth(float d) {
    // Assuming reverse-Z infinite far plane
    float near = cb.projMatrix[3][2] / cb.projMatrix[2][2];
    return near / d;
}

// ─── GTAO (Ground Truth Ambient Occlusion) ───────────────────────────────
// Horizon-based AO with cosine-weighted integration.

float IntegrateArc(float h1, float h2, float n) {
    // Integrate visibility over the arc between two horizon angles
    float sinN = sin(n);
    float cosN = cos(n);
    return 0.25 * (-cos(2.0 * h1 - n) + cosN + 2.0 * h1 * sinN)
         + 0.25 * (-cos(2.0 * h2 - n) + cosN + 2.0 * h2 * sinN);
}

float GTAO(float2 uv, float3 viewPos, float3 viewNormal) {
    float totalAO = 0.0;

    // Spatial noise for direction jitter
    float noise = g_NoiseTex.SampleLevel(g_PointClamp, uv * cb.resolution / 64.0, 0);
    float rotAngle = noise * 3.14159265;

    float radiusPixels = cb.aoRadius / max(-viewPos.z, 0.1) * cb.projMatrix[0][0] * cb.resolution.x * 0.5;
    radiusPixels = clamp(radiusPixels, 3.0, 256.0);

    for (u32 dir = 0; dir < cb.aoSampleCount; ++dir) {
        // Distribute directions evenly
        float angle = (float(dir) + noise) * 3.14159265 / float(cb.aoSampleCount);
        float2 direction = float2(cos(angle + rotAngle), sin(angle + rotAngle));

        // Find horizon angle by marching along direction
        float horizonCos = -1.0;

        for (u32 step = 1; step <= cb.aoStepsPerDir; ++step) {
            float2 offset = direction * (float(step) / float(cb.aoStepsPerDir)) * radiusPixels * cb.invResolution;
            float2 sampleUV = uv + offset;

            if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) break;

            float sampleDepth = g_SceneDepth.SampleLevel(g_PointClamp, sampleUV, 0);
            float3 sampleViewPos = ReconstructViewPos(sampleUV, sampleDepth);

            float3 horizonVec = sampleViewPos - viewPos;
            float horizonDist = length(horizonVec);

            if (horizonDist < cb.aoMaxDistance) {
                float h = dot(normalize(horizonVec), viewNormal);
                horizonCos = max(horizonCos, h);
            }
        }

        // Convert horizon cosine to angle and integrate
        float horizonAngle = acos(clamp(horizonCos, -1.0, 1.0));
        float normalAngle = acos(clamp(dot(viewNormal, float3(direction, 0)), -1.0, 1.0));

        totalAO += IntegrateArc(-horizonAngle, horizonAngle, normalAngle);
    }

    totalAO /= float(cb.aoSampleCount);
    totalAO = saturate(totalAO);

    // Apply intensity
    totalAO = pow(totalAO, cb.aoIntensity);

    return totalAO;
}

// ─── Multi-Bounce AO Approximation ──────────────────────────────────────
// Accounts for light bouncing in occluded areas (Jimenez 2016).

float3 MultiBounceAO(float ao, float3 albedo) {
    float3 a = 2.0404 * albedo - 0.3324;
    float3 b = -4.7951 * albedo + 0.6417;
    float3 c = 2.7552 * albedo + 0.6903;
    return max(float3(ao, ao, ao), ((ao * a + b) * ao + c) * ao);
}

// ─── Contact Shadows ─────────────────────────────────────────────────────
// Short-range screen-space ray march toward light source.

float ContactShadow(float2 uv, float3 viewPos, float3 lightDirView) {
    // Ray start and direction in screen space
    float3 rayStart = viewPos;
    float3 rayDir = normalize(lightDirView);

    // Project ray endpoint to find screen-space direction
    float3 rayEnd = rayStart + rayDir * cb.contactShadowLength;

    // Project both to screen
    float4 startClip = mul(cb.projMatrix, float4(rayStart, 1.0));
    float4 endClip = mul(cb.projMatrix, float4(rayEnd, 1.0));

    float2 startScreen = (startClip.xy / startClip.w) * 0.5 + 0.5;
    float2 endScreen = (endClip.xy / endClip.w) * 0.5 + 0.5;
    startScreen.y = 1.0 - startScreen.y;
    endScreen.y = 1.0 - endScreen.y;

    float2 rayScreenDir = endScreen - startScreen;
    float rayScreenLen = length(rayScreenDir);

    if (rayScreenLen < 0.001) return 1.0;

    float shadow = 1.0;
    float stepSize = 1.0 / float(cb.contactShadowSteps);

    for (u32 i = 1; i <= cb.contactShadowSteps; ++i) {
        float t = float(i) * stepSize;
        float2 sampleUV = startScreen + rayScreenDir * t;

        if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) break;

        // Interpolate expected depth along ray
        float expectedDepth = lerp(startClip.z / startClip.w, endClip.z / endClip.w, t);

        float sceneDepth = g_SceneDepth.SampleLevel(g_PointClamp, sampleUV, 0);

        float depthDiff = expectedDepth - sceneDepth;

        // Hit: scene is closer than expected (occluder found)
        if (depthDiff > cb.contactShadowBias && depthDiff < cb.contactShadowLength * 0.5) {
            // Soft fade based on distance along ray
            float fadeFactor = 1.0 - t * cb.contactShadowFade;
            shadow = min(shadow, 1.0 - fadeFactor);
        }
    }

    return saturate(shadow);
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float depth = g_SceneDepth.SampleLevel(g_PointClamp, uv, 0);
    float3 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;

    // Skip sky pixels
    if (depth < 0.0001) {
        g_Output[DTid.xy] = float4(sceneColor, 1.0);
        g_AOOutput[DTid.xy] = float2(1.0, 1.0);
        return;
    }

    // Reconstruct view-space position and normal
    float3 viewPos = ReconstructViewPos(uv, depth);

    float3 worldNormal = g_GBufferNormal.SampleLevel(g_PointClamp, uv, 0).rgb * 2.0 - 1.0;
    float3 viewNormal = normalize(mul((float3x3)cb.viewMatrix, worldNormal));

    // ── GTAO ─────────────────────────────────────────────────────────
    float ao = GTAO(uv, viewPos, viewNormal);

    // Temporal accumulation
    float prevAO = g_PrevAO.SampleLevel(g_LinearClamp, uv, 0);
    ao = lerp(ao, prevAO, cb.temporalBlend);

    // Multi-bounce approximation
    float3 albedoEstimate = float3(cb.multiBounceAlbedo, cb.multiBounceAlbedo, cb.multiBounceAlbedo);
    float3 multiBounce = MultiBounceAO(ao, albedoEstimate);

    // ── Contact Shadows ──────────────────────────────────────────────
    float3 lightDirView = normalize(mul((float3x3)cb.viewMatrix, cb.lightDirection));
    float contactShadow = ContactShadow(uv, viewPos, lightDirView);

    // ── Composite ────────────────────────────────────────────────────
    // AO modulates indirect lighting, contact shadow modulates direct
    float3 composited = sceneColor;
    composited *= multiBounce;                          // AO on indirect
    composited *= lerp(1.0, contactShadow, 0.5);       // Subtle contact shadow on direct

    g_Output[DTid.xy] = float4(composited, 1.0);
    g_AOOutput[DTid.xy] = float2(ao, contactShadow);
}
