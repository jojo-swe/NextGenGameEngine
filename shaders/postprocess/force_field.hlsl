// ─── Procedural Force Field / Energy Shield Effect Shader ────────────────
// Screen-space post-process for sci-fi energy shields, force fields, and
// barrier effects with hex grid pattern, impact ripples, and Fresnel glow.
//
// Features:
//   - Hexagonal grid pattern with animated pulse
//   - Fresnel-based edge glow (view-angle dependent)
//   - Impact ripple rings with decay and propagation
//   - Animated energy flow lines (scrolling UV distortion)
//   - Intersection highlight where field meets geometry
//   - Per-shield configurable color, opacity, and pattern scale
//   - Multiple shield source support
//   - Depth-based intersection detection
//
// References:
//   - "Halo Energy Shields" (343 Industries, GDC 2018)
//   - "Force Field Rendering in Destiny 2" (Bungie, SIGGRAPH 2020)
//   - "Hex Grid Shaders" (Inigo Quilez, 2017)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor    : register(t0);
Texture2D<float>  g_SceneDepth    : register(t1);
Texture2D<float4> g_GBufferNormal : register(t2);
Texture2D<float>  g_NoiseTex      : register(t3);
Texture2D<float>  g_ShieldDepth   : register(t4); // Depth of shield geometry

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct ImpactPoint {
    float3 worldPosition;
    float  impactTime;     // Time of impact
    float  rippleSpeed;    // Propagation speed
    float  rippleWidth;    // Width of ripple ring
    float  intensity;      // Impact strength
    float  pad0;
};

struct ShieldSource {
    float3 center;         // World-space center
    float  radius;         // World-space radius
    float3 color;          // Shield color
    float  opacity;        // Base opacity (0..1)
    float  hexScale;       // Hex grid scale (default 20.0)
    float  pulseSpeed;     // Hex pulse animation speed
    float  flowSpeed;      // Energy flow speed
    float  fresnelPower;   // Edge glow exponent (default 3.0)
    float  fresnelIntensity; // Edge glow brightness (default 2.0)
    float  intersectionWidth; // Geometry intersection band (default 0.1)
    u32    impactCount;
    float  pad0;
};

struct ForceFieldConstants {
    float4x4 viewProjMatrix;
    float4x4 invViewProjMatrix;
    float2   resolution;
    float2   invResolution;
    float3   cameraPos;
    float    time;
    float    globalOpacity;      // Master opacity (default 1.0)
    float    distortionAmount;   // UV distortion from energy flow (default 0.002)
    float    flickerAmount;      // Random flicker intensity (default 0.1)
    u32      shieldCount;
};

[[vk::push_constant]] ConstantBuffer<ForceFieldConstants> cb;

StructuredBuffer<ShieldSource> g_Shields : register(t5);
StructuredBuffer<ImpactPoint>  g_Impacts : register(t6);

// ─── Hex Grid ────────────────────────────────────────────────────────────

float2 HexCenter(float2 p) {
    // Axial coordinates for hex grid
    float2 a = float2(1.0, 0.0);
    float2 b = float2(0.5, 0.866025);

    float2 pa = float2(dot(p, a), dot(p, b));
    float2 pi = floor(pa + 0.5);

    // Find closest hex center
    float2 pf = pa - pi;

    float2 ca = pi;
    float2 cb2 = pi + sign(pf);

    float2 wa = float2(dot(ca, float2(a.x, b.x)), dot(ca, float2(a.y, b.y)));
    float2 wb = float2(dot(cb2, float2(a.x, b.x)), dot(cb2, float2(a.y, b.y)));

    float da = length(p - wa);
    float db = length(p - wb);

    return da < db ? wa : wb;
}

float HexDist(float2 p) {
    p = abs(p);
    return max(dot(p, float2(1.0, 0.577350)), p.y * 1.154700);
}

float HexGrid(float2 uv, float scale) {
    float2 p = uv * scale;
    float2 center = HexCenter(p);
    float2 local = p - center;

    float dist = HexDist(local);
    float edge = smoothstep(0.45, 0.5, dist);

    return edge;
}

// ─── Sphere Mapping ──────────────────────────────────────────────────────

float2 SphereUV(float3 worldPos, float3 center) {
    float3 dir = normalize(worldPos - center);
    float u = atan2(dir.z, dir.x) / (2.0 * 3.14159) + 0.5;
    float v = asin(clamp(dir.y, -1.0, 1.0)) / 3.14159 + 0.5;
    return float2(u, v);
}

// ─── Fresnel ─────────────────────────────────────────────────────────────

float FresnelTerm(float3 viewDir, float3 normal, float power) {
    float NdotV = saturate(dot(normal, viewDir));
    return pow(1.0 - NdotV, power);
}

// ─── Impact Ripples ──────────────────────────────────────────────────────

float ImpactRipple(float3 worldPos, ImpactPoint impact, float currentTime) {
    float elapsed = currentTime - impact.impactTime;
    if (elapsed < 0.0 || elapsed > 3.0) return 0.0; // Max 3 second ripple

    float dist = length(worldPos - impact.worldPosition);
    float rippleRadius = elapsed * impact.rippleSpeed;
    float rippleDist = abs(dist - rippleRadius);

    // Ring shape
    float ring = smoothstep(impact.rippleWidth, 0.0, rippleDist);

    // Decay over time
    float decay = exp(-elapsed * 1.5);

    // Multiple concentric rings
    float ring2Dist = abs(dist - rippleRadius * 0.7);
    float ring2 = smoothstep(impact.rippleWidth * 0.5, 0.0, ring2Dist) * 0.5;

    return (ring + ring2) * decay * impact.intensity;
}

// ─── Reconstruct World Pos ───────────────────────────────────────────────

float3 ReconstructWorldPos(float2 uv, float depth) {
    float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
    clip.y = -clip.y;
    float4 world = mul(cb.invViewProjMatrix, clip);
    return world.xyz / world.w;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float4 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);
    float sceneDepth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
    float shieldDepth = g_ShieldDepth.SampleLevel(g_LinearClamp, uv, 0);
    float3 normal = g_GBufferNormal.SampleLevel(g_LinearClamp, uv, 0).xyz * 2.0 - 1.0;

    float3 sceneWorldPos = ReconstructWorldPos(uv, sceneDepth);
    float3 shieldWorldPos = ReconstructWorldPos(uv, shieldDepth);
    float3 viewDir = normalize(cb.cameraPos - shieldWorldPos);

    float3 totalShieldColor = float3(0, 0, 0);
    float totalShieldAlpha = 0.0;

    for (u32 s = 0; s < cb.shieldCount && s < 8; ++s) {
        ShieldSource shield = g_Shields[s];

        // Check if this pixel is on the shield surface
        float distToCenter = length(shieldWorldPos - shield.center);
        float onShield = smoothstep(shield.radius + 0.2, shield.radius - 0.2, distToCenter);
        if (onShield < 0.01) continue;

        // ── Hex grid pattern ─────────────────────────────────────────
        float2 sphereUV = SphereUV(shieldWorldPos, shield.center);
        float hexEdge = HexGrid(sphereUV, shield.hexScale);

        // Animated hex pulse
        float hexPulse = 0.3 + 0.7 * (0.5 + 0.5 * sin(cb.time * shield.pulseSpeed +
                         length(sphereUV) * 10.0));

        // ── Energy flow ──────────────────────────────────────────────
        float2 flowUV = sphereUV + float2(0, cb.time * shield.flowSpeed * 0.1);
        float flow = g_NoiseTex.SampleLevel(g_LinearWrap, flowUV * 4.0, 0);
        float flow2 = g_NoiseTex.SampleLevel(g_LinearWrap, flowUV * 8.0 + 0.5, 0);
        float energyFlow = flow * 0.6 + flow2 * 0.4;

        // ── Fresnel edge glow ────────────────────────────────────────
        float3 shieldNormal = normalize(shieldWorldPos - shield.center);
        float fresnel = FresnelTerm(viewDir, shieldNormal, shield.fresnelPower);
        fresnel *= shield.fresnelIntensity;

        // ── Intersection highlight ───────────────────────────────────
        float depthDiff = abs(sceneDepth - shieldDepth);
        float intersection = smoothstep(shield.intersectionWidth, 0.0, depthDiff);

        // ── Impact ripples ───────────────────────────────────────────
        float totalRipple = 0.0;
        // Process impacts for this shield (assume impacts are sequential per shield)
        // For simplicity, iterate up to impactCount from buffer start
        for (u32 i = 0; i < shield.impactCount && i < 16; ++i) {
            totalRipple += ImpactRipple(shieldWorldPos, g_Impacts[i], cb.time);
        }
        totalRipple = saturate(totalRipple);

        // ── Flicker ──────────────────────────────────────────────────
        float flicker = 1.0 - cb.flickerAmount * (0.5 + 0.5 * sin(cb.time * 30.0 + s * 7.0));

        // ── Composite shield ─────────────────────────────────────────
        float pattern = hexEdge * hexPulse * 0.6 + energyFlow * 0.3 + 0.1;
        float alpha = (pattern + fresnel + intersection * 2.0 + totalRipple) * shield.opacity;
        alpha *= onShield * flicker * cb.globalOpacity;
        alpha = saturate(alpha);

        // Shield color with HDR emission on edges and impacts
        float3 shieldCol = shield.color * pattern;
        shieldCol += shield.color * fresnel * 2.0;                    // Edge glow
        shieldCol += shield.color * intersection * 4.0;              // Intersection glow
        shieldCol += shield.color * totalRipple * 3.0;              // Impact flash
        shieldCol += float3(1, 1, 1) * totalRipple * totalRipple;   // White flash on impact

        totalShieldColor += shieldCol * alpha;
        totalShieldAlpha = saturate(totalShieldAlpha + alpha);
    }

    // Blend shield over scene
    float3 finalColor = lerp(sceneColor.rgb, totalShieldColor, totalShieldAlpha * 0.5);
    finalColor += totalShieldColor * 0.5; // Additive emission component

    // UV distortion from shield energy
    if (totalShieldAlpha > 0.01) {
        float2 distortUV = uv;
        float distortNoise = g_NoiseTex.SampleLevel(g_LinearWrap,
            uv * 10.0 + cb.time * 0.3, 0);
        distortUV += (distortNoise - 0.5) * cb.distortionAmount * totalShieldAlpha;
        distortUV = clamp(distortUV, 0.0, 1.0);

        float3 distortedScene = g_SceneColor.SampleLevel(g_LinearClamp, distortUV, 0).rgb;
        finalColor = lerp(finalColor, distortedScene + totalShieldColor, totalShieldAlpha * 0.3);
    }

    g_Output[DTid.xy] = float4(finalColor, 1.0);
}
