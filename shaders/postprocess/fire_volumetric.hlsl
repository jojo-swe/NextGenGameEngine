// ─── Procedural Fire/Flame Volumetric Shader ─────────────────────────────
// Screen-space ray-marched volumetric fire with procedural noise-driven
// flame shapes, ember particles, and heat distortion.
//
// Features:
//   - Ray-marched volumetric density field from FBM noise
//   - Temperature-based blackbody color ramp
//   - Flickering flame tip animation
//   - Ember particle spawning along flame edges
//   - Heat haze distortion above flame
//   - Absorption and emission model (Beer-Lambert + emission)
//   - Multiple fire shapes: campfire, torch, explosion burst
//
// References:
//   - "Real-Time Volumetric Rendering" (Wrenninge, SIGGRAPH 2011)
//   - "Physically Based Sky, Atmosphere and Cloud Rendering" (Hillaire, 2020)
//   - "GPU Pro 7: Real-Time Volumetric Cloudscapes" (adapted for fire)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_SceneColor  : register(t0);
Texture2D<float>  g_SceneDepth  : register(t1);
Texture2D<float>  g_NoiseTex    : register(t2);

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

RWTexture2D<float4> g_Output : register(u0);

struct FireConstants {
    float4x4 invViewProj;
    float2   resolution;
    float2   invResolution;
    float3   cameraPos;
    float    time;
    float3   firePosition;       // World-space fire center
    float    fireRadius;         // Horizontal extent (default 0.5)
    float    fireHeight;         // Vertical extent (default 2.0)
    float    density;            // Flame density multiplier (default 3.0)
    float    noiseScale;         // FBM frequency (default 2.0)
    float    noiseSpeed;         // Animation speed (default 3.0)
    float    turbulence;         // Turbulence amount (default 1.5)
    float    temperatureBase;    // Base temperature K (default 1200)
    float    temperatureTip;     // Tip temperature K (default 800)
    float    absorptionCoeff;    // Beer-Lambert absorption (default 2.0)
    float    emissionStrength;   // Emission multiplier (default 5.0)
    float    flickerSpeed;       // Flicker frequency (default 8.0)
    float    flickerAmount;      // Flicker intensity (default 0.3)
    u32      raySteps;           // March steps (default 48)
    float    heatHazeStrength;   // Distortion above flame (default 0.01)
    float    emberDensity;       // Ember particle density (default 0.5)
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<FireConstants> cb;

// ─── Noise Functions ─────────────────────────────────────────────────────

float Hash3D(float3 p) {
    p = frac(p * float3(443.8975, 397.2973, 491.1871));
    p += dot(p, p.yxz + 19.19);
    return frac((p.x + p.y) * p.z);
}

float ValueNoise3D(float3 p) {
    float3 ip = floor(p);
    float3 fp = frac(p);
    fp = fp * fp * (3.0 - 2.0 * fp);

    float n000 = Hash3D(ip);
    float n100 = Hash3D(ip + float3(1, 0, 0));
    float n010 = Hash3D(ip + float3(0, 1, 0));
    float n110 = Hash3D(ip + float3(1, 1, 0));
    float n001 = Hash3D(ip + float3(0, 0, 1));
    float n101 = Hash3D(ip + float3(1, 0, 1));
    float n011 = Hash3D(ip + float3(0, 1, 1));
    float n111 = Hash3D(ip + float3(1, 1, 1));

    float nx00 = lerp(n000, n100, fp.x);
    float nx10 = lerp(n010, n110, fp.x);
    float nx01 = lerp(n001, n101, fp.x);
    float nx11 = lerp(n011, n111, fp.x);

    float nxy0 = lerp(nx00, nx10, fp.y);
    float nxy1 = lerp(nx01, nx11, fp.y);

    return lerp(nxy0, nxy1, fp.z);
}

float FBM(float3 p, uint octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    float frequency = 1.0;

    for (uint i = 0; i < octaves; ++i) {
        value += ValueNoise3D(p * frequency) * amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return value;
}

// ─── Blackbody Color Ramp ────────────────────────────────────────────────
// Approximate blackbody radiation for temperature in Kelvin.

float3 BlackbodyColor(float temperature) {
    // Simplified Planckian locus approximation
    float t = temperature / 1000.0;

    float3 color;
    // Red channel
    if (t < 1.0) color.r = 0.0;
    else if (t < 6.6) color.r = 1.0;
    else color.r = saturate(1.292 * pow(t - 0.6, -0.1332));

    // Green channel
    if (t < 1.0) color.g = 0.0;
    else if (t < 6.6) color.g = saturate(0.39 * log(t) - 0.2);
    else color.g = saturate(1.13 * pow(t - 0.6, -0.0755));

    // Blue channel
    if (t < 2.0) color.b = 0.0;
    else if (t < 6.6) color.b = saturate(0.543 * log(t - 1.0) - 0.55);
    else color.b = 1.0;

    return color;
}

// ─── Fire Density Field ──────────────────────────────────────────────────

float FireDensity(float3 worldPos) {
    float3 localPos = worldPos - cb.firePosition;

    // Normalize to fire volume
    float2 horizontalDist = localPos.xz / cb.fireRadius;
    float height = localPos.y / cb.fireHeight;

    // Outside bounding cylinder
    float radialDist = length(horizontalDist);
    if (radialDist > 1.5 || height < -0.1 || height > 1.3) return 0.0;

    // Cone shape: narrows toward top
    float coneRadius = lerp(1.0, 0.1, saturate(height));
    float coneDist = radialDist / max(coneRadius, 0.01);
    if (coneDist > 1.0) return 0.0;

    // Base density from cone falloff
    float baseDensity = smoothstep(1.0, 0.3, coneDist);

    // Vertical falloff (strongest at base, fading at tip)
    float verticalFalloff = smoothstep(1.2, 0.0, height) * smoothstep(-0.1, 0.1, height);

    // Animated noise for flame shape
    float3 noisePos = localPos * cb.noiseScale;
    noisePos.y -= cb.time * cb.noiseSpeed; // Upward motion
    noisePos.xz += sin(cb.time * 0.7) * 0.2; // Sway

    float noise = FBM(noisePos, 4);

    // Turbulence: more at tips
    float turbNoise = FBM(noisePos * 2.0 + float3(0, cb.time * cb.noiseSpeed * 1.5, 0), 3);
    noise += turbNoise * cb.turbulence * height;

    // Flicker
    float flicker = 1.0 + sin(cb.time * cb.flickerSpeed) * cb.flickerAmount;
    flicker *= 1.0 + sin(cb.time * cb.flickerSpeed * 1.7 + 2.0) * cb.flickerAmount * 0.5;

    float density = baseDensity * verticalFalloff * noise * flicker * cb.density;
    return max(density, 0.0);
}

// ─── Fire Temperature ────────────────────────────────────────────────────

float FireTemperature(float3 worldPos, float density) {
    float3 localPos = worldPos - cb.firePosition;
    float height = saturate(localPos.y / cb.fireHeight);

    // Hotter at base, cooler at tip
    float temp = lerp(cb.temperatureBase, cb.temperatureTip, height);

    // Higher density = slightly hotter
    temp += density * 200.0;

    return temp;
}

// ─── Ember Particles ─────────────────────────────────────────────────────

float EmberField(float3 worldPos) {
    float3 localPos = worldPos - cb.firePosition;
    float height = localPos.y / cb.fireHeight;

    // Embers exist above mid-flame
    if (height < 0.3 || height > 2.0) return 0.0;

    // Animated noise for particle positions
    float3 emberPos = localPos * 8.0;
    emberPos.y -= cb.time * 4.0; // Rise faster than flame

    float ember = ValueNoise3D(emberPos);
    ember = smoothstep(0.92, 0.98, ember); // Sparse bright points

    float heightFade = smoothstep(0.3, 0.5, height) * smoothstep(2.0, 1.5, height);
    return ember * heightFade * cb.emberDensity;
}

// ─── Ray-AABB Intersection ───────────────────────────────────────────────

bool IntersectFireVolume(float3 rayOrigin, float3 rayDir, out float tNear, out float tFar) {
    float3 bmin = cb.firePosition - float3(cb.fireRadius * 1.5, 0.1, cb.fireRadius * 1.5);
    float3 bmax = cb.firePosition + float3(cb.fireRadius * 1.5, cb.fireHeight * 1.3, cb.fireRadius * 1.5);

    float3 invDir = 1.0 / rayDir;
    float3 t0 = (bmin - rayOrigin) * invDir;
    float3 t1 = (bmax - rayOrigin) * invDir;

    float3 tmin = min(t0, t1);
    float3 tmax = max(t0, t1);

    tNear = max(max(tmin.x, tmin.y), tmin.z);
    tFar = min(min(tmax.x, tmax.y), tmax.z);

    tNear = max(tNear, 0.0);
    return tNear < tFar;
}

// ─── Main Compute Shader ─────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    float3 sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, uv, 0).rgb;
    float sceneDepth = g_SceneDepth.SampleLevel(g_LinearClamp, uv, 0);

    // Reconstruct ray
    float4 clipNear = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    float4 clipFar = float4(uv * 2.0 - 1.0, 1.0, 1.0);
    clipNear.y = -clipNear.y;
    clipFar.y = -clipFar.y;

    float4 worldNear = mul(cb.invViewProj, clipNear);
    float4 worldFar = mul(cb.invViewProj, clipFar);
    worldNear.xyz /= worldNear.w;
    worldFar.xyz /= worldFar.w;

    float3 rayOrigin = cb.cameraPos;
    float3 rayDir = normalize(worldFar.xyz - worldNear.xyz);

    // Intersect fire bounding volume
    float tNear, tFar;
    if (!IntersectFireVolume(rayOrigin, rayDir, tNear, tFar)) {
        // Heat haze above fire (distortion even outside volume)
        float3 aboveFireDir = cb.firePosition + float3(0, cb.fireHeight * 1.5, 0) - cb.cameraPos;
        float dotUp = dot(normalize(aboveFireDir), rayDir);
        if (dotUp > 0.95) {
            float haze = (dotUp - 0.95) * 20.0;
            float noise = g_NoiseTex.SampleLevel(g_LinearWrap, uv * 10.0 + cb.time * 0.5, 0);
            float2 offset = float2(noise - 0.5, noise - 0.5) * cb.heatHazeStrength * haze;
            sceneColor = g_SceneColor.SampleLevel(g_LinearClamp, clamp(uv + offset, 0.0, 1.0), 0).rgb;
        }

        g_Output[DTid.xy] = float4(sceneColor, 1.0);
        return;
    }

    // Clamp to scene depth
    float maxDist = length(worldFar.xyz - worldNear.xyz) * sceneDepth;
    tFar = min(tFar, maxDist);

    // Ray march through fire volume
    float stepSize = (tFar - tNear) / float(cb.raySteps);
    float transmittance = 1.0;
    float3 accumulatedLight = float3(0, 0, 0);

    for (u32 i = 0; i < cb.raySteps; ++i) {
        if (transmittance < 0.01) break;

        float t = tNear + (float(i) + 0.5) * stepSize;
        float3 samplePos = rayOrigin + rayDir * t;

        // Sample fire density
        float density = FireDensity(samplePos);

        if (density > 0.001) {
            // Temperature and color
            float temp = FireTemperature(samplePos, density);
            float3 fireColor = BlackbodyColor(temp);

            // Beer-Lambert absorption
            float absorption = exp(-density * cb.absorptionCoeff * stepSize);

            // Emission (fire emits light)
            float3 emission = fireColor * density * cb.emissionStrength * stepSize;

            accumulatedLight += emission * transmittance;
            transmittance *= absorption;
        }

        // Ember particles
        float ember = EmberField(samplePos);
        if (ember > 0.0) {
            float3 emberColor = BlackbodyColor(1500.0) * ember * 3.0 * stepSize;
            accumulatedLight += emberColor * transmittance;
        }
    }

    // Composite: scene behind fire attenuated + fire emission
    float3 finalColor = sceneColor * transmittance + accumulatedLight;

    // Heat haze distortion near fire
    float distToFire = length(cb.firePosition - cb.cameraPos);
    float hazeFactor = saturate(1.0 - (tNear / max(distToFire, 0.1)));
    float noise = g_NoiseTex.SampleLevel(g_LinearWrap, uv * 8.0 + cb.time * 0.3, 0);
    float2 hazeOffset = float2(noise - 0.5, noise - 0.5) * cb.heatHazeStrength * hazeFactor * (1.0 - transmittance);
    float3 hazedScene = g_SceneColor.SampleLevel(g_LinearClamp, clamp(uv + hazeOffset, 0.0, 1.0), 0).rgb;
    finalColor = lerp(finalColor, hazedScene * transmittance + accumulatedLight, 0.3);

    g_Output[DTid.xy] = float4(finalColor, 1.0);
}
