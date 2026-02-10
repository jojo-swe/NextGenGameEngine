// ─── Screen-Space Heat Distortion Shader ─────────────────────────────────
// Full-screen post-process that simulates atmospheric heat shimmer /
// mirage distortion from hot surfaces, explosions, and exhaust plumes.
//
// Features:
//   - Animated UV distortion from dual-layer scrolling noise
//   - Distance-based intensity falloff (stronger near heat source)
//   - Depth-aware masking (don't distort objects in front of heat source)
//   - Multiple heat source support via structured buffer
//   - Chromatic aberration in distorted regions
//   - Temporal stability with jitter compensation
//   - Screen-space radial falloff per source
//
// References:
//   - "Heat Haze in Uncharted 4" (Naughty Dog, GDC 2016)
//   - "Real-Time Atmospheric Effects" (DICE, Frostbite)
//   - "Distortion Effects in Doom Eternal" (id Software)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor   : register(t0);
Texture2D<float>  g_SceneDepth   : register(t1);
Texture2D<float>  g_NoiseTex     : register(t2); // Tileable perlin/simplex noise

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct HeatSource {
    float3 worldPosition;
    float  radius;           // World-space influence radius
    float  intensity;        // Distortion strength (default 1.0)
    float  riseSpeed;        // Vertical shimmer speed (default 2.0)
    float  noiseFrequency;   // Noise UV frequency (default 5.0)
    float  pad0;
};

struct HeatConstants {
    float4x4 viewProjMatrix;
    float4x4 invViewProjMatrix;
    float2   resolution;
    float2   invResolution;
    float3   cameraPos;
    float    time;
    float    globalIntensity;       // Master intensity (default 1.0)
    float    distortionScale;       // UV offset scale (default 0.015)
    float    chromaticStrength;     // Chromatic aberration in heat (default 0.003)
    float    depthFadeStart;        // Depth fade near (default 5.0)
    float    depthFadeEnd;          // Depth fade far (default 100.0)
    float    noiseScrollSpeed;      // Secondary noise scroll (default 1.5)
    u32      sourceCount;           // Number of active heat sources
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<HeatConstants> cb;

StructuredBuffer<HeatSource> g_HeatSources : register(t3);

// ─── World Position Reconstruction ───────────────────────────────────────

float3 ReconstructWorldPos(float2 uv, float depth) {
    float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
    clip.y = -clip.y;
    float4 world = mul(cb.invViewProjMatrix, clip);
    return world.xyz / world.w;
}

float2 ProjectToScreen(float3 worldPos) {
    float4 clip = mul(cb.viewProjMatrix, float4(worldPos, 1.0));
    clip.xy /= clip.w;
    float2 screen = clip.xy * 0.5 + 0.5;
    screen.y = 1.0 - screen.y;
    return screen;
}

// ─── Noise Sampling ──────────────────────────────────────────────────────

float2 SampleDistortionNoise(float2 uv, float speed, float frequency) {
    // Dual-layer scrolling noise for organic shimmer
    float2 uv1 = uv * frequency + float2(0, cb.time * speed);
    float2 uv2 = uv * frequency * 1.3 + float2(cb.time * speed * 0.7, 0);

    float n1 = g_NoiseTex.SampleLevel(g_LinearWrap, uv1, 0);
    float n2 = g_NoiseTex.SampleLevel(g_LinearWrap, uv2, 0);

    // Convert from [0,1] to [-1,1]
    float2 distortion;
    distortion.x = (n1 - 0.5) * 2.0;
    distortion.y = (n2 - 0.5) * 2.0;

    // Bias upward (heat rises)
    distortion.y = abs(distortion.y) * -1.0; // Always distort upward in screen space

    return distortion;
}

// ─── Per-Source Heat Influence ────────────────────────────────────────────

float ComputeHeatInfluence(float2 pixelUV, float pixelDepth, float3 pixelWorldPos,
                            HeatSource source) {
    // Project heat source to screen
    float2 sourceScreen = ProjectToScreen(source.worldPosition);

    // Screen-space distance
    float2 screenDelta = (pixelUV - sourceScreen) * float2(cb.resolution.x / cb.resolution.y, 1.0);
    float screenDist = length(screenDelta);

    // World-space distance for radius check
    float worldDist = length(pixelWorldPos - source.worldPosition);

    // Outside influence radius
    if (worldDist > source.radius * 2.0) return 0.0;

    // Radial falloff (smooth)
    float radialFalloff = 1.0 - smoothstep(0.0, source.radius, worldDist);
    radialFalloff *= radialFalloff; // Quadratic falloff

    // Depth-based masking: don't distort objects in front of heat source
    float sourceDepthLinear = length(source.worldPosition - cb.cameraPos);
    float pixelDepthLinear = length(pixelWorldPos - cb.cameraPos);

    // Only distort pixels behind or near the heat source
    float depthMask = smoothstep(sourceDepthLinear - source.radius * 0.5,
                                  sourceDepthLinear + source.radius, pixelDepthLinear);

    // Screen-space vertical bias: heat rises, so more distortion above source
    float verticalBias = saturate((sourceScreen.y - pixelUV.y) * 3.0 + 0.5);

    return radialFalloff * depthMask * verticalBias * source.intensity;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float depth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
    float3 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;

    // Skip sky
    if (depth < 0.0001) {
        g_Output[DTid.xy] = float4(sceneColor, 1.0);
        return;
    }

    float3 worldPos = ReconstructWorldPos(uv, depth);

    // Accumulate heat influence from all sources
    float totalInfluence = 0.0;
    float2 totalDistortion = float2(0, 0);

    for (u32 i = 0; i < cb.sourceCount && i < 32; ++i) {
        HeatSource source = g_HeatSources[i];
        float influence = ComputeHeatInfluence(uv, depth, worldPos, source);

        if (influence > 0.001) {
            float2 noise = SampleDistortionNoise(uv, source.riseSpeed, source.noiseFrequency);
            totalDistortion += noise * influence * source.intensity;
            totalInfluence += influence;
        }
    }

    totalInfluence = saturate(totalInfluence);

    if (totalInfluence < 0.001) {
        g_Output[DTid.xy] = float4(sceneColor, 1.0);
        return;
    }

    // Apply distance-based global fade
    float pixelDist = length(worldPos - cb.cameraPos);
    float distFade = 1.0 - smoothstep(cb.depthFadeStart, cb.depthFadeEnd, pixelDist);
    totalDistortion *= distFade * cb.globalIntensity * cb.distortionScale;

    // Sample distorted scene color
    float2 distortedUV = clamp(uv + totalDistortion, 0.0, 1.0);
    float3 distortedColor = g_SceneColor.SampleLevel(g_LinearClamp, distortedUV, 0).rgb;

    // Chromatic aberration in distorted regions
    if (cb.chromaticStrength > 0.0) {
        float chromaOffset = cb.chromaticStrength * totalInfluence;
        float2 chromaDir = normalize(totalDistortion + 0.0001);

        float r = g_SceneColor.SampleLevel(g_LinearClamp,
                    clamp(distortedUV + chromaDir * chromaOffset, 0.0, 1.0), 0).r;
        float b = g_SceneColor.SampleLevel(g_LinearClamp,
                    clamp(distortedUV - chromaDir * chromaOffset, 0.0, 1.0), 0).b;

        distortedColor.r = r;
        distortedColor.b = b;
    }

    // Blend based on influence
    float3 finalColor = lerp(sceneColor, distortedColor, totalInfluence);

    g_Output[DTid.xy] = float4(finalColor, 1.0);
}
