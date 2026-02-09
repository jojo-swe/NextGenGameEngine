// ─── Screen-Space Rain/Wetness Shader ────────────────────────────────────
// Procedural rain and surface wetness effect with:
//   - Animated rain ripples on horizontal surfaces
//   - Puddle accumulation in concavities
//   - Vertical rain streaks on walls/windows
//   - Surface darkening and roughness reduction when wet
//   - Drip trails on angled surfaces
//
// References:
//   - "Rendering Rain in Real-Time" (Puig-Centelles et al., 2009)
//   - "Wet Surface Rendering in Uncharted 4" (Naughty Dog, SIGGRAPH 2016)
//   - "Rain Rendering in Ghost of Tsushima" (Sucker Punch, 2020)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor    : register(t0);
Texture2D<float>  g_SceneDepth    : register(t1);
Texture2D<float4> g_SceneNormal   : register(t2);
Texture2D<float>  g_NoiseTex      : register(t3); // Tileable noise
Texture2D<float4> g_RippleNormal  : register(t4); // Animated ripple normal atlas

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct RainConstants {
    float2 resolution;
    float2 invResolution;
    float  time;
    float  rainIntensity;       // 0=dry, 1=heavy rain (default 0.5)
    float  wetness;             // Surface wet amount 0..1 (default 0.5)
    float  puddleThreshold;     // Normal.y threshold for puddle (default 0.9)
    float  puddleDepth;         // Puddle reflection strength (default 0.3)
    float  rippleScale;         // Ripple pattern UV scale (default 4.0)
    float  rippleSpeed;         // Animation speed (default 1.5)
    float  rippleStrength;      // Normal distortion (default 0.02)
    float  streakScale;         // Wall streak UV scale (default 8.0)
    float  streakSpeed;         // Streak fall speed (default 2.0)
    float  darkeningAmount;     // Wet surface darkening (default 0.3)
    float  roughnessReduction;  // Wet roughness reduction (default 0.5)
    float  dripTrailWidth;      // Drip trail thickness (default 0.02)
    float  dripTrailSpeed;      // Drip fall speed (default 1.0)
    float  pad0;
    float  pad1;
};

[[vk::push_constant]] ConstantBuffer<RainConstants> cb;

// ─── Rain Ripples ────────────────────────────────────────────────────────
// Concentric ring patterns on horizontal (puddle) surfaces.

float2 RippleHash(float2 p) {
    p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));
    return frac(sin(p) * 43758.5453);
}

float3 RainRipple(float2 uv, float timeOffset) {
    float2 gridUV = uv * cb.rippleScale;
    float2 cell = floor(gridUV);
    float2 frac_uv = frac(gridUV);

    float3 totalNormal = float3(0, 0, 1);

    // Check 3x3 neighborhood for overlapping ripples
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float2 neighbor = float2(x, y);
            float2 randomOffset = RippleHash(cell + neighbor);

            // Staggered timing per cell
            float dropTime = frac(cb.time * cb.rippleSpeed * 0.3 + randomOffset.x + timeOffset);

            float2 dropCenter = neighbor + randomOffset - frac_uv;
            float dist = length(dropCenter);

            // Expanding ring
            float ringRadius = dropTime * 0.5;
            float ringWidth = 0.05;
            float ring = 1.0 - smoothstep(ringWidth, ringWidth * 2.0, abs(dist - ringRadius));

            // Fade out over time
            float fade = 1.0 - dropTime;
            ring *= fade * fade;

            // Normal from ring gradient
            if (dist > 0.001) {
                float2 grad = dropCenter / dist * ring;
                totalNormal.xy += grad * cb.rippleStrength;
            }
        }
    }

    return normalize(totalNormal);
}

// ─── Rain Streaks ────────────────────────────────────────────────────────
// Vertical water trails on wall/window surfaces.

float RainStreak(float2 uv, float3 worldNormal) {
    // Only on vertical surfaces
    float wallFactor = 1.0 - abs(worldNormal.y);
    if (wallFactor < 0.3) return 0.0;

    float2 streakUV = uv * float2(cb.streakScale, cb.streakScale * 0.3);
    streakUV.y += cb.time * cb.streakSpeed;

    // Multiple streak layers for variation
    float streak1 = g_NoiseTex.SampleLevel(g_LinearWrap, streakUV, 0);
    float streak2 = g_NoiseTex.SampleLevel(g_LinearWrap, streakUV * 1.7 + 0.3, 0);

    // Threshold to create thin trails
    float trail = smoothstep(0.75, 0.85, streak1) + smoothstep(0.8, 0.9, streak2) * 0.5;

    return saturate(trail * wallFactor * cb.rainIntensity);
}

// ─── Drip Trails ─────────────────────────────────────────────────────────
// Gravity-driven drips on angled surfaces.

float DripTrail(float2 uv, float slope) {
    if (slope < 0.2 || slope > 0.8) return 0.0;

    float2 dripUV = uv;
    dripUV.y += cb.time * cb.dripTrailSpeed;

    float noise = g_NoiseTex.SampleLevel(g_LinearWrap, dripUV * float2(20.0, 5.0), 0);
    float trail = smoothstep(1.0 - cb.dripTrailWidth, 1.0, noise);

    // Modulate by slope (strongest on 45-degree surfaces)
    float slopeMod = 1.0 - abs(slope - 0.5) * 2.0;

    return trail * slopeMod * cb.rainIntensity;
}

// ─── Puddle Detection ────────────────────────────────────────────────────
// Horizontal surfaces accumulate water into puddles.

float PuddleMask(float3 worldNormal, float2 uv) {
    float upFacing = worldNormal.y;
    if (upFacing < cb.puddleThreshold) return 0.0;

    // Noise-based puddle shape (concavity approximation)
    float puddleNoise = g_NoiseTex.SampleLevel(g_LinearWrap, uv * 3.0, 0);
    float puddle = smoothstep(0.4, 0.6, puddleNoise) * cb.wetness;

    // Stronger in flat areas
    float flatness = smoothstep(cb.puddleThreshold, 1.0, upFacing);

    return saturate(puddle * flatness);
}

// ─── Wetness Modifiers ───────────────────────────────────────────────────

float3 WetSurfaceDarkening(float3 color, float wetAmount) {
    // Wet surfaces appear darker (water fills micro-cavities, reducing diffuse)
    return color * lerp(1.0, 1.0 - cb.darkeningAmount, wetAmount);
}

float WetRoughnessReduction(float roughness, float wetAmount) {
    // Water film smooths surface
    return lerp(roughness, roughness * (1.0 - cb.roughnessReduction), wetAmount);
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float3 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;
    float3 worldNormal = g_SceneNormal.SampleLevel(g_LinearClamp, uv, 0).rgb * 2.0 - 1.0;
    worldNormal = normalize(worldNormal);

    if (cb.rainIntensity < 0.01 && cb.wetness < 0.01) {
        g_Output[DTid.xy] = float4(sceneColor, 1.0);
        return;
    }

    float slope = 1.0 - abs(worldNormal.y); // 0=flat, 1=vertical

    // ── Puddles ──────────────────────────────────────────────────────
    float puddle = PuddleMask(worldNormal, uv);

    // Ripples on puddle surfaces
    float3 rippleN = float3(0, 0, 1);
    if (puddle > 0.01 && cb.rainIntensity > 0.1) {
        rippleN = RainRipple(uv, 0.0);
        // Second layer for density
        float3 ripple2 = RainRipple(uv * 1.3 + 0.5, 0.37);
        rippleN = normalize(rippleN + (ripple2 - float3(0, 0, 1)) * 0.5);
    }

    // Puddle reflection (simple environment distortion)
    float2 reflectOffset = rippleN.xy * cb.puddleDepth * puddle;
    float2 reflectUV = uv + reflectOffset;
    reflectUV = clamp(reflectUV, 0.0, 1.0);
    float3 reflectColor = g_SceneColor.SampleLevel(g_LinearClamp, reflectUV, 0).rgb;

    // ── Streaks ──────────────────────────────────────────────────────
    float streak = RainStreak(uv, worldNormal);

    // ── Drip trails ──────────────────────────────────────────────────
    float drip = DripTrail(uv, slope);

    // ── Combined wetness ─────────────────────────────────────────────
    float totalWet = saturate(cb.wetness + puddle + streak * 0.3 + drip * 0.2);

    // Apply wetness: darken + smooth
    float3 wetColor = WetSurfaceDarkening(sceneColor, totalWet);

    // Blend puddle reflection
    wetColor = lerp(wetColor, reflectColor, puddle * 0.4);

    // Add streak highlight (water catches light)
    float streakHighlight = streak * 0.15;
    wetColor += float3(0.7, 0.8, 1.0) * streakHighlight;

    // Add drip trail highlight
    wetColor += float3(0.5, 0.6, 0.8) * drip * 0.1;

    // Specular boost on wet areas (sharper highlights)
    float specBoost = totalWet * 0.1;
    wetColor += float3(1, 1, 1) * specBoost * saturate(worldNormal.y);

    g_Output[DTid.xy] = float4(wetColor, 1.0);
}
