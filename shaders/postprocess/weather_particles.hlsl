// ─── Procedural Rain/Snow Weather Particle Overlay Shader ────────────────
// Screen-space post-process for weather particle effects including rain
// streaks, snowflakes, and mixed precipitation with wind simulation.
//
// Features:
//   - Procedural rain streaks (angled, motion-blurred)
//   - Procedural snowflakes (layered, varying size/speed)
//   - Wind direction and turbulence affecting particle trajectories
//   - Depth-aware occlusion (particles behind geometry hidden)
//   - Near-camera splash/impact effects for rain
//   - Accumulation: snow buildup on upward-facing normals
//   - Fog density increase during precipitation
//   - Configurable intensity, speed, wind, and particle size
//   - Multiple precipitation layers for parallax depth
//
// References:
//   - "Weather Effects in The Last of Us Part II" (Naughty Dog, GDC 2021)
//   - "Procedural Rain in Uncharted 4" (Naughty Dog, SIGGRAPH 2016)
//   - "Real-Time Snow Rendering" (Haglund, 2002)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor  : register(t0);
Texture2D<float>  g_SceneDepth  : register(t1);
Texture2D<float4> g_SceneNormal : register(t2);
Texture2D<float>  g_NoiseTex    : register(t3);

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct WeatherParticleConstants {
    float2   resolution;
    float2   invResolution;
    float    time;
    u32      weatherType;          // 0=rain, 1=snow, 2=mixed
    float    intensity;            // Particle density (0-1, default 0.5)
    float    particleSpeed;        // Fall speed multiplier (default 1.0)
    float2   windDirection;        // Normalized XY wind vector
    float    windStrength;         // Wind force (default 0.3)
    float    windTurbulence;       // Turbulence amount (default 0.2)
    float    particleSize;         // Base particle size (default 1.0)
    float    depthFadeStart;       // Depth where particles start fading (default 0.1)
    float    depthFadeEnd;         // Depth where particles fully visible (default 0.3)
    float    fogDensityBoost;      // Extra fog during weather (default 0.1)
    float    splashIntensity;      // Rain splash effect strength (default 0.5)
    float    snowAccumulation;     // Snow buildup on surfaces (default 0.0)
    u32      layerCount;           // Parallax layers (1-4, default 3)
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<WeatherParticleConstants> cb;

// ─── Hash Functions ──────────────────────────────────────────────────────

float Hash21(float2 p) {
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float2 Hash22(float2 p) {
    float3 a = frac(p.xyx * float3(123.34, 234.34, 345.65));
    a += dot(a, a + 34.45);
    return frac(float2(a.x * a.y, a.y * a.z));
}

float Noise21(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = Hash21(i);
    float b = Hash21(i + float2(1, 0));
    float c = Hash21(i + float2(0, 1));
    float d = Hash21(i + float2(1, 1));

    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// ─── Rain Streak ─────────────────────────────────────────────────────────

float RainStreak(float2 uv, float2 cellId, float layerSpeed, float layerSize) {
    float2 wind = cb.windDirection * cb.windStrength;
    float turbulence = sin(cb.time * 3.0 + cellId.x * 7.0) * cb.windTurbulence;

    // Rain falls downward with wind offset
    float2 fallDir = float2(wind.x + turbulence, -1.0 * cb.particleSpeed * layerSpeed);

    // Animated position
    float2 offset = fallDir * cb.time;
    float2 p = uv * layerSize + offset;

    // Grid cell
    float2 cell = floor(p);
    float2 local = frac(p) - 0.5;

    // Random per-cell
    float2 rnd = Hash22(cell + cellId);

    // Only some cells have rain
    if (rnd.x > cb.intensity) return 0.0;

    // Streak shape: thin vertical line with slight angle
    float2 streakCenter = (rnd - 0.5) * 0.4;
    float2 toCenter = local - streakCenter;

    // Rotate streak by wind angle
    float angle = atan2(fallDir.x, -fallDir.y);
    float cs = cos(angle);
    float sn = sin(angle);
    float2 rotated = float2(toCenter.x * cs - toCenter.y * sn,
                             toCenter.x * sn + toCenter.y * cs);

    // Thin streak
    float width = 0.015 * cb.particleSize;
    float length = 0.15 * cb.particleSize * layerSpeed;

    float streak = smoothstep(width, 0.0, abs(rotated.x)) *
                   smoothstep(length, 0.0, abs(rotated.y));

    return streak * rnd.y;
}

// ─── Snowflake ───────────────────────────────────────────────────────────

float Snowflake(float2 uv, float2 cellId, float layerSpeed, float layerSize) {
    float2 wind = cb.windDirection * cb.windStrength * 0.5;

    // Snow drifts slowly with gentle oscillation
    float drift = sin(cb.time * 1.5 + cellId.x * 5.0 + cellId.y * 3.0) * 0.3;
    float2 fallDir = float2(wind.x + drift * cb.windTurbulence,
                             -0.3 * cb.particleSpeed * layerSpeed);

    float2 offset = fallDir * cb.time;
    float2 p = uv * layerSize + offset;

    float2 cell = floor(p);
    float2 local = frac(p) - 0.5;

    float2 rnd = Hash22(cell + cellId);

    if (rnd.x > cb.intensity) return 0.0;

    float2 center = (rnd - 0.5) * 0.3;
    float dist = length(local - center);

    // Circular snowflake with soft edge
    float radius = 0.02 * cb.particleSize * (0.5 + rnd.y * 0.5);
    float flake = smoothstep(radius, radius * 0.3, dist);

    // Slight sparkle
    float sparkle = sin(cb.time * 8.0 + rnd.x * 100.0) * 0.3 + 0.7;

    return flake * sparkle;
}

// ─── Rain Splash ─────────────────────────────────────────────────────────

float RainSplash(float2 uv, float depth) {
    if (cb.splashIntensity <= 0.0 || depth > 0.95) return 0.0;

    float2 p = uv * 30.0;
    float2 cell = floor(p);
    float2 local = frac(p) - 0.5;

    float2 rnd = Hash22(cell + float2(cb.time * 2.0, 0));

    if (rnd.x > cb.intensity * 0.5) return 0.0;

    // Expanding ring
    float t = frac(cb.time * 3.0 + rnd.y * 10.0);
    float radius = t * 0.3;
    float ring = abs(length(local) - radius);
    float splash = smoothstep(0.02, 0.0, ring) * (1.0 - t);

    return splash * cb.splashIntensity;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float4 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0);
    float depth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);

    // ── Depth-based particle visibility ──────────────────────────────
    float depthMask = smoothstep(cb.depthFadeStart, cb.depthFadeEnd, depth);

    // ── Accumulate weather particles across layers ───────────────────
    float totalParticle = 0.0;
    u32 layers = clamp(cb.layerCount, 1u, 4u);

    for (u32 layer = 0; layer < layers; ++layer) {
        float layerDepth = float(layer + 1) / float(layers);
        float layerSpeed = 0.5 + layerDepth * 0.5;
        float layerSize = 20.0 + float(layer) * 15.0;
        float layerAlpha = 0.3 + layerDepth * 0.7;
        float2 layerId = float2(float(layer) * 100.0, float(layer) * 200.0);

        float particle = 0.0;

        if (cb.weatherType == 0 || cb.weatherType == 2) {
            // Rain
            particle += RainStreak(uv, layerId, layerSpeed, layerSize);
        }

        if (cb.weatherType == 1 || cb.weatherType == 2) {
            // Snow
            particle += Snowflake(uv, layerId + float2(50, 50), layerSpeed * 0.6, layerSize * 0.7);
        }

        totalParticle += particle * layerAlpha;
    }

    totalParticle *= depthMask;

    // ── Rain splash on surfaces ──────────────────────────────────────
    float splash = 0.0;
    if (cb.weatherType == 0 || cb.weatherType == 2) {
        splash = RainSplash(uv, depth);
    }

    // ── Snow accumulation on upward-facing surfaces ──────────────────
    float3 accumColor = float3(0, 0, 0);
    if (cb.snowAccumulation > 0.0 && (cb.weatherType == 1 || cb.weatherType == 2)) {
        float3 normal = g_SceneNormal.SampleLevel(g_LinearClamp, uv, 0).xyz * 2.0 - 1.0;
        float upFacing = saturate(dot(normal, float3(0, 1, 0)));
        float snowMask = upFacing * cb.snowAccumulation * (1.0 - depth * 0.5);
        accumColor = float3(0.9, 0.92, 0.95) * snowMask;
    }

    // ── Weather fog boost ────────────────────────────────────────────
    float fogBoost = (1.0 - depth) * cb.fogDensityBoost;
    float3 fogColor = float3(0.6, 0.65, 0.7);

    // ── Composite ────────────────────────────────────────────────────
    float3 particleColor = (cb.weatherType == 1)
        ? float3(0.95, 0.97, 1.0)   // Snow: white-blue
        : float3(0.7, 0.75, 0.85);  // Rain: grey-blue

    float3 result = sceneColor.rgb;
    result = lerp(result, fogColor, fogBoost);
    result += accumColor;
    result += particleColor * totalParticle * 0.5;
    result += float3(1, 1, 1) * splash * 0.3;

    g_Output[DTid.xy] = float4(result, 1.0);
}
