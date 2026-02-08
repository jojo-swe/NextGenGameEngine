// ─── Volumetric Clouds Shader ─────────────────────────────────────────────
// Ray-marched volumetric clouds using layered 3D noise (Worley + Perlin).
// Based on the technique from "The Real-time Volumetric Cloudscapes of
// Horizon: Zero Dawn" (Schneider & Vos, SIGGRAPH 2015).
//
// Features:
//   - 3D noise-based cloud density (base shape + detail erosion)
//   - Height-based density gradient (weather map driven)
//   - Beer-Lambert light extinction + powder sugar effect
//   - Multi-octave light marching toward sun
//   - Temporal reprojection for performance (render at quarter res)
//   - Ambient + direct lighting with silver lining

#include "../common/math.hlsl"

// ─── Constants ───────────────────────────────────────────────────────────

struct CloudConstants {
    float4x4 invViewProj;
    float4   cameraPos;
    float4   sunDirection;
    float4   sunColor;
    float4   ambientColor;

    float    cloudLayerBottom;     // e.g., 1500 m
    float    cloudLayerTop;        // e.g., 4000 m
    float    cloudCoverage;        // 0-1
    float    cloudDensity;         // Multiplier

    float    windSpeed;
    float2   windDirection;
    float    time;

    float    detailScale;          // Detail noise UV scale
    float    erosionStrength;      // How much detail erodes base shape
    float    silverLiningIntensity;
    float    silverLiningSpread;

    float    lightStepSize;
    uint     lightSampleCount;     // Samples toward sun (e.g., 6)
    uint     primarySampleCount;   // Samples along view ray (e.g., 64)
    float    maxRayDistance;

    float2   screenSize;
    float2   pad;
};

[[vk::push_constant]] ConstantBuffer<CloudConstants> pc;

// ─── Textures ────────────────────────────────────────────────────────────

Texture3D<float4>   g_ShapeNoise   : register(t0, space40); // 128³ Worley-Perlin
Texture3D<float4>   g_DetailNoise  : register(t1, space40); // 32³ high-freq Worley
Texture2D<float4>   g_WeatherMap   : register(t2, space40); // R=coverage, G=type, B=precipitation
Texture2D<float>    g_DepthBuffer  : register(t3, space40);
Texture2D<float4>   g_PrevClouds   : register(t4, space40); // Previous frame for temporal reprojection

SamplerState g_LinearWrap  : register(s0, space40);
SamplerState g_LinearClamp : register(s1, space40);

RWTexture2D<float4> g_Output       : register(u0, space40); // RGB = cloud color, A = transmittance

// ─── Utility ─────────────────────────────────────────────────────────────

float Remap(float value, float oldMin, float oldMax, float newMin, float newMax) {
    return newMin + (value - oldMin) / (oldMax - oldMin) * (newMax - newMin);
}

float HeightFraction(float3 pos) {
    return saturate((pos.y - pc.cloudLayerBottom) / (pc.cloudLayerTop - pc.cloudLayerBottom));
}

// Height-based density gradient (cumulus-like profile)
float HeightGradient(float heightFrac, float cloudType) {
    // Cloud type 0 = stratus (flat), 1 = cumulus (puffy)
    float a = Remap(heightFrac, 0.0, lerp(0.1, 0.3, cloudType), 0.0, 1.0);
    float b = Remap(heightFrac, lerp(0.5, 0.7, cloudType), 1.0, 1.0, 0.0);
    return saturate(a * b);
}

// ─── Cloud Density Sampling ──────────────────────────────────────────────

float SampleCloudDensity(float3 pos, bool cheapSample) {
    float heightFrac = HeightFraction(pos);
    if (heightFrac < 0.0 || heightFrac > 1.0) return 0.0;

    // Wind animation
    float3 windOffset = float3(pc.windDirection.x, 0, pc.windDirection.y) * pc.windSpeed * pc.time;
    float3 samplePos = pos + windOffset;

    // Weather map (xz plane, tiled)
    float2 weatherUV = samplePos.xz * 0.00005;
    float4 weather = g_WeatherMap.SampleLevel(g_LinearWrap, weatherUV, 0);
    float coverage = weather.r * pc.cloudCoverage;
    float cloudType = weather.g;

    // Height gradient
    float heightGrad = HeightGradient(heightFrac, cloudType);

    // Base shape noise (low frequency)
    float3 baseUV = samplePos * 0.0003;
    float4 baseNoise = g_ShapeNoise.SampleLevel(g_LinearWrap, baseUV, 0);

    // Combine FBM from shape noise channels
    float baseFBM = baseNoise.g * 0.625 + baseNoise.b * 0.25 + baseNoise.a * 0.125;
    float baseShape = Remap(baseNoise.r, baseFBM - 1.0, 1.0, 0.0, 1.0);

    // Apply coverage and height
    float density = baseShape * heightGrad;
    density = Remap(density, 1.0 - coverage, 1.0, 0.0, 1.0);
    density = saturate(density) * pc.cloudDensity;

    if (density <= 0.0 || cheapSample) return density;

    // Detail erosion (high frequency)
    float3 detailUV = samplePos * pc.detailScale * 0.001;
    float4 detailNoise = g_DetailNoise.SampleLevel(g_LinearWrap, detailUV, 0);
    float detailFBM = detailNoise.r * 0.625 + detailNoise.g * 0.25 + detailNoise.b * 0.125;

    // Erode more at edges (lower density regions)
    float erosion = lerp(detailFBM, 1.0 - detailFBM, saturate(heightFrac * 3.0));
    density = Remap(density, erosion * pc.erosionStrength, 1.0, 0.0, 1.0);

    return max(density, 0.0);
}

// ─── Light Marching ──────────────────────────────────────────────────────

float LightMarch(float3 pos) {
    float3 lightDir = normalize(-pc.sunDirection.xyz);
    float stepSize = (pc.cloudLayerTop - pos.y) / max(lightDir.y, 0.001) / float(pc.lightSampleCount);
    stepSize = min(stepSize, pc.lightStepSize);

    float totalDensity = 0.0;
    float3 samplePos = pos;

    for (uint i = 0; i < pc.lightSampleCount; ++i) {
        samplePos += lightDir * stepSize;
        float density = SampleCloudDensity(samplePos, true);
        totalDensity += density * stepSize;
    }

    // Beer-Lambert extinction
    float beer = exp(-totalDensity * 0.04);

    // Powder sugar effect (brightening at thin cloud edges)
    float powder = 1.0 - exp(-totalDensity * 0.08);

    return beer * lerp(1.0, powder * 2.0, 0.5);
}

// ─── Ray-Cloud Intersection ──────────────────────────────────────────────

float2 RayCloudLayerIntersect(float3 origin, float3 dir) {
    // Returns (tNear, tFar) for intersection with cloud layer slab
    float tBottom = (pc.cloudLayerBottom - origin.y) / dir.y;
    float tTop = (pc.cloudLayerTop - origin.y) / dir.y;

    float tNear = min(tBottom, tTop);
    float tFar = max(tBottom, tTop);

    tNear = max(tNear, 0.0);
    return float2(tNear, tFar);
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= uint(pc.screenSize.x) || DTid.y >= uint(pc.screenSize.y)) return;

    float2 uv = (float2(DTid.xy) + 0.5) / pc.screenSize;

    // Reconstruct ray direction
    float4 clipPos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    clipPos.y = -clipPos.y;
    float4 worldPos = mul(pc.invViewProj, clipPos);
    float3 rayDir = normalize(worldPos.xyz / worldPos.w - pc.cameraPos.xyz);
    float3 rayOrigin = pc.cameraPos.xyz;

    // Scene depth check
    float depth = g_DepthBuffer.SampleLevel(g_LinearClamp, uv, 0);
    float sceneDistance = depth > 0.0 ? length(worldPos.xyz / worldPos.w - rayOrigin) : 1e10;

    // Intersect cloud layer
    float2 cloudT = RayCloudLayerIntersect(rayOrigin, rayDir);
    if (cloudT.x >= cloudT.y || cloudT.x > pc.maxRayDistance) {
        g_Output[DTid.xy] = float4(0, 0, 0, 1); // No clouds, full transmittance
        return;
    }

    cloudT.y = min(cloudT.y, min(sceneDistance, pc.maxRayDistance));

    // Ray march through cloud layer
    float stepSize = (cloudT.y - cloudT.x) / float(pc.primarySampleCount);
    float t = cloudT.x;

    float3 accumulatedColor = float3(0, 0, 0);
    float transmittance = 1.0;

    float3 lightDir = normalize(-pc.sunDirection.xyz);

    for (uint i = 0; i < pc.primarySampleCount; ++i) {
        if (transmittance < 0.01) break;

        float3 pos = rayOrigin + rayDir * t;
        float density = SampleCloudDensity(pos, false);

        if (density > 0.001) {
            // Light contribution
            float lightEnergy = LightMarch(pos);

            // Ambient lighting (stronger near top of cloud)
            float heightFrac = HeightFraction(pos);
            float3 ambient = pc.ambientColor.rgb * lerp(0.3, 1.0, heightFrac);

            // Direct sun lighting
            float3 direct = pc.sunColor.rgb * lightEnergy;

            // Silver lining (forward scattering at cloud edges)
            float cosAngle = dot(rayDir, lightDir);
            float silver = pow(saturate(cosAngle), pc.silverLiningSpread) * pc.silverLiningIntensity;
            direct += pc.sunColor.rgb * silver;

            float3 cloudColor = ambient + direct;

            // Accumulate with Beer-Lambert
            float sampleExtinction = density * stepSize * 0.04;
            float sampleTransmittance = exp(-sampleExtinction);

            accumulatedColor += cloudColor * (1.0 - sampleTransmittance) * transmittance;
            transmittance *= sampleTransmittance;
        }

        t += stepSize;
    }

    g_Output[DTid.xy] = float4(accumulatedColor, transmittance);
}
