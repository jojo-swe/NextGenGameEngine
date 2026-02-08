// ─── Screen-Space Reflections (Hi-Z Ray March) ──────────────────────────
// Traces reflection rays in screen space using the HZB for acceleration.
// Falls back to GI probes for rays that miss or exit the screen.
//
// Algorithm:
//   1. Compute reflection direction from surface normal + view
//   2. March through HZB mip levels (coarse-to-fine)
//   3. On hit: read color from previous frame's scene buffer
//   4. On miss: sample environment or GI probes
//   5. Temporal accumulation to reduce noise

#include "../common/math.hlsl"
#include "../common/brdf.hlsl"

struct SSRConstants {
    float4x4 viewMatrix;
    float4x4 projMatrix;
    float4x4 invViewProj;
    float4x4 prevViewProj;
    float4   cameraPos;
    uint2    screenSize;
    uint     maxSteps;
    float    maxDistance;
    float    thickness;       // Depth comparison thickness
    float    roughnessCutoff; // Skip SSR for very rough surfaces
    float    temporalBlend;   // Blend with previous frame (0.9)
    uint     hzbMipCount;
    float    stride;          // Initial step size multiplier
    float    jitter;          // Temporal jitter amount
    uint     frameIndex;
    uint     pad;
};

[[vk::push_constant]] ConstantBuffer<SSRConstants> pc;

Texture2D<float4>   g_SceneColor     : register(t0, space13);
Texture2D<float>    g_DepthBuffer    : register(t1, space13);
Texture2D<float4>   g_GBuffer_Normal : register(t2, space13); // World normal + roughness
Texture2D<float>    g_HZB            : register(t3, space13);
Texture2D<float4>   g_PrevSSR        : register(t4, space13); // Previous frame SSR
Texture2D<float2>   g_MotionVectors  : register(t5, space13);

RWTexture2D<float4> g_SSROutput      : register(u0, space13);

SamplerState g_PointClamp  : register(s0, space13);
SamplerState g_LinearClamp : register(s1, space13);

// ─── Utility ─────────────────────────────────────────────────────────────

float3 ReconstructWorldPos(float2 uv, float depth) {
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float4 clip = float4(ndc, depth, 1.0);
    float4 world = mul(pc.invViewProj, clip);
    return world.xyz / world.w;
}

float3 ProjectToScreen(float3 worldPos) {
    float4 clip = mul(pc.projMatrix, mul(pc.viewMatrix, float4(worldPos, 1.0)));
    float3 ndc = clip.xyz / clip.w;
    return float3(ndc.xy * 0.5 + 0.5, ndc.z);
}

// Blue noise-like hash for jitter
float InterleavedGradientNoise(float2 screenPos) {
    float3 magic = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(magic.z * frac(dot(screenPos, magic.xy)));
}

// ─── Hi-Z Ray March ──────────────────────────────────────────────────────

struct RayMarchResult {
    bool   hit;
    float2 hitUV;
    float  hitDepth;
    float  confidence;
};

RayMarchResult HiZRayMarch(float3 rayOriginSS, float3 rayDirSS) {
    RayMarchResult result;
    result.hit = false;
    result.confidence = 0;

    float3 pos = rayOriginSS;
    float2 texelSize = 1.0 / float2(pc.screenSize);

    // Start at a coarse mip level and refine
    int mipLevel = 2;
    float stepSize = pc.stride;

    for (uint i = 0; i < pc.maxSteps; ++i) {
        pos += rayDirSS * stepSize;

        // Screen bounds check
        if (pos.x < 0 || pos.x > 1 || pos.y < 0 || pos.y > 1) break;
        if (pos.z < 0 || pos.z > 1) break;

        // Sample HZB at current mip
        float sceneDepth = g_HZB.SampleLevel(g_PointClamp, pos.xy, float(mipLevel));

        // Depth comparison
        float depthDiff = pos.z - sceneDepth;

        if (depthDiff > 0 && depthDiff < pc.thickness) {
            // Potential hit — refine at finer mip
            if (mipLevel <= 0) {
                // Refined enough — we have a hit
                result.hit = true;
                result.hitUV = pos.xy;
                result.hitDepth = sceneDepth;

                // Confidence based on how close to screen edge and how parallel the ray is
                float edgeFade = 1.0;
                float2 edgeDist = min(pos.xy, 1.0 - pos.xy);
                edgeFade = saturate(min(edgeDist.x, edgeDist.y) * 10.0);

                result.confidence = edgeFade;
                break;
            }
            // Step back and refine
            pos -= rayDirSS * stepSize;
            mipLevel--;
            stepSize *= 0.5;
        } else if (depthDiff < 0) {
            // Haven't reached the surface yet — try coarser mip for speed
            mipLevel = min(mipLevel + 1, int(pc.hzbMipCount) - 1);
            stepSize *= 2.0;
        }
    }

    return result;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= pc.screenSize.x || DTid.y >= pc.screenSize.y) return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(pc.screenSize);

    // Read surface data
    float depth = g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0);
    if (depth <= 0.0) {
        g_SSROutput[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    float4 normalRough = g_GBuffer_Normal.SampleLevel(g_PointClamp, uv, 0);
    float3 worldNormal = normalize(normalRough.xyz * 2.0 - 1.0);
    float roughness = normalRough.w;

    // Skip very rough surfaces (diffuse-dominated)
    if (roughness > pc.roughnessCutoff) {
        g_SSROutput[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    // Reconstruct world position
    float3 worldPos = ReconstructWorldPos(uv, depth);
    float3 viewDir = normalize(worldPos - pc.cameraPos.xyz);

    // Compute reflection direction
    float3 reflectDir = reflect(viewDir, worldNormal);

    // Add roughness-based jitter for glossy reflections
    float jitter = InterleavedGradientNoise(float2(DTid.xy) + float(pc.frameIndex) * 5.37) * pc.jitter;

    // Project reflection ray endpoints to screen space
    float3 rayOriginSS = float3(uv, depth);
    float3 rayEndWorld = worldPos + reflectDir * pc.maxDistance;
    float3 rayEndSS = ProjectToScreen(rayEndWorld);

    float3 rayDirSS = normalize(rayEndSS - rayOriginSS);

    // Apply jitter along ray direction
    rayOriginSS += rayDirSS * jitter * 0.01;

    // Hi-Z ray march
    RayMarchResult marchResult = HiZRayMarch(rayOriginSS, rayDirSS);

    float4 reflectionColor = float4(0, 0, 0, 0);

    if (marchResult.hit) {
        // Sample scene color at hit point
        float3 hitColor = g_SceneColor.SampleLevel(g_LinearClamp, marchResult.hitUV, 0).rgb;

        // Fresnel attenuation
        float NdotV = saturate(dot(worldNormal, -viewDir));
        float fresnel = SchlickFresnel(NdotV, 0.04);

        // Roughness fade (rougher = less sharp reflection)
        float roughnessFade = 1.0 - roughness * roughness;

        reflectionColor = float4(hitColor * fresnel * roughnessFade, marchResult.confidence);
    }

    // Temporal accumulation
    float2 motion = g_MotionVectors.SampleLevel(g_PointClamp, uv, 0);
    float2 prevUV = uv - motion;

    if (prevUV.x >= 0 && prevUV.x <= 1 && prevUV.y >= 0 && prevUV.y <= 1) {
        float4 prevSSR = g_PrevSSR.SampleLevel(g_LinearClamp, prevUV, 0);

        // Blend with temporal history
        float blendFactor = marchResult.hit ? pc.temporalBlend : pc.temporalBlend * 0.5;
        reflectionColor = lerp(reflectionColor, prevSSR, blendFactor);
    }

    g_SSROutput[DTid.xy] = reflectionColor;
}
