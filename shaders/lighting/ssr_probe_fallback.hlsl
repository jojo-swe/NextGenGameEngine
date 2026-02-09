// ─── Screen-Space Reflections with Probe Fallback ────────────────────────
// Combines Hi-Z ray-marched SSR with reflection probe fallback for
// areas where screen-space data is unavailable (off-screen, occluded).
//
// Architecture:
//   1. Hi-Z ray march in screen space for nearby reflections
//   2. If ray misses or exits screen → sample reflection probe cubemap
//   3. Blend based on confidence (ray hit vs probe), roughness, Fresnel
//   4. Temporal accumulation with reprojection for stability
//
// References:
//   - "Stochastic Screen-Space Reflections" (Stachowiak, SIGGRAPH 2015)
//   - "Hybrid Screen-Space Reflections" (Ubisoft, GDC 2017)

#include "../common/math.hlsl"

// ─── Inputs ──────────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor   : register(t0); // Current frame lit scene
Texture2D<float>  g_DepthBuffer  : register(t1); // Hi-Z mip chain
Texture2D<float4> g_GBuffer1     : register(t2); // Normal + roughness
Texture2D<float2> g_MotionVectors: register(t3);
Texture2D<float4> g_SSRHistory   : register(t4); // Previous frame SSR result
TextureCube<float4> g_ProbeArray[8] : register(t5); // Reflection probes

RWTexture2D<float4> g_Output : register(u0); // SSR result (RGB: color, A: confidence)

SamplerState g_PointClamp   : register(s0);
SamplerState g_LinearClamp  : register(s1);
SamplerState g_LinearWrap   : register(s2);

struct SSRProbeConstants {
    float4x4 view;
    float4x4 proj;
    float4x4 invViewProj;
    float4x4 prevViewProj;
    float3   cameraPos;
    float    maxRayDistance;     // World-space max distance (default 100)
    float2   resolution;
    float2   invResolution;
    uint     maxSteps;          // Hi-Z march steps (default 64)
    uint     maxBinarySteps;    // Refinement steps (default 8)
    float    thickness;         // Depth comparison thickness (default 0.1)
    float    stride;            // Initial step stride (default 1.0)
    float    jitter;            // Temporal jitter amount (default 1.0)
    float    temporalBlend;     // History blend factor (default 0.9)
    float    roughnessThreshold;// Max roughness for SSR (default 0.4)
    float    fresnelExponent;   // Fresnel strength (default 5.0)
    uint     probeCount;        // Active reflection probes
    float    probeBlendDistance; // Distance for probe fade (default 10.0)
    uint     frameIndex;
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<SSRProbeConstants> cb;

// ─── Probe Data ──────────────────────────────────────────────────────────

struct ReflectionProbe {
    float3 position;
    float  radius;
    float3 boxMin;      // AABB for box projection
    float  pad0;
    float3 boxMax;
    float  blendWeight;
};

StructuredBuffer<ReflectionProbe> g_Probes : register(t13);

// ─── Utility ─────────────────────────────────────────────────────────────

float3 ReconstructWorldPos(float2 uv, float depth) {
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;
    float4 worldPos = mul(cb.invViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}

float3 ReconstructViewPos(float2 uv, float depth) {
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;
    float4 viewPos = mul(cb.invViewProj, clipPos); // Simplified; in practice use invProj
    return viewPos.xyz / viewPos.w;
}

float Fresnel(float cosTheta, float f0, float exponent) {
    return f0 + (1.0 - f0) * pow(saturate(1.0 - cosTheta), exponent);
}

float InterleavedGradientNoise(float2 position, uint frame) {
    position += float(frame) * float2(5.588238, 5.588238);
    return frac(52.9829189 * frac(0.06711056 * position.x + 0.00583715 * position.y));
}

// ─── Hi-Z Ray March ──────────────────────────────────────────────────────

struct RayMarchResult {
    float2 hitUV;
    float  hitDepth;
    float  confidence;
    bool   hit;
};

RayMarchResult HiZRayMarch(float3 rayOrigin, float3 rayDir, float2 startUV) {
    RayMarchResult result;
    result.hit = false;
    result.confidence = 0.0;

    // Project ray into screen space
    float4 rayEndClip = mul(cb.proj, mul(cb.view, float4(rayOrigin + rayDir * cb.maxRayDistance, 1.0)));
    float2 rayEndUV = (rayEndClip.xy / rayEndClip.w) * float2(0.5, -0.5) + 0.5;

    float2 rayUV = rayEndUV - startUV;
    float rayLen = length(rayUV * cb.resolution);
    if (rayLen < 1.0) return result;

    // Jitter start position for temporal stability
    float jitter = InterleavedGradientNoise(startUV * cb.resolution, cb.frameIndex) * cb.jitter;

    float2 stepUV = rayUV / rayLen;
    float stepSize = cb.stride;

    float2 currentUV = startUV + stepUV * jitter;
    float currentDepthLinear = 0.0;

    // Linear ray march with Hi-Z acceleration
    [loop]
    for (uint i = 0; i < cb.maxSteps; ++i) {
        currentUV += stepUV * stepSize;

        // Check bounds
        if (any(currentUV < 0.0) || any(currentUV > 1.0)) break;

        // Sample depth at current mip level (Hi-Z acceleration)
        uint mipLevel = clamp(uint(log2(stepSize)), 0, 6);
        float sampledDepth = g_DepthBuffer.SampleLevel(g_PointClamp, currentUV, mipLevel);

        // Reconstruct positions for depth comparison
        float3 sampleWorldPos = ReconstructWorldPos(currentUV, sampledDepth);
        float3 rayWorldPos = rayOrigin + rayDir * (float(i) * stepSize * cb.maxRayDistance / rayLen);

        float depthDiff = length(sampleWorldPos - cb.cameraPos) - length(rayWorldPos - cb.cameraPos);

        if (depthDiff > 0.0 && depthDiff < cb.thickness) {
            // Hit — binary refinement
            float2 hitUV = currentUV;
            float2 prevUV = currentUV - stepUV * stepSize;

            for (uint j = 0; j < cb.maxBinarySteps; ++j) {
                float2 midUV = (hitUV + prevUV) * 0.5;
                float midDepth = g_DepthBuffer.SampleLevel(g_PointClamp, midUV, 0);
                float3 midWorld = ReconstructWorldPos(midUV, midDepth);
                float midDiff = length(midWorld - cb.cameraPos) - length(rayWorldPos - cb.cameraPos);

                if (midDiff > 0.0 && midDiff < cb.thickness) {
                    hitUV = midUV;
                } else {
                    prevUV = midUV;
                }
            }

            result.hitUV = hitUV;
            result.hitDepth = g_DepthBuffer.SampleLevel(g_PointClamp, hitUV, 0);
            result.hit = true;

            // Confidence based on distance from screen edges and depth similarity
            float2 edgeDist = min(hitUV, 1.0 - hitUV);
            float edgeFade = saturate(min(edgeDist.x, edgeDist.y) * 10.0);
            float depthFade = 1.0 - saturate(abs(depthDiff) / cb.thickness);
            result.confidence = edgeFade * depthFade;

            return result;
        }

        // Adaptive step size (speed up in empty space)
        if (depthDiff < -cb.thickness * 2.0) {
            stepSize *= 1.5;
        }
    }

    return result;
}

// ─── Reflection Probe Sampling ───────────────────────────────────────────

float3 SampleProbeWithBoxProjection(uint probeIdx, float3 reflectDir,
                                      float3 worldPos, float roughness) {
    ReflectionProbe probe = g_Probes[probeIdx];

    // Box projection correction
    float3 rbMax = (probe.boxMax - worldPos) / reflectDir;
    float3 rbMin = (probe.boxMin - worldPos) / reflectDir;
    float3 rbMaxMin = max(rbMax, rbMin);
    float dist = min(rbMaxMin.x, min(rbMaxMin.y, rbMaxMin.z));

    float3 intersect = worldPos + reflectDir * dist;
    float3 correctedDir = intersect - probe.position;

    // Roughness → mip level (assuming 8 mip levels)
    float mip = roughness * 7.0;

    return g_ProbeArray[probeIdx].SampleLevel(g_LinearWrap, correctedDir, mip).rgb;
}

float3 BlendProbes(float3 worldPos, float3 reflectDir, float roughness) {
    float3 result = float3(0, 0, 0);
    float totalWeight = 0.0;

    for (uint i = 0; i < cb.probeCount && i < 8; ++i) {
        ReflectionProbe probe = g_Probes[i];

        float dist = length(worldPos - probe.position);
        if (dist > probe.radius) continue;

        float weight = saturate(1.0 - dist / probe.radius);
        weight *= probe.blendWeight;

        float3 probeColor = SampleProbeWithBoxProjection(i, reflectDir, worldPos, roughness);
        result += probeColor * weight;
        totalWeight += weight;
    }

    if (totalWeight > 0.0) result /= totalWeight;
    return result;
}

// ─── Main SSR + Probe Fallback ───────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSSSRProbeFallback(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float depth = g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0);
    if (depth >= 1.0) {
        g_Output[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    float4 gb1 = g_GBuffer1.SampleLevel(g_PointClamp, uv, 0);
    float3 worldNormal = normalize(gb1.rgb * 2.0 - 1.0);
    float roughness = gb1.a;

    // Skip very rough surfaces (diffuse-like, no specular reflections)
    if (roughness > cb.roughnessThreshold) {
        g_Output[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    float3 worldPos = ReconstructWorldPos(uv, depth);
    float3 viewDir = normalize(worldPos - cb.cameraPos);
    float3 reflectDir = reflect(viewDir, worldNormal);

    // Fresnel
    float NdotV = saturate(dot(worldNormal, -viewDir));
    float fresnel = Fresnel(NdotV, 0.04, cb.fresnelExponent);

    // ── SSR Ray March ────────────────────────────────────────────────
    RayMarchResult ssrResult = HiZRayMarch(worldPos, reflectDir, uv);

    float3 ssrColor = float3(0, 0, 0);
    float ssrConfidence = 0.0;

    if (ssrResult.hit) {
        ssrColor = g_SceneColor.SampleLevel(g_LinearClamp, ssrResult.hitUV, 0).rgb;
        ssrConfidence = ssrResult.confidence;

        // Roughness-based fade
        ssrConfidence *= saturate(1.0 - roughness / cb.roughnessThreshold);
    }

    // ── Probe Fallback ───────────────────────────────────────────────
    float3 probeColor = BlendProbes(worldPos, reflectDir, roughness);

    // ── Blend SSR + Probe ────────────────────────────────────────────
    float3 finalReflection = lerp(probeColor, ssrColor, ssrConfidence);
    finalReflection *= fresnel;

    // ── Temporal Accumulation ────────────────────────────────────────
    float2 motion = g_MotionVectors.SampleLevel(g_PointClamp, uv, 0);
    float2 historyUV = uv - motion;

    float3 output = finalReflection;

    if (all(historyUV >= 0.0) && all(historyUV <= 1.0)) {
        float4 history = g_SSRHistory.SampleLevel(g_LinearClamp, historyUV, 0);

        // Neighborhood clamping
        float3 minC = finalReflection * 0.8;
        float3 maxC = finalReflection * 1.2 + 0.01;
        float3 clampedHistory = clamp(history.rgb, minC, maxC);

        output = lerp(finalReflection, clampedHistory, cb.temporalBlend);
    }

    float outputConfidence = max(ssrConfidence, 0.1); // Minimum from probes
    g_Output[DTid.xy] = float4(output, outputConfidence);
}
