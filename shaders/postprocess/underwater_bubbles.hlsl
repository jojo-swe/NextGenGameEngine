// ─── Procedural Underwater Bubble / Caustics Overlay Shader ──────────────
// Screen-space post-process for underwater environments with rising
// bubbles, animated caustic patterns, depth fog, and light shafts.
//
// Features:
//   - Procedural rising bubbles (spherical + refraction highlight)
//   - Animated caustic pattern overlay (dual-layer Voronoi)
//   - Depth-based underwater fog with absorption color
//   - Light shaft god rays from water surface
//   - Bubble size variation and clustering
//   - Screen-space distortion from water movement
//   - Configurable water depth, color, and clarity
//
// References:
//   - "Underwater Rendering in Subnautica" (Unknown Worlds, GDC 2019)
//   - "Procedural Caustics" (Inigo Quilez, 2015)
//   - "Real-Time Underwater Effects" (GPU Gems 2, Ch. 2)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor : register(t0);
Texture2D<float>  g_SceneDepth : register(t1);
Texture2D<float>  g_NoiseTex   : register(t2);

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct UnderwaterConstants {
    float2   resolution;
    float2   invResolution;
    float3   cameraPos;
    float    time;
    float3   waterColor;         // Absorption tint (default: 0.05, 0.2, 0.35)
    float    waterDepth;         // Max fog distance (default 50.0)
    float3   causticsColor;      // Caustic light color (default: 0.8, 0.9, 1.0)
    float    causticsIntensity;  // Caustic brightness (default 1.5)
    float    causticsScale;      // UV scale for caustic pattern (default 8.0)
    float    causticsSpeed;      // Animation speed (default 1.0)
    float    bubbleDensity;      // Bubbles per screen area (default 0.3)
    float    bubbleMinSize;      // Min bubble radius in UV (default 0.003)
    float    bubbleMaxSize;      // Max bubble radius in UV (default 0.015)
    float    bubbleRiseSpeed;    // Rise speed (default 0.5)
    float    distortionAmount;   // Screen-space water distortion (default 0.003)
    float    fogDensity;         // Exponential fog density (default 0.04)
    float3   lightDir;           // Direction to surface light
    float    godRayIntensity;    // Light shaft brightness (default 0.5)
    float    godRayDecay;        // Shaft falloff (default 0.95)
    float    clarity;            // Water clarity 0=murky, 1=clear (default 0.5)
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<UnderwaterConstants> cb;

// ─── Hash Functions ──────────────────────────────────────────────────────

float Hash21(float2 p) {
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float2 Hash22(float2 p) {
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.xx + p3.yz) * p3.zy);
}

// ─── Voronoi Caustics ────────────────────────────────────────────────────

float VoronoiCaustics(float2 uv, float timeOffset) {
    float2 p = uv * cb.causticsScale;
    float2 ip = floor(p);
    float2 fp = frac(p);

    float minDist = 1.0;
    float secondDist = 1.0;

    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float2 neighbor = float2(float(x), float(y));
            float2 cellId = ip + neighbor;

            // Animated cell center
            float2 cellHash = Hash22(cellId);
            float2 cellCenter = neighbor + cellHash;
            cellCenter.x += sin(cb.time * cb.causticsSpeed * 0.7 + cellHash.x * 6.28) * 0.3;
            cellCenter.y += cos(cb.time * cb.causticsSpeed * 0.9 + cellHash.y * 6.28) * 0.3;

            float dist = length(fp - cellCenter);

            if (dist < minDist) {
                secondDist = minDist;
                minDist = dist;
            } else if (dist < secondDist) {
                secondDist = dist;
            }
        }
    }

    // Edge detection: caustic lines at Voronoi edges
    float edge = secondDist - minDist;
    float caustic = smoothstep(0.0, 0.15, edge);
    caustic = 1.0 - caustic;
    caustic = pow(caustic, 3.0);

    return caustic;
}

float DualLayerCaustics(float2 uv) {
    float c1 = VoronoiCaustics(uv, 0.0);
    float c2 = VoronoiCaustics(uv * 1.3 + float2(0.7, 0.3), 2.0);

    // Combine layers with different weights
    return c1 * 0.6 + c2 * 0.4;
}

// ─── Procedural Bubbles ──────────────────────────────────────────────────

struct BubbleResult {
    float alpha;
    float3 color;
};

BubbleResult RenderBubbles(float2 uv) {
    BubbleResult result;
    result.alpha = 0.0;
    result.color = float3(0, 0, 0);

    // Grid of bubble columns
    float columnCount = 30.0;
    float columnWidth = 1.0 / columnCount;

    for (float col = 0.0; col < columnCount; col += 1.0) {
        float colCenter = (col + 0.5) * columnWidth;
        float colSeed = Hash21(float2(col, 0.0));

        if (colSeed > cb.bubbleDensity) continue;

        // Multiple bubbles per column at different phases
        for (float b = 0.0; b < 3.0; b += 1.0) {
            float bubbleSeed = Hash21(float2(col, b + 1.0));
            float bubbleSize = lerp(cb.bubbleMinSize, cb.bubbleMaxSize, bubbleSeed);

            // Rising animation
            float phase = frac(cb.time * cb.bubbleRiseSpeed * (0.5 + bubbleSeed) + bubbleSeed * 10.0);
            float bubbleY = 1.0 - phase; // Rise from bottom to top

            // Horizontal wobble
            float wobble = sin(cb.time * 3.0 + bubbleSeed * 20.0 + phase * 10.0) * 0.01;
            float bubbleX = colCenter + wobble;

            float2 bubbleCenter = float2(bubbleX, bubbleY);
            float2 delta = uv - bubbleCenter;
            // Aspect correction
            delta.x *= cb.resolution.x / cb.resolution.y;

            float dist = length(delta);

            if (dist < bubbleSize) {
                float t = dist / bubbleSize;

                // Bubble sphere shading
                float rim = smoothstep(0.6, 1.0, t); // Rim highlight
                float highlight = smoothstep(0.7, 0.0, length(delta - float2(-bubbleSize * 0.3, -bubbleSize * 0.3) / (cb.resolution.x / cb.resolution.y)));

                float3 bubbleCol = float3(0.7, 0.85, 1.0) * (1.0 - t * 0.5);
                bubbleCol += float3(1.0, 1.0, 1.0) * highlight * 0.8; // Specular highlight
                bubbleCol += float3(0.5, 0.7, 1.0) * rim * 0.3;      // Rim light

                float alpha = smoothstep(1.0, 0.8, t) * 0.3;

                // Fade at top and bottom of screen
                alpha *= smoothstep(0.0, 0.1, bubbleY) * smoothstep(1.0, 0.9, bubbleY);

                result.color += bubbleCol * alpha;
                result.alpha = max(result.alpha, alpha);
            }
        }
    }

    return result;
}

// ─── God Rays ────────────────────────────────────────────────────────────

float GodRays(float2 uv) {
    // Simplified screen-space light shafts from above
    float2 lightScreenPos = float2(0.5, 0.0); // Light from top center

    float2 dir = uv - lightScreenPos;
    float dist = length(dir);

    float rays = 0.0;
    float2 sampleUV = uv;
    float weight = 1.0;

    for (int i = 0; i < 8; ++i) {
        sampleUV -= dir * 0.02;
        float noise = g_NoiseTex.SampleLevel(g_LinearWrap,
            sampleUV * 2.0 + float2(cb.time * 0.05, 0), 0);
        rays += noise * weight;
        weight *= cb.godRayDecay;
    }

    rays /= 8.0;
    rays *= smoothstep(1.0, 0.0, dist); // Fade with distance from light

    return rays * cb.godRayIntensity;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    // Water distortion
    float2 distortedUV = uv;
    float distortNoise1 = g_NoiseTex.SampleLevel(g_LinearWrap,
        uv * 5.0 + float2(cb.time * 0.1, cb.time * 0.07), 0);
    float distortNoise2 = g_NoiseTex.SampleLevel(g_LinearWrap,
        uv * 7.0 + float2(-cb.time * 0.08, cb.time * 0.12), 0);
    distortedUV += (float2(distortNoise1, distortNoise2) - 0.5) * cb.distortionAmount;
    distortedUV = clamp(distortedUV, 0.0, 1.0);

    float4 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, distortedUV, 0);
    float depth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);

    // Linear depth approximation for fog
    float linearDepth = depth; // In a real engine: linearize from projection matrix

    // ── Underwater fog ───────────────────────────────────────────────
    float fogFactor = 1.0 - exp(-linearDepth * cb.fogDensity * cb.waterDepth);
    fogFactor = lerp(fogFactor, fogFactor * 0.3, cb.clarity); // Clarity reduces fog

    float3 fogColor = cb.waterColor;
    float3 fogged = lerp(sceneColor.rgb, fogColor, saturate(fogFactor));

    // ── Caustics ─────────────────────────────────────────────────────
    float caustics = DualLayerCaustics(uv);

    // Caustics are stronger on surfaces (not in deep water)
    float causticsStrength = (1.0 - fogFactor) * cb.causticsIntensity;
    float3 causticsContrib = cb.causticsColor * caustics * causticsStrength;

    fogged += causticsContrib;

    // ── God rays ─────────────────────────────────────────────────────
    float rays = GodRays(uv);
    fogged += cb.causticsColor * rays * (1.0 - fogFactor * 0.5);

    // ── Bubbles ──────────────────────────────────────────────────────
    BubbleResult bubbles = RenderBubbles(uv);
    fogged = lerp(fogged, bubbles.color, bubbles.alpha);
    fogged += bubbles.color * 0.5; // Additive glow

    // ── Color grading: slight blue-green tint ────────────────────────
    fogged *= float3(0.85, 0.95, 1.0);

    // ── Vignette (darker at edges, simulating light from above) ──────
    float2 vignetteUV = uv * 2.0 - 1.0;
    float vignette = 1.0 - dot(vignetteUV, vignetteUV) * 0.3;
    // Stronger darkening at bottom
    vignette -= (1.0 - uv.y) * 0.15;
    fogged *= saturate(vignette);

    g_Output[DTid.xy] = float4(fogged, 1.0);
}
