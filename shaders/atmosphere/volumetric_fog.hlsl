// ─── Volumetric Fog (Froxel-Based) ───────────────────────────────────────
// Screen-space volumetric lighting using froxel (frustum-aligned voxel) grid.
//
// Pipeline:
//   1. Inject: Scatter light density + scattering into 3D froxel texture
//   2. Propagate: March through froxels front-to-back, accumulate in-scattering
//   3. Apply: Sample froxel texture during material resolve or composite
//
// Froxel grid: screenW/8 × screenH/8 × 128 depth slices (exponential distribution)

#include "../common/math.hlsl"

// ─── Froxel Parameters ──────────────────────────────────────────────────

struct FogConstants {
    float4x4 invViewProj;
    float4x4 prevViewProj;
    float4   cameraPos;
    float4   sunDirection;
    float4   sunColor;          // Color × intensity
    float4   fogColor;          // Ambient fog color
    float    fogDensity;        // Global density multiplier
    float    fogAbsorption;     // Absorption coefficient
    float    fogAnisotropy;     // Henyey-Greenstein asymmetry (0.6)
    float    nearPlane;
    float    farPlane;
    float    noiseScale;
    float    noiseStrength;
    float    windSpeed;
    float3   windDirection;
    float    time;
    uint     froxelWidth;
    uint     froxelHeight;
    uint     froxelDepth;
    uint     enableNoise;
};

ConstantBuffer<FogConstants> g_Fog : register(b0, space8);

// Froxel 3D textures
RWTexture3D<float4> g_ScatteringExtinction : register(u0, space8); // rgb=scattering, a=extinction
RWTexture3D<float4> g_IntegratedFog        : register(u1, space8); // rgb=in-scatter, a=transmittance

Texture3D<float4>   g_PrevIntegratedFog    : register(t0, space8); // Previous frame for temporal
Texture3D<float>     g_NoiseTexture        : register(t1, space8); // 3D noise for fog variation
Texture2D<float>     g_ShadowMap           : register(t2, space8); // For shadow ray

SamplerState g_LinearClamp  : register(s0, space8);
SamplerState g_LinearRepeat : register(s1, space8);

// ─── Depth Distribution ──────────────────────────────────────────────────
// Exponential depth slicing for better near-field resolution

float SliceToDepth(float slice, float nearZ, float farZ, float depthSlices) {
    float t = slice / depthSlices;
    return nearZ * pow(farZ / nearZ, t);
}

float DepthToSlice(float depth, float nearZ, float farZ, float depthSlices) {
    return depthSlices * log(depth / nearZ) / log(farZ / nearZ);
}

float3 FroxelToWorldPos(uint3 froxelCoord) {
    float2 uv = (float2(froxelCoord.xy) + 0.5) / float2(g_Fog.froxelWidth, g_Fog.froxelHeight);
    float depth = SliceToDepth(float(froxelCoord.z) + 0.5, g_Fog.nearPlane, g_Fog.farPlane,
                               float(g_Fog.froxelDepth));

    uv = uv * 2.0 - 1.0;
    uv.y = -uv.y;

    float4 clipPos = float4(uv, 0.5, 1.0); // approximate clip z
    float4 worldPos = mul(g_Fog.invViewProj, clipPos);
    float3 dir = normalize(worldPos.xyz / worldPos.w - g_Fog.cameraPos.xyz);

    return g_Fog.cameraPos.xyz + dir * depth;
}

// ─── Henyey-Greenstein Phase Function ────────────────────────────────────

float PhaseHG(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * pow(denom, 1.5));
}

// ─── Pass 1: Light & Density Injection ───────────────────────────────────

[numthreads(8, 8, 1)]
void InjectCS(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= g_Fog.froxelWidth || DTid.y >= g_Fog.froxelHeight || DTid.z >= g_Fog.froxelDepth)
        return;

    float3 worldPos = FroxelToWorldPos(DTid);

    // Base density
    float density = g_Fog.fogDensity;

    // Height-based falloff (exponential)
    float heightFalloff = exp(-max(worldPos.y, 0.0) * 0.1);
    density *= heightFalloff;

    // Animated noise for variation
    if (g_Fog.enableNoise) {
        float3 noiseCoord = worldPos * g_Fog.noiseScale + g_Fog.windDirection * g_Fog.time * g_Fog.windSpeed;
        float noise = g_NoiseTexture.SampleLevel(g_LinearRepeat, noiseCoord * 0.01, 0);
        density *= lerp(1.0, noise, g_Fog.noiseStrength);
    }

    density = max(density, 0.0);

    // Light contribution at this froxel
    float3 viewDir = normalize(worldPos - g_Fog.cameraPos.xyz);
    float cosTheta = dot(viewDir, g_Fog.sunDirection.xyz);
    float phase = PhaseHG(cosTheta, g_Fog.fogAnisotropy);

    // Shadow test for sun
    // TODO: sample virtual shadow map for this world position
    float shadow = 1.0;

    float3 scattering = g_Fog.sunColor.rgb * phase * shadow * density;
    scattering += g_Fog.fogColor.rgb * density * 0.1; // Ambient

    float extinction = density * (1.0 + g_Fog.fogAbsorption);

    g_ScatteringExtinction[DTid] = float4(scattering, extinction);
}

// ─── Pass 2: Front-to-Back Integration ───────────────────────────────────

[numthreads(8, 8, 1)]
void IntegrateCS(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= g_Fog.froxelWidth || DTid.y >= g_Fog.froxelHeight) return;

    float3 accumulatedScattering = float3(0, 0, 0);
    float accumulatedTransmittance = 1.0;

    for (uint z = 0; z < g_Fog.froxelDepth; ++z) {
        uint3 coord = uint3(DTid.xy, z);

        float4 scatterExtinction = g_ScatteringExtinction[coord];
        float3 scattering = scatterExtinction.rgb;
        float extinction = scatterExtinction.a;

        // Slice thickness
        float depthFront = SliceToDepth(float(z), g_Fog.nearPlane, g_Fog.farPlane, float(g_Fog.froxelDepth));
        float depthBack = SliceToDepth(float(z + 1), g_Fog.nearPlane, g_Fog.farPlane, float(g_Fog.froxelDepth));
        float sliceThickness = depthBack - depthFront;

        // Beer-Lambert transmittance through this slice
        float sliceTransmittance = exp(-extinction * sliceThickness);

        // Energy-conserving in-scattering integration
        // Int(scattering × e^(-extinction × t), 0, thickness)
        // ≈ scattering × (1 - e^(-extinction × thickness)) / extinction
        float3 sliceScattering;
        if (extinction > EPSILON) {
            sliceScattering = scattering * (1.0 - sliceTransmittance) / extinction;
        } else {
            sliceScattering = scattering * sliceThickness;
        }

        accumulatedScattering += accumulatedTransmittance * sliceScattering;
        accumulatedTransmittance *= sliceTransmittance;

        // Temporal reprojection blend (reduces flicker)
        float4 prevValue = g_PrevIntegratedFog.SampleLevel(g_LinearClamp,
            float3((float2(DTid.xy) + 0.5) / float2(g_Fog.froxelWidth, g_Fog.froxelHeight),
                   (float(z) + 0.5) / float(g_Fog.froxelDepth)), 0);

        float3 blended = lerp(float3(accumulatedScattering), prevValue.rgb, 0.9);
        float blendedT = lerp(accumulatedTransmittance, prevValue.a, 0.9);

        g_IntegratedFog[coord] = float4(blended, blendedT);
    }
}
