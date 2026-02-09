// ─── Atmospheric Scattering LUT Precompute ───────────────────────────────
// Precomputes lookup tables for physically-based atmospheric scattering.
// Three LUTs are generated:
//   1. Transmittance LUT (256x64): optical depth along view ray
//   2. Multi-scattering LUT (32x32): multi-scatter contribution
//   3. Sky-View LUT (192x108): final sky radiance for given view
//
// Based on:
//   - "A Scalable and Production Ready Sky and Atmosphere Rendering Technique"
//     (Hillaire, EGSR 2020 — Unreal Engine 5 sky model)
//   - Bruneton & Neyret 2008 precomputed atmospheric scattering
//
// All units in kilometers. Earth radius = 6360 km, atmosphere top = 6460 km.

#include "../common/math.hlsl"

// ─── Atmosphere Parameters ───────────────────────────────────────────────

struct AtmosphereParams {
    float3 rayleighScattering;   // Rayleigh scattering coefficients (5.802e-6, 13.558e-6, 33.1e-6) per km
    float  rayleighDensityH;     // Rayleigh scale height (8.0 km)
    float3 mieScattering;        // Mie scattering coefficient (3.996e-6) per km
    float  mieDensityH;          // Mie scale height (1.2 km)
    float3 mieAbsorption;        // Mie absorption (4.4e-6) per km
    float  miePhaseG;            // Mie phase function asymmetry (0.8)
    float3 ozoneAbsorption;      // Ozone absorption (0.65e-6, 1.881e-6, 0.085e-6) per km
    float  ozoneLayerCenter;     // Ozone layer center altitude (25 km)
    float  ozoneLayerWidth;      // Ozone layer half-width (15 km)
    float  planetRadius;         // 6360 km
    float  atmosphereRadius;     // 6460 km
    float  pad0;
    float3 sunDirection;
    float  sunIntensity;         // Solar illuminance (default 1.0)
    float3 groundAlbedo;
    float  pad1;
};

[[vk::push_constant]] ConstantBuffer<AtmosphereParams> atm;

// ─── LUT Outputs ─────────────────────────────────────────────────────────

RWTexture2D<float4> g_TransmittanceLUT   : register(u0); // 256x64
RWTexture2D<float4> g_MultiScatterLUT    : register(u1); // 32x32
RWTexture2D<float4> g_SkyViewLUT         : register(u2); // 192x108

// ─── Ray-Sphere Intersection ─────────────────────────────────────────────

// Returns distance to intersection (negative if no hit)
float RaySphereIntersect(float3 origin, float3 dir, float3 center, float radius) {
    float3 oc = origin - center;
    float b = dot(oc, dir);
    float c = dot(oc, oc) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0) return -1.0;
    float sqrtD = sqrt(discriminant);
    float t0 = -b - sqrtD;
    float t1 = -b + sqrtD;
    return t0 > 0.0 ? t0 : t1;
}

float RaySphereIntersectNearest(float3 origin, float3 dir, float radius) {
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0) return -1.0;
    return -b + sqrt(discriminant);
}

// ─── Density Functions ───────────────────────────────────────────────────

float RayleighDensity(float altitude) {
    return exp(-altitude / atm.rayleighDensityH);
}

float MieDensity(float altitude) {
    return exp(-altitude / atm.mieDensityH);
}

float OzoneDensity(float altitude) {
    return max(0.0, 1.0 - abs(altitude - atm.ozoneLayerCenter) / atm.ozoneLayerWidth);
}

// ─── Extinction Coefficient ──────────────────────────────────────────────

float3 GetExtinction(float altitude) {
    float rayleigh = RayleighDensity(altitude);
    float mie = MieDensity(altitude);
    float ozone = OzoneDensity(altitude);

    float3 rayleighExt = atm.rayleighScattering * rayleigh;
    float3 mieExt = (atm.mieScattering + atm.mieAbsorption) * mie;
    float3 ozoneExt = atm.ozoneAbsorption * ozone;

    return rayleighExt + mieExt + ozoneExt;
}

float3 GetScattering(float altitude) {
    float rayleigh = RayleighDensity(altitude);
    float mie = MieDensity(altitude);

    return atm.rayleighScattering * rayleigh + atm.mieScattering * mie;
}

// ─── Phase Functions ─────────────────────────────────────────────────────

float RayleighPhase(float cosTheta) {
    return (3.0 / (16.0 * PI)) * (1.0 + cosTheta * cosTheta);
}

float HenyeyGreensteinPhase(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * denom * sqrt(denom));
}

// ─── Transmittance LUT (256x64) ─────────────────────────────────────────
// Parameterized by:
//   x: cos(zenith angle) mapped to [0,1]
//   y: altitude mapped to [0, atmosphereHeight]
// Stores optical depth from point to atmosphere top.

static const uint TRANSMITTANCE_W = 256;
static const uint TRANSMITTANCE_H = 64;
static const uint TRANSMITTANCE_STEPS = 40;

float2 TransmittanceLUTParamsToUV(float altitude, float cosZenith) {
    float H = sqrt(max(0.0, atm.atmosphereRadius * atm.atmosphereRadius -
                             atm.planetRadius * atm.planetRadius));
    float rho = sqrt(max(0.0, (atm.planetRadius + altitude) * (atm.planetRadius + altitude) -
                              atm.planetRadius * atm.planetRadius));

    float discriminant = (atm.planetRadius + altitude) * (atm.planetRadius + altitude) *
                         (cosZenith * cosZenith - 1.0) + atm.atmosphereRadius * atm.atmosphereRadius;
    float d = max(0.0, sqrt(discriminant) - (atm.planetRadius + altitude) * cosZenith);

    float dMin = atm.atmosphereRadius - atm.planetRadius - altitude;
    float dMax = rho + H;

    float u = (d - dMin) / max(dMax - dMin, 0.0001);
    float v = rho / max(H, 0.0001);

    return float2(u, v);
}

void TransmittanceLUTUVToParams(float2 uv, out float altitude, out float cosZenith) {
    float H = sqrt(max(0.0, atm.atmosphereRadius * atm.atmosphereRadius -
                             atm.planetRadius * atm.planetRadius));
    float rho = H * uv.y;
    altitude = sqrt(rho * rho + atm.planetRadius * atm.planetRadius) - atm.planetRadius;

    float dMin = atm.atmosphereRadius - atm.planetRadius - altitude;
    float dMax = rho + H;
    float d = dMin + uv.x * (dMax - dMin);

    float r = atm.planetRadius + altitude;
    cosZenith = (d == 0.0) ? 1.0 : (H * H - rho * rho - d * d) / (2.0 * r * d);
    cosZenith = clamp(cosZenith, -1.0, 1.0);
}

float3 ComputeTransmittance(float altitude, float cosZenith) {
    float3 origin = float3(0, atm.planetRadius + altitude, 0);
    float3 dir = float3(sqrt(max(0.0, 1.0 - cosZenith * cosZenith)), cosZenith, 0);

    float tMax = RaySphereIntersectNearest(origin, dir, atm.atmosphereRadius);
    if (tMax < 0.0) return float3(1, 1, 1);

    float dt = tMax / float(TRANSMITTANCE_STEPS);
    float3 opticalDepth = float3(0, 0, 0);

    for (uint i = 0; i < TRANSMITTANCE_STEPS; ++i) {
        float t = (float(i) + 0.5) * dt;
        float3 pos = origin + dir * t;
        float h = length(pos) - atm.planetRadius;
        opticalDepth += GetExtinction(h) * dt;
    }

    return exp(-opticalDepth);
}

[numthreads(8, 8, 1)]
void CSTransmittanceLUT(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= TRANSMITTANCE_W || DTid.y >= TRANSMITTANCE_H) return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(TRANSMITTANCE_W, TRANSMITTANCE_H);

    float altitude, cosZenith;
    TransmittanceLUTUVToParams(uv, altitude, cosZenith);

    float3 transmittance = ComputeTransmittance(altitude, cosZenith);
    g_TransmittanceLUT[DTid.xy] = float4(transmittance, 1.0);
}

// ─── Multi-Scattering LUT (32x32) ───────────────────────────────────────
// Approximates the contribution of 2nd+ order scattering using
// the isotropic phase function approximation (Hillaire 2020).
// Parameterized by altitude and sun zenith angle.

static const uint MULTISCATTER_W = 32;
static const uint MULTISCATTER_H = 32;
static const uint MULTISCATTER_DIR_SAMPLES = 64;
static const uint MULTISCATTER_STEPS = 20;

Texture2D<float4> g_TransmittanceLUTRead : register(t0);
SamplerState g_LinearClamp : register(s0);

float3 SampleTransmittanceLUT(float altitude, float cosZenith) {
    float2 uv = TransmittanceLUTParamsToUV(altitude, cosZenith);
    return g_TransmittanceLUTRead.SampleLevel(g_LinearClamp, uv, 0).rgb;
}

[numthreads(8, 8, 1)]
void CSMultiScatterLUT(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= MULTISCATTER_W || DTid.y >= MULTISCATTER_H) return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(MULTISCATTER_W, MULTISCATTER_H);

    float altitude = uv.y * (atm.atmosphereRadius - atm.planetRadius);
    float sunCosZenith = uv.x * 2.0 - 1.0;

    float3 origin = float3(0, atm.planetRadius + altitude, 0);

    // Integrate over sphere of directions
    float3 luminanceSum = float3(0, 0, 0);
    float3 fmsSum = float3(0, 0, 0); // Multi-scatter factor

    float sqrtSamples = sqrt(float(MULTISCATTER_DIR_SAMPLES));

    for (uint i = 0; i < MULTISCATTER_DIR_SAMPLES; ++i) {
        // Uniform sphere sampling (Fibonacci)
        float theta = acos(1.0 - 2.0 * (float(i) + 0.5) / float(MULTISCATTER_DIR_SAMPLES));
        float phi = 2.0 * PI * (float(i) * 1.618033988);
        float3 dir = float3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi));

        float tMax = RaySphereIntersectNearest(origin, dir, atm.atmosphereRadius);
        float tGround = RaySphereIntersect(origin, dir, float3(0, 0, 0), atm.planetRadius);

        bool hitGround = tGround > 0.0 && tGround < tMax;
        if (hitGround) tMax = tGround;

        float dt = tMax / float(MULTISCATTER_STEPS);
        float3 throughput = float3(1, 1, 1);
        float3 inScatter = float3(0, 0, 0);
        float3 multiScatterFactor = float3(0, 0, 0);

        for (uint s = 0; s < MULTISCATTER_STEPS; ++s) {
            float t = (float(s) + 0.5) * dt;
            float3 pos = origin + dir * t;
            float h = length(pos) - atm.planetRadius;
            h = max(h, 0.0);

            float3 extinction = GetExtinction(h);
            float3 scattering = GetScattering(h);
            float3 sampleTransmittance = exp(-extinction * dt);

            // Sun transmittance at this point
            float3 sunDir = float3(sqrt(max(0.0, 1.0 - sunCosZenith * sunCosZenith)), sunCosZenith, 0);
            float sunCosZ = dot(normalize(pos), sunDir);
            float3 sunTransmittance = SampleTransmittanceLUT(h, sunCosZ);

            float3 scatteringIntegral = (scattering - scattering * sampleTransmittance) /
                                         max(extinction, float3(1e-7, 1e-7, 1e-7));

            inScatter += throughput * scatteringIntegral * sunTransmittance * atm.sunIntensity;
            multiScatterFactor += throughput * scatteringIntegral;

            throughput *= sampleTransmittance;
        }

        // Ground contribution
        if (hitGround) {
            float3 groundPos = origin + dir * tMax;
            float3 sunDir = float3(sqrt(max(0.0, 1.0 - sunCosZenith * sunCosZenith)), sunCosZenith, 0);
            float groundSunCosZ = dot(normalize(groundPos), sunDir);
            float3 groundSunTransmittance = SampleTransmittanceLUT(0.0, groundSunCosZ);
            inScatter += throughput * atm.groundAlbedo * max(groundSunCosZ, 0.0) *
                         groundSunTransmittance * atm.sunIntensity / PI;
        }

        // Isotropic phase (1/4π)
        float isotropicPhase = 1.0 / (4.0 * PI);
        luminanceSum += inScatter * isotropicPhase;
        fmsSum += multiScatterFactor * isotropicPhase;
    }

    // Average over all directions
    luminanceSum /= float(MULTISCATTER_DIR_SAMPLES);
    fmsSum /= float(MULTISCATTER_DIR_SAMPLES);

    // Multi-scattering: L_ms = L_2nd / (1 - f_ms)
    float3 multiScatter = luminanceSum / max(float3(1, 1, 1) - fmsSum, float3(1e-7, 1e-7, 1e-7));

    g_MultiScatterLUT[DTid.xy] = float4(multiScatter, 1.0);
}

// ─── Sky-View LUT (192x108) ─────────────────────────────────────────────
// Final sky radiance for a given camera position and view direction.
// Parameterized by view azimuth and zenith angle.

static const uint SKYVIEW_W = 192;
static const uint SKYVIEW_H = 108;
static const uint SKYVIEW_STEPS = 32;

Texture2D<float4> g_MultiScatterLUTRead : register(t1);

struct SkyViewConstants {
    float3 cameraPos;    // World position (km)
    float  pad;
    float3 sunDir;
    float  pad2;
};

ConstantBuffer<SkyViewConstants> skyView : register(b0);

float3 SampleMultiScatterLUT(float altitude, float sunCosZenith) {
    float2 uv;
    uv.x = sunCosZenith * 0.5 + 0.5;
    uv.y = altitude / (atm.atmosphereRadius - atm.planetRadius);
    return g_MultiScatterLUTRead.SampleLevel(g_LinearClamp, uv, 0).rgb;
}

[numthreads(8, 8, 1)]
void CSSkyViewLUT(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= SKYVIEW_W || DTid.y >= SKYVIEW_H) return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(SKYVIEW_W, SKYVIEW_H);

    // Map UV to view direction (non-linear mapping for horizon detail)
    float azimuth = (uv.x - 0.5) * 2.0 * PI;
    float adjV = uv.y;
    float zenith;
    if (adjV < 0.5) {
        float coord = 1.0 - 2.0 * adjV;
        zenith = PI * 0.5 - coord * coord * PI * 0.5;
    } else {
        float coord = 2.0 * adjV - 1.0;
        zenith = PI * 0.5 + coord * coord * PI * 0.5;
    }

    float cosZenith = cos(zenith);
    float sinZenith = sin(zenith);
    float3 viewDir = float3(sinZenith * cos(azimuth), cosZenith, sinZenith * sin(azimuth));

    float3 origin = float3(0, atm.planetRadius + max(skyView.cameraPos.y * 0.001, 0.001), 0);

    float tMax = RaySphereIntersectNearest(origin, viewDir, atm.atmosphereRadius);
    float tGround = RaySphereIntersect(origin, viewDir, float3(0, 0, 0), atm.planetRadius);

    bool hitGround = tGround > 0.0 && tGround < tMax;
    if (hitGround) tMax = tGround;

    float dt = tMax / float(SKYVIEW_STEPS);
    float3 throughput = float3(1, 1, 1);
    float3 luminance = float3(0, 0, 0);

    for (uint i = 0; i < SKYVIEW_STEPS; ++i) {
        float t = (float(i) + 0.5) * dt;
        float3 pos = origin + viewDir * t;
        float h = length(pos) - atm.planetRadius;
        h = max(h, 0.0);

        float3 extinction = GetExtinction(h);
        float3 scattering = GetScattering(h);
        float3 sampleTransmittance = exp(-extinction * dt);

        // Sun transmittance
        float sunCosZ = dot(normalize(pos), skyView.sunDir);
        float3 sunTransmittance = SampleTransmittanceLUT(h, sunCosZ);

        // Phase functions
        float cosTheta = dot(viewDir, skyView.sunDir);
        float rayleighP = RayleighPhase(cosTheta);
        float mieP = HenyeyGreensteinPhase(cosTheta, atm.miePhaseG);

        float rayleighDens = RayleighDensity(h);
        float mieDens = MieDensity(h);

        float3 singleScatter = (atm.rayleighScattering * rayleighDens * rayleighP +
                                 atm.mieScattering * mieDens * mieP) *
                                sunTransmittance * atm.sunIntensity;

        // Multi-scattering contribution
        float3 multiScatter = SampleMultiScatterLUT(h, sunCosZ) * scattering;

        float3 scatteringIntegral = ((singleScatter + multiScatter) -
                                      (singleScatter + multiScatter) * sampleTransmittance) /
                                     max(extinction, float3(1e-7, 1e-7, 1e-7));

        luminance += throughput * scatteringIntegral;
        throughput *= sampleTransmittance;
    }

    // Ground contribution
    if (hitGround) {
        float3 groundPos = origin + viewDir * tMax;
        float groundSunCosZ = dot(normalize(groundPos), skyView.sunDir);
        float3 groundSunTransmittance = SampleTransmittanceLUT(0.0, groundSunCosZ);
        luminance += throughput * atm.groundAlbedo * max(groundSunCosZ, 0.0) *
                     groundSunTransmittance * atm.sunIntensity / PI;
    }

    g_SkyViewLUT[DTid.xy] = float4(luminance, 1.0);
}
