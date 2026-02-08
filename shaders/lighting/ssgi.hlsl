// ─── Screen-Space Global Illumination (SSGI) ────────────────────────────
// Computes indirect diffuse lighting by tracing rays in screen space
// against the depth buffer. Uses importance sampling based on surface
// normals and a multi-bounce approximation.
//
// Algorithm:
//   1. For each pixel, generate N rays in the hemisphere around the normal
//   2. March each ray through the Hi-Z depth buffer
//   3. On hit, sample the irradiance at the hit point
//   4. Accumulate weighted by BRDF (Lambert diffuse)
//   5. Temporal accumulation to amortize cost across frames
//
// Based on techniques from:
//   - "SSGI in Battlefield V" (Hillaire, SIGGRAPH 2020)
//   - "Stochastic Screen-Space Reflections" (Stachowiak)

#include "../common/math.hlsl"

Texture2D<float4> g_GBuffer0    : register(t0); // RGB: Albedo, A: metallic
Texture2D<float4> g_GBuffer1    : register(t1); // RGB: World normal (encoded), A: roughness
Texture2D<float>  g_DepthBuffer : register(t2);
Texture2D<float4> g_PrevFrame   : register(t3); // Previous frame color for multi-bounce
Texture2D<float4> g_History     : register(t4); // Temporal history
Texture2D<float2> g_MotionVec   : register(t5); // Motion vectors for reprojection

RWTexture2D<float4> g_Output : register(u0);

SamplerState g_PointClamp  : register(s0);
SamplerState g_LinearClamp : register(s1);

struct SSGIConstants {
    float4x4 viewProj;
    float4x4 invViewProj;
    float4x4 prevViewProj;
    float4x4 view;
    float2   resolution;
    float2   invResolution;
    float    near;
    float    far;
    float    maxRayLength;      // Max trace distance in world units (default 20)
    float    thickness;         // Depth comparison thickness (default 0.3)
    uint     maxSteps;          // Hi-Z march steps (default 64)
    uint     raysPerPixel;      // Rays per pixel (default 1, amortized temporally)
    uint     frameIndex;        // For blue noise / Halton jitter
    float    indirectIntensity; // Brightness multiplier (default 1.0)
    float    temporalBlend;     // History blend factor (default 0.95)
    float    multiBounceScale;  // Multi-bounce approximation strength
    float    normalBias;        // Offset along normal to avoid self-intersection
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<SSGIConstants> cb;

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

float LinearizeDepth(float d) {
    return cb.near * cb.far / (cb.far - d * (cb.far - cb.near));
}

// Cosine-weighted hemisphere sampling
float3 CosineSampleHemisphere(float2 xi, float3 N) {
    float phi = 2.0 * PI * xi.x;
    float cosTheta = sqrt(1.0 - xi.y);
    float sinTheta = sqrt(xi.y);

    float3 H = float3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

    // Build tangent frame
    float3 up = abs(N.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 T = normalize(cross(up, N));
    float3 B = cross(N, T);

    return normalize(T * H.x + B * H.y + N * H.z);
}

// Blue noise / Halton sequence for temporal jitter
float2 Halton(uint index) {
    float2 result;

    // Base 2
    uint bits = index;
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1);
    bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2);
    bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4);
    bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8);
    result.x = float(bits) * 2.3283064365386963e-10;

    // Base 3
    float f = 1.0;
    float r = 0.0;
    uint i = index;
    while (i > 0) {
        f /= 3.0;
        r += f * float(i % 3);
        i /= 3;
    }
    result.y = r;

    return result;
}

// ─── Hi-Z Ray March ──────────────────────────────────────────────────────

struct TraceResult {
    bool  hit;
    float2 hitUV;
    float  hitDepth;
    float  confidence;
};

TraceResult HiZTrace(float3 rayOrigin, float3 rayDir) {
    TraceResult result;
    result.hit = false;
    result.confidence = 0.0;

    // Project ray start and end to screen space
    float4 startClip = mul(cb.viewProj, float4(rayOrigin, 1.0));
    float3 startNDC = startClip.xyz / startClip.w;
    float2 startUV = startNDC.xy * 0.5 + 0.5;
    startUV.y = 1.0 - startUV.y;

    float3 rayEnd = rayOrigin + rayDir * cb.maxRayLength;
    float4 endClip = mul(cb.viewProj, float4(rayEnd, 1.0));
    float3 endNDC = endClip.xyz / endClip.w;
    float2 endUV = endNDC.xy * 0.5 + 0.5;
    endUV.y = 1.0 - endUV.y;

    float2 deltaUV = endUV - startUV;
    float2 stepUV = deltaUV / float(cb.maxSteps);

    float2 currentUV = startUV;
    float stepZ = (endNDC.z - startNDC.z) / float(cb.maxSteps);
    float currentZ = startNDC.z;

    for (uint step = 0; step < cb.maxSteps; ++step) {
        currentUV += stepUV;
        currentZ += stepZ;

        // Bounds check
        if (any(currentUV < 0.0) || any(currentUV > 1.0)) break;

        float sceneDepth = g_DepthBuffer.SampleLevel(g_PointClamp, currentUV, 0);
        float linearScene = LinearizeDepth(sceneDepth);
        float linearRay = LinearizeDepth(currentZ);

        float depthDiff = linearRay - linearScene;

        if (depthDiff > 0.0 && depthDiff < cb.thickness) {
            result.hit = true;
            result.hitUV = currentUV;
            result.hitDepth = sceneDepth;

            // Confidence based on distance and depth agreement
            float distFade = 1.0 - float(step) / float(cb.maxSteps);
            float depthFade = 1.0 - saturate(depthDiff / cb.thickness);
            result.confidence = distFade * depthFade;
            break;
        }
    }

    return result;
}

// ─── Main SSGI Pass ──────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSTrace(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    // Read GBuffer
    float4 gbuffer0 = g_GBuffer0.SampleLevel(g_PointClamp, uv, 0);
    float4 gbuffer1 = g_GBuffer1.SampleLevel(g_PointClamp, uv, 0);
    float  depth    = g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0);

    // Skip sky
    if (depth >= 1.0) {
        g_Output[DTid.xy] = float4(0, 0, 0, 0);
        return;
    }

    float3 albedo    = gbuffer0.rgb;
    float  metallic  = gbuffer0.a;
    float3 worldNorm = DecodeNormal(gbuffer1.rgb);
    float  roughness = gbuffer1.a;

    float3 worldPos = ReconstructWorldPos(uv, depth);

    // Offset along normal to avoid self-intersection
    float3 biasedPos = worldPos + worldNorm * cb.normalBias;

    float3 totalIrradiance = float3(0, 0, 0);
    float  totalWeight = 0.0;

    for (uint ray = 0; ray < cb.raysPerPixel; ++ray) {
        // Jittered random per pixel per frame
        uint seed = DTid.x + DTid.y * uint(cb.resolution.x) + cb.frameIndex * 1337 + ray * 7;
        float2 xi = Halton(seed);

        // Generate ray direction in hemisphere
        float3 rayDir = CosineSampleHemisphere(xi, worldNorm);

        TraceResult trace = HiZTrace(biasedPos, rayDir);

        if (trace.hit) {
            // Sample irradiance at hit point
            float3 hitColor = g_PrevFrame.SampleLevel(g_LinearClamp, trace.hitUV, 0).rgb;

            // Lambert BRDF weight (cosine already in hemisphere sampling)
            float weight = trace.confidence;

            totalIrradiance += hitColor * weight;
            totalWeight += weight;
        }
    }

    float3 indirectLight = float3(0, 0, 0);
    if (totalWeight > 0.0) {
        indirectLight = totalIrradiance / totalWeight;
    }

    // Multi-bounce approximation (Jimenez 2016)
    // Approximate higher-order bounces using albedo
    float3 multiBounce = indirectLight * albedo * cb.multiBounceScale;
    indirectLight += multiBounce;

    // Apply intensity and albedo
    indirectLight *= albedo * (1.0 - metallic) * cb.indirectIntensity;

    g_Output[DTid.xy] = float4(indirectLight, 1.0);
}

// ─── Temporal Accumulation ───────────────────────────────────────────────

RWTexture2D<float4> g_TemporalOutput : register(u1);

[numthreads(8, 8, 1)]
void CSTemporal(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float4 currentGI = g_Output[DTid.xy];
    float2 motion = g_MotionVec.SampleLevel(g_PointClamp, uv, 0);
    float2 historyUV = uv - motion;

    // Bounds check for history
    bool validHistory = all(historyUV >= 0.0) && all(historyUV <= 1.0);

    float4 historyGI = float4(0, 0, 0, 0);
    if (validHistory) {
        historyGI = g_History.SampleLevel(g_LinearClamp, historyUV, 0);
    }

    // Neighborhood clamping to reject stale history
    float3 minColor = currentGI.rgb;
    float3 maxColor = currentGI.rgb;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            int2 coord = int2(DTid.xy) + int2(x, y);
            coord = clamp(coord, int2(0, 0), int2(cb.resolution) - 1);
            float3 neighborColor = g_Output[coord].rgb;
            minColor = min(minColor, neighborColor);
            maxColor = max(maxColor, neighborColor);
        }
    }

    // Clamp history to neighborhood bounds
    historyGI.rgb = clamp(historyGI.rgb, minColor, maxColor);

    // Blend
    float blend = validHistory ? cb.temporalBlend : 0.0;
    float3 result = lerp(currentGI.rgb, historyGI.rgb, blend);

    g_TemporalOutput[DTid.xy] = float4(result, 1.0);
}

// ─── Spatial Denoise (Edge-Aware Bilateral) ──────────────────────────────

Texture2D<float4> g_SSGIInput : register(t6);
RWTexture2D<float4> g_DenoiseOutput : register(u2);

[numthreads(8, 8, 1)]
void CSDenoise(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float4 centerColor = g_SSGIInput.SampleLevel(g_PointClamp, uv, 0);
    float  centerDepth = LinearizeDepth(g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0));
    float3 centerNorm  = DecodeNormal(g_GBuffer1.SampleLevel(g_PointClamp, uv, 0).rgb);

    float3 totalColor = float3(0, 0, 0);
    float  totalWeight = 0.0;

    const int RADIUS = 3;

    for (int y = -RADIUS; y <= RADIUS; ++y) {
        for (int x = -RADIUS; x <= RADIUS; ++x) {
            float2 sampleUV = uv + float2(x, y) * cb.invResolution;
            if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) continue;

            float3 sampleColor = g_SSGIInput.SampleLevel(g_PointClamp, sampleUV, 0).rgb;
            float  sampleDepth = LinearizeDepth(g_DepthBuffer.SampleLevel(g_PointClamp, sampleUV, 0));
            float3 sampleNorm  = DecodeNormal(g_GBuffer1.SampleLevel(g_PointClamp, sampleUV, 0).rgb);

            // Spatial weight (Gaussian)
            float spatialDist = length(float2(x, y));
            float spatialW = exp(-spatialDist * spatialDist / 4.0);

            // Depth weight
            float depthDiff = abs(centerDepth - sampleDepth) / max(centerDepth, 0.001);
            float depthW = exp(-depthDiff * depthDiff * 100.0);

            // Normal weight
            float normalW = pow(max(dot(centerNorm, sampleNorm), 0.0), 16.0);

            float weight = spatialW * depthW * normalW;

            totalColor += sampleColor * weight;
            totalWeight += weight;
        }
    }

    float3 result = totalWeight > 0.0 ? totalColor / totalWeight : centerColor.rgb;
    g_DenoiseOutput[DTid.xy] = float4(result, 1.0);
}
