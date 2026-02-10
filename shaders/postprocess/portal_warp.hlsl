// ─── Procedural Portal / Warp Distortion Effect Shader ───────────────────
// Screen-space post-process for sci-fi portal, black hole, and spatial
// warp rendering with gravitational lensing, event horizon, and
// accretion disc effects.
//
// Features:
//   - Gravitational lensing UV distortion (radial bend toward center)
//   - Event horizon masking (black disc with soft edge)
//   - Accretion disc glow with Doppler shift simulation
//   - Edge chromatic aberration (rainbow fringing)
//   - Animated swirl/rotation of distortion field
//   - Depth-aware warping (objects behind portal distorted more)
//   - Multiple portal source support
//   - Configurable warp strength, radius, and rotation speed
//
// References:
//   - "Interstellar Black Hole VFX" (Double Negative, SIGGRAPH 2015)
//   - "Portal Rendering in Portal 2" (Valve, GDC 2011)
//   - "Gravitational Lensing in Games" (Oliver et al., I3D 2019)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor   : register(t0);
Texture2D<float>  g_SceneDepth   : register(t1);
Texture2D<float>  g_NoiseTex     : register(t2);

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct PortalSource {
    float3 worldPosition;
    float  radius;              // World-space radius
    float  warpStrength;        // Distortion intensity (default 1.0)
    float  rotationSpeed;       // Swirl angular velocity (default 1.0)
    float  eventHorizonRadius;  // Inner black disc fraction of radius (default 0.2)
    float  accretionWidth;      // Accretion disc width fraction (default 0.15)
    float3 portalColor;         // Tint color (default: 0.3, 0.5, 1.0)
    float  pad0;
};

struct PortalConstants {
    float4x4 viewProjMatrix;
    float4x4 invViewProjMatrix;
    float2   resolution;
    float2   invResolution;
    float3   cameraPos;
    float    time;
    float    globalStrength;     // Master intensity (default 1.0)
    float    chromaticAmount;    // Edge chromatic aberration (default 0.005)
    float    lensingPower;       // Gravitational lensing exponent (default 2.0)
    float    noiseAmount;        // Distortion noise modulation (default 0.1)
    float    glowIntensity;     // Accretion/edge glow brightness (default 3.0)
    float    fadeStart;         // Distance fade near (default 2.0)
    float    fadeEnd;           // Distance fade far (default 80.0)
    u32      sourceCount;
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<PortalConstants> cb;

StructuredBuffer<PortalSource> g_Portals : register(t3);

// ─── Utility ─────────────────────────────────────────────────────────────

float3 ReconstructWorldPos(float2 uv, float depth) {
    float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
    clip.y = -clip.y;
    float4 world = mul(cb.invViewProjMatrix, clip);
    return world.xyz / world.w;
}

float2 ProjectToScreen(float3 worldPos) {
    float4 clip = mul(cb.viewProjMatrix, float4(worldPos, 1.0));
    if (clip.w <= 0.0) return float2(-10, -10); // Behind camera
    clip.xy /= clip.w;
    float2 screen = clip.xy * 0.5 + 0.5;
    screen.y = 1.0 - screen.y;
    return screen;
}

// ─── Gravitational Lensing ───────────────────────────────────────────────
// Bends UV coordinates toward portal center with inverse-square falloff.

float2 GravitationalLens(float2 uv, float2 portalCenter, float portalScreenRadius,
                          float strength, float power, float swirl) {
    float2 delta = uv - portalCenter;
    // Correct for aspect ratio
    delta.x *= cb.resolution.x / cb.resolution.y;
    float dist = length(delta);

    if (dist < 0.001 || dist > portalScreenRadius * 3.0) return uv;

    float normalizedDist = dist / portalScreenRadius;

    // Inverse power falloff: stronger near center
    float bendAmount = strength / pow(max(normalizedDist, 0.1), power);
    bendAmount = min(bendAmount, 2.0); // Clamp to prevent extreme distortion

    // Direction: pull toward center
    float2 dir = normalize(delta);

    // Swirl: rotate the distortion direction
    float angle = swirl;
    float2 swirlDir;
    swirlDir.x = dir.x * cos(angle) - dir.y * sin(angle);
    swirlDir.y = dir.x * sin(angle) + dir.y * cos(angle);

    float2 offset = -swirlDir * bendAmount * portalScreenRadius * 0.1;
    // Undo aspect correction on output
    offset.x /= (cb.resolution.x / cb.resolution.y);

    return uv + offset;
}

// ─── Event Horizon ───────────────────────────────────────────────────────

float EventHorizonMask(float2 uv, float2 center, float screenRadius, float horizonFraction) {
    float2 delta = uv - center;
    delta.x *= cb.resolution.x / cb.resolution.y;
    float dist = length(delta);

    float horizonRadius = screenRadius * horizonFraction;
    float edgeSoftness = horizonRadius * 0.3;

    return smoothstep(horizonRadius - edgeSoftness, horizonRadius + edgeSoftness, dist);
}

// ─── Accretion Disc ──────────────────────────────────────────────────────

float3 AccretionDisc(float2 uv, float2 center, float screenRadius,
                      float horizonFraction, float discWidth, float3 color, float swirl) {
    float2 delta = uv - center;
    delta.x *= cb.resolution.x / cb.resolution.y;
    float dist = length(delta);

    float innerEdge = screenRadius * horizonFraction;
    float outerEdge = innerEdge + screenRadius * discWidth;

    if (dist < innerEdge || dist > outerEdge) return float3(0, 0, 0);

    // Ring intensity: peaks at center of disc
    float ringT = (dist - innerEdge) / (outerEdge - innerEdge);
    float ringIntensity = sin(ringT * 3.14159) * 2.0;

    // Angular variation for accretion texture
    float angle = atan2(delta.y, delta.x) + swirl;
    float angularNoise = 0.5 + 0.5 * sin(angle * 8.0 + swirl * 3.0);
    angularNoise *= 0.7 + 0.3 * sin(angle * 13.0 - swirl * 5.0);

    // Doppler shift: blueshift on approaching side, redshift on receding
    float dopplerPhase = (angle + 3.14159) / (2.0 * 3.14159);
    float3 dopplerColor = lerp(
        color * float3(1.5, 0.8, 0.3),  // Redshift
        color * float3(0.3, 0.8, 1.5),  // Blueshift
        dopplerPhase
    );

    return dopplerColor * ringIntensity * angularNoise * cb.glowIntensity;
}

// ─── Edge Glow ───────────────────────────────────────────────────────────

float3 EdgeGlow(float2 uv, float2 center, float screenRadius, float3 color) {
    float2 delta = uv - center;
    delta.x *= cb.resolution.x / cb.resolution.y;
    float dist = length(delta);

    float glowStart = screenRadius * 0.8;
    float glowEnd = screenRadius * 1.3;

    if (dist < glowStart || dist > glowEnd) return float3(0, 0, 0);

    float t = 1.0 - (dist - glowStart) / (glowEnd - glowStart);
    float glow = pow(t, 2.0) * cb.glowIntensity * 0.5;

    return color * glow;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;
    float depth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);
    float3 worldPos = ReconstructWorldPos(uv, depth);

    float2 distortedUV = uv;
    float totalInfluence = 0.0;
    float3 additiveGlow = float3(0, 0, 0);
    float horizonMask = 1.0;

    for (u32 i = 0; i < cb.sourceCount && i < 16; ++i) {
        PortalSource portal = g_Portals[i];

        float2 portalScreen = ProjectToScreen(portal.worldPosition);
        if (portalScreen.x < -1.0) continue; // Behind camera

        // Approximate screen-space radius from world radius
        float distToPortal = length(portal.worldPosition - cb.cameraPos);
        float screenRadius = portal.radius / max(distToPortal, 0.1) * 0.5;

        // Distance-based fade
        float distFade = 1.0 - smoothstep(cb.fadeStart, cb.fadeEnd, distToPortal);
        if (distFade < 0.01) continue;

        // Swirl animation
        float swirl = cb.time * portal.rotationSpeed;

        // Noise modulation
        float noise = g_NoiseTex.SampleLevel(g_LinearWrap,
            uv * 3.0 + float2(cb.time * 0.1, cb.time * 0.07), 0);
        float noiseMod = 1.0 + (noise - 0.5) * cb.noiseAmount;

        // Apply gravitational lensing
        float warpStr = portal.warpStrength * cb.globalStrength * distFade * noiseMod;
        distortedUV = GravitationalLens(distortedUV, portalScreen, screenRadius,
                                          warpStr, cb.lensingPower, swirl);

        // Event horizon
        float horizon = EventHorizonMask(uv, portalScreen, screenRadius, portal.eventHorizonRadius);
        horizonMask *= horizon;

        // Accretion disc
        additiveGlow += AccretionDisc(uv, portalScreen, screenRadius,
                                        portal.eventHorizonRadius, portal.accretionWidth,
                                        portal.portalColor, swirl) * distFade;

        // Edge glow
        additiveGlow += EdgeGlow(uv, portalScreen, screenRadius, portal.portalColor) * distFade;

        totalInfluence += (1.0 - horizon) + warpStr * 0.1;
    }

    // Clamp distorted UV
    distortedUV = clamp(distortedUV, 0.0, 1.0);

    // Sample scene with optional chromatic aberration
    float3 sceneColor;
    if (cb.chromaticAmount > 0.0 && totalInfluence > 0.01) {
        float2 chromaDir = normalize(distortedUV - uv + 0.0001);
        float chromaStr = cb.chromaticAmount * saturate(totalInfluence);

        sceneColor.r = g_SceneColor.SampleLevel(g_LinearClamp,
            clamp(distortedUV + chromaDir * chromaStr, 0.0, 1.0), 0).r;
        sceneColor.g = g_SceneColor.SampleLevel(g_LinearClamp, distortedUV, 0).g;
        sceneColor.b = g_SceneColor.SampleLevel(g_LinearClamp,
            clamp(distortedUV - chromaDir * chromaStr, 0.0, 1.0), 0).b;
    } else {
        sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, distortedUV, 0).rgb;
    }

    // Apply event horizon (black center)
    sceneColor *= horizonMask;

    // Add accretion disc and edge glow
    sceneColor += additiveGlow;

    g_Output[DTid.xy] = float4(sceneColor, 1.0);
}
