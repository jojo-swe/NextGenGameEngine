// ─── Procedural Energy Shield / Bubble Effect Shader ─────────────────────
// Screen-space post-process for sci-fi energy shield and force bubble
// effects with procedural hex grid, impact ripples, and Fresnel glow.
//
// Features:
//   - Procedural hexagonal grid pattern on shield surface
//   - Fresnel rim glow (stronger at glancing angles)
//   - Impact ripple animation (expanding rings from hit points)
//   - Shield opacity modulation by angle and distance
//   - Animated energy flow (scrolling noise along surface)
//   - Configurable shield color, intensity, and pattern scale
//   - Multiple simultaneous impact points (up to 8)
//   - Shield flicker on low health
//   - Depth intersection highlight (objects touching shield)
//   - Additive blending with scene color
//
// References:
//   - "Halo Shield Effects" (Bungie, GDC 2009)
//   - "Force Field Rendering in Mass Effect" (BioWare, SIGGRAPH 2012)
//   - "Procedural Hex Grids" (Red Blob Games)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor   : register(t0);
Texture2D<float>  g_SceneDepth   : register(t1);
Texture2D<float>  g_ShieldDepth  : register(t2); // Shield mesh depth
Texture2D<float4> g_ShieldNormal : register(t3); // Shield mesh world normal
Texture2D<float>  g_NoiseTex     : register(t4);

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct ImpactPoint {
    float3 worldPos;
    float  time;         // Time since impact (seconds)
    float  intensity;    // Impact strength (0-1)
    float  radius;       // Max ripple radius
    float  pad0;
    float  pad1;
};

struct EnergyShieldConstants {
    float2   resolution;
    float2   invResolution;
    float    time;
    float3   shieldColor;         // Base shield color (default: 0.2, 0.6, 1.0)
    float    shieldIntensity;     // Overall brightness (default 1.5)
    float    fresnelPower;        // Fresnel exponent (default 3.0)
    float    fresnelIntensity;    // Fresnel glow strength (default 2.0)
    float    hexScale;            // Hex grid scale (default 10.0)
    float    hexLineWidth;        // Hex grid line thickness (default 0.05)
    float    energyFlowSpeed;     // Noise scroll speed (default 0.5)
    float    energyFlowScale;     // Noise tiling (default 3.0)
    float    intersectionWidth;   // Depth intersection highlight width (default 0.01)
    float    flickerSpeed;        // Low-health flicker speed (default 0.0)
    float    flickerAmount;       // Flicker intensity (default 0.0)
    float    healthFraction;      // Shield health 0-1 (default 1.0)
    u32      impactCount;         // Active impacts (0-8)
    float    impactRippleSpeed;   // Ripple expansion speed (default 3.0)
    float    impactRippleWidth;   // Ripple ring width (default 0.1)
    float    impactDecay;         // Ripple fade time (default 1.5)
    float3   cameraPos;           // World-space camera position
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<EnergyShieldConstants> cb;

StructuredBuffer<ImpactPoint> g_Impacts : register(t5);

// ─── Hex Grid ────────────────────────────────────────────────────────────

float HexGrid(float2 uv) {
    float2 p = uv * cb.hexScale;

    // Hex grid coordinates
    float2 h = float2(1.0, sqrt(3.0));
    float2 a = fmod(p, h) - h * 0.5;
    float2 b = fmod(p - h * 0.5, h) - h * 0.5;

    float2 gv = (dot(a, a) < dot(b, b)) ? a : b;

    // Distance to hex edge
    float d = max(abs(gv.x), abs(gv.y * 0.577 + gv.x * 0.5));
    d = max(d, abs(gv.y * 0.577 - gv.x * 0.5));

    float hexEdge = smoothstep(0.5 - cb.hexLineWidth, 0.5, d);

    return hexEdge;
}

// ─── Fresnel ─────────────────────────────────────────────────────────────

float FresnelEffect(float3 normal, float3 viewDir) {
    float NdotV = saturate(dot(normal, viewDir));
    return pow(1.0 - NdotV, cb.fresnelPower) * cb.fresnelIntensity;
}

// ─── Impact Ripple ───────────────────────────────────────────────────────

float ImpactRipple(float3 worldPos, ImpactPoint impact) {
    float dist = length(worldPos - impact.worldPos);
    float elapsed = impact.time;

    if (elapsed > cb.impactDecay) return 0.0;

    float rippleRadius = elapsed * cb.impactRippleSpeed;
    float rippleDist = abs(dist - rippleRadius);

    // Ring shape
    float ring = smoothstep(cb.impactRippleWidth, 0.0, rippleDist);

    // Fade over time
    float fade = 1.0 - elapsed / cb.impactDecay;
    fade *= fade; // Quadratic falloff

    // Fade with distance from impact center
    float distFade = 1.0 - smoothstep(0.0, impact.radius, dist);

    return ring * fade * distFade * impact.intensity;
}

// ─── Energy Flow Noise ───────────────────────────────────────────────────

float EnergyFlow(float2 uv) {
    float2 flowUV = uv * cb.energyFlowScale;
    flowUV.y += cb.time * cb.energyFlowSpeed;
    flowUV.x += sin(cb.time * 0.3) * 0.2;

    float noise = g_NoiseTex.SampleLevel(g_LinearWrap, flowUV, 0);
    float noise2 = g_NoiseTex.SampleLevel(g_LinearWrap, flowUV * 1.5 + float2(0.5, cb.time * 0.2), 0);

    return noise * 0.6 + noise2 * 0.4;
}

// ─── Shield Flicker ──────────────────────────────────────────────────────

float ShieldFlicker() {
    if (cb.flickerSpeed <= 0.0) return 1.0;

    float flicker = sin(cb.time * cb.flickerSpeed) * 0.5 + 0.5;
    float flicker2 = sin(cb.time * cb.flickerSpeed * 3.7) * 0.5 + 0.5;

    // More flicker at lower health
    float healthFlicker = lerp(1.0, flicker * flicker2, (1.0 - cb.healthFraction) * cb.flickerAmount);

    return saturate(healthFlicker);
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float4 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);
    float sceneDepth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
    float shieldDepth = g_ShieldDepth.SampleLevel(g_LinearClamp, uv, 0);

    // ── Shield visibility check ──────────────────────────────────────
    // Shield is visible where its depth is in front of scene
    float shieldVisible = step(shieldDepth, sceneDepth) * step(0.001, shieldDepth);

    if (shieldVisible < 0.001) {
        g_Output[DTid.xy] = sceneColor;
        return;
    }

    float4 shieldNormal = g_ShieldNormal.SampleLevel(g_LinearClamp, uv, 0);
    float3 normal = normalize(shieldNormal.xyz * 2.0 - 1.0);

    // Approximate view direction from UV
    float3 viewDir = normalize(float3((uv - 0.5) * 2.0, 1.0));

    // ── Hex grid pattern ─────────────────────────────────────────────
    float hex = HexGrid(uv);

    // ── Fresnel rim glow ─────────────────────────────────────────────
    float fresnel = FresnelEffect(normal, -viewDir);

    // ── Energy flow ──────────────────────────────────────────────────
    float flow = EnergyFlow(uv);

    // ── Impact ripples ───────────────────────────────────────────────
    float totalImpact = 0.0;
    u32 impactCount = min(cb.impactCount, 8u);
    for (u32 i = 0; i < impactCount; ++i) {
        ImpactPoint impact = g_Impacts[i];
        // Use UV-space approximation for impact distance
        float3 approxWorldPos = float3(uv * 2.0 - 1.0, shieldDepth);
        totalImpact += ImpactRipple(approxWorldPos, impact);
    }

    // ── Depth intersection highlight ─────────────────────────────────
    float depthDiff = abs(sceneDepth - shieldDepth);
    float intersection = smoothstep(cb.intersectionWidth, 0.0, depthDiff);

    // ── Shield flicker ───────────────────────────────────────────────
    float flicker = ShieldFlicker();

    // ── Combine shield components ────────────────────────────────────
    float shieldAlpha = 0.0;
    shieldAlpha += hex * 0.3;
    shieldAlpha += fresnel;
    shieldAlpha += flow * 0.2;
    shieldAlpha += totalImpact * 2.0;
    shieldAlpha += intersection * 1.5;

    shieldAlpha *= cb.shieldIntensity * flicker;
    shieldAlpha = saturate(shieldAlpha);

    // ── Shield color with health tint ────────────────────────────────
    float3 shieldCol = cb.shieldColor;

    // Shift toward red at low health
    if (cb.healthFraction < 0.3) {
        float redShift = 1.0 - cb.healthFraction / 0.3;
        shieldCol = lerp(shieldCol, float3(1.0, 0.1, 0.05), redShift * 0.7);
    }

    // Impact flashes are brighter/whiter
    float3 impactColor = lerp(shieldCol, float3(1.0, 1.0, 1.0), 0.5) * totalImpact;

    float3 finalShield = shieldCol * shieldAlpha + impactColor;

    // ── Additive blend ───────────────────────────────────────────────
    float3 result = sceneColor.rgb + finalShield;

    g_Output[DTid.xy] = float4(result, 1.0);
}
