// ─── Procedural Chromatic Vignette / Lens Distortion Shader ──────────────
// Screen-space post-process combining chromatic aberration, vignette
// darkening, and lens distortion into a unified camera lens simulation.
//
// Features:
//   - Radial chromatic aberration (RGB channel separation)
//   - Barrel/pincushion lens distortion
//   - Smooth vignette with configurable falloff
//   - Anamorphic squeeze (horizontal/vertical stretch)
//   - Lens dirt overlay modulation
//   - Color fringing at screen edges
//   - Configurable aberration strength, distortion, and vignette
//   - Per-channel distortion offsets for realistic CA
//   - Animated breathing effect (subtle pulsation)
//
// References:
//   - "Practical Post-Process Depth of Field" (Jimenez, SIGGRAPH 2014)
//   - "Lens Distortion in Unreal Engine" (Epic Games)
//   - "Chromatic Aberration Done Right" (Filmic Worlds, 2015)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor : register(t0);
Texture2D<float>  g_LensDirt   : register(t1);

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct ChromaticVignetteConstants {
    float2   resolution;
    float2   invResolution;
    float    time;
    float    caIntensity;          // Chromatic aberration strength (default 0.005)
    float    caFalloff;            // CA increases toward edges (exponent, default 2.0)
    float    distortionK1;         // Barrel distortion coefficient (default 0.0)
    float    distortionK2;         // Higher-order distortion (default 0.0)
    float    vignetteIntensity;    // Vignette darkness (default 0.4)
    float    vignetteSoftness;     // Vignette falloff curve (default 0.5)
    float    vignetteRoundness;    // 0=rectangular, 1=circular (default 0.8)
    float    anamorphicSqueeze;    // Horizontal squeeze factor (default 1.0)
    float    lensDirtIntensity;    // Lens dirt overlay strength (default 0.0)
    float    breatheSpeed;         // Animated pulsation speed (default 0.0)
    float    breatheAmount;        // Pulsation intensity (default 0.0)
    float3   caChannelOffsets;     // Per-channel distortion multipliers (R, G, B)
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<ChromaticVignetteConstants> cb;

// ─── Lens Distortion ─────────────────────────────────────────────────────

float2 ApplyDistortion(float2 uv, float extraOffset) {
    float2 centered = (uv - 0.5) * 2.0;

    // Anamorphic squeeze
    centered.x *= cb.anamorphicSqueeze;

    float r2 = dot(centered, centered);
    float r4 = r2 * r2;

    // Brown-Conrady distortion model
    float distortion = 1.0 + (cb.distortionK1 + extraOffset) * r2 + cb.distortionK2 * r4;

    centered *= distortion;

    // Undo anamorphic
    centered.x /= cb.anamorphicSqueeze;

    return centered * 0.5 + 0.5;
}

// ─── Chromatic Aberration ────────────────────────────────────────────────

float3 ChromaticAberration(float2 uv) {
    float2 centered = uv - 0.5;
    float dist = length(centered);

    // CA strength increases toward edges
    float caStrength = cb.caIntensity * pow(dist * 2.0, cb.caFalloff);

    // Animated breathing
    if (cb.breatheSpeed > 0.0) {
        float breathe = sin(cb.time * cb.breatheSpeed) * cb.breatheAmount;
        caStrength *= 1.0 + breathe;
    }

    // Per-channel UV offsets (radial direction)
    float2 dir = normalize(centered + 0.0001);

    float2 uvR = uv + dir * caStrength * cb.caChannelOffsets.x;
    float2 uvG = uv + dir * caStrength * cb.caChannelOffsets.y;
    float2 uvB = uv + dir * caStrength * cb.caChannelOffsets.z;

    // Apply lens distortion per channel
    if (abs(cb.distortionK1) > 0.0001 || abs(cb.distortionK2) > 0.0001) {
        float channelSpread = caStrength * 0.5;
        uvR = ApplyDistortion(uvR, -channelSpread);
        uvG = ApplyDistortion(uvG, 0.0);
        uvB = ApplyDistortion(uvB, channelSpread);
    }

    // Clamp to valid range
    uvR = clamp(uvR, 0.0, 1.0);
    uvG = clamp(uvG, 0.0, 1.0);
    uvB = clamp(uvB, 0.0, 1.0);

    float r = g_SceneColor.SampleLevel(g_LinearClamp, uvR, 0).r;
    float g = g_SceneColor.SampleLevel(g_LinearClamp, uvG, 0).g;
    float b = g_SceneColor.SampleLevel(g_LinearClamp, uvB, 0).b;

    return float3(r, g, b);
}

// ─── Vignette ────────────────────────────────────────────────────────────

float ComputeVignette(float2 uv) {
    float2 centered = (uv - 0.5) * 2.0;

    // Roundness: interpolate between rectangular and circular distance
    float circularDist = length(centered);
    float rectDist = max(abs(centered.x), abs(centered.y));
    float dist = lerp(rectDist, circularDist, cb.vignetteRoundness);

    // Smooth falloff
    float vignette = 1.0 - pow(saturate(dist), 2.0 / max(cb.vignetteSoftness, 0.01));
    vignette = lerp(1.0, vignette, cb.vignetteIntensity);

    return saturate(vignette);
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    // ── Chromatic aberration with lens distortion ────────────────────
    float3 color;
    if (cb.caIntensity > 0.0001) {
        color = ChromaticAberration(uv);
    } else if (abs(cb.distortionK1) > 0.0001 || abs(cb.distortionK2) > 0.0001) {
        float2 distortedUV = ApplyDistortion(uv, 0.0);
        distortedUV = clamp(distortedUV, 0.0, 1.0);
        color = g_SceneColor.SampleLevel(g_LinearClamp, distortedUV, 0).rgb;
    } else {
        color = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;
    }

    // ── Vignette ─────────────────────────────────────────────────────
    float vignette = ComputeVignette(uv);
    color *= vignette;

    // ── Lens dirt overlay ────────────────────────────────────────────
    if (cb.lensDirtIntensity > 0.0) {
        float dirt = g_LensDirt.SampleLevel(g_LinearWrap, uv, 0);
        float luminance = dot(color, float3(0.2126, 0.7152, 0.0722));
        color += color * dirt * cb.lensDirtIntensity * luminance;
    }

    color = saturate(color);

    g_Output[DTid.xy] = float4(color, 1.0);
}
