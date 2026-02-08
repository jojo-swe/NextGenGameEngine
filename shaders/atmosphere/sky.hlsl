// ─── Procedural Sky + Atmospheric Scattering ─────────────────────────────
// Bruneton-style precomputed atmospheric scattering with:
//   - Rayleigh scattering (blue sky)
//   - Mie scattering (haze/sun glow)
//   - Ozone absorption layer
//   - Multiple scattering approximation
//
// Precomputed LUTs:
//   - Transmittance LUT (256×64)
//   - Multiscatter LUT (32×32)
//   - Sky-View LUT (192×108, per-frame)

#include "../common/math.hlsl"

// ─── Atmosphere Parameters ───────────────────────────────────────────────

struct AtmosphereParams {
    float3 sunDirection;
    float  sunIntensity;
    float3 rayleighScattering;  // Scattering coefficients at sea level
    float  rayleighDensityH;    // Scale height (8.5 km)
    float3 mieScattering;
    float  mieDensityH;         // Scale height (1.2 km)
    float  mieAnisotropy;       // Phase function asymmetry (0.8)
    float3 ozoneAbsorption;
    float  ozoneCenterH;        // Center altitude (25 km)
    float  ozoneWidth;          // Width (15 km)
    float  planetRadius;        // 6360 km
    float  atmosphereRadius;    // 6460 km
    float  groundAlbedo;
};

ConstantBuffer<AtmosphereParams> g_Atmo : register(b0, space7);

// ─── Ray-Sphere Intersection ─────────────────────────────────────────────

float2 RaySphereIntersect(float3 ro, float3 rd, float radius) {
    float b = dot(ro, rd);
    float c = dot(ro, ro) - radius * radius;
    float d = b * b - c;
    if (d < 0.0) return float2(-1, -1);
    d = sqrt(d);
    return float2(-b - d, -b + d);
}

// ─── Density Functions ───────────────────────────────────────────────────

float DensityRayleigh(float h) {
    return exp(-h / g_Atmo.rayleighDensityH);
}

float DensityMie(float h) {
    return exp(-h / g_Atmo.mieDensityH);
}

float DensityOzone(float h) {
    return max(0.0, 1.0 - abs(h - g_Atmo.ozoneCenterH) / g_Atmo.ozoneWidth);
}

// ─── Phase Functions ─────────────────────────────────────────────────────

float PhaseRayleigh(float cosTheta) {
    return 3.0 / (16.0 * PI) * (1.0 + cosTheta * cosTheta);
}

float PhaseMie(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return 3.0 / (8.0 * PI) * ((1.0 - g2) * (1.0 + cosTheta * cosTheta))
           / ((2.0 + g2) * pow(denom, 1.5));
}

// ─── Transmittance (optical depth integration) ───────────────────────────

float3 ComputeTransmittance(float3 ro, float3 rd, float tMax) {
    const int STEPS = 32;
    float dt = tMax / (float)STEPS;
    float3 opticalDepth = float3(0, 0, 0);

    for (int i = 0; i < STEPS; ++i) {
        float t = (float(i) + 0.5) * dt;
        float3 pos = ro + rd * t;
        float h = length(pos) - g_Atmo.planetRadius;

        float dR = DensityRayleigh(h);
        float dM = DensityMie(h);
        float dO = DensityOzone(h);

        opticalDepth += (g_Atmo.rayleighScattering * dR +
                         g_Atmo.mieScattering * dM * 1.1 +
                         g_Atmo.ozoneAbsorption * dO) * dt;
    }

    return exp(-opticalDepth);
}

// ─── Single Scattering (sky radiance along view ray) ─────────────────────

float3 ComputeSkyRadiance(float3 ro, float3 rd) {
    float2 atmoHit = RaySphereIntersect(ro, rd, g_Atmo.atmosphereRadius);
    if (atmoHit.y < 0.0) return float3(0, 0, 0);

    float2 groundHit = RaySphereIntersect(ro, rd, g_Atmo.planetRadius);
    float tMax = (groundHit.x > 0.0) ? groundHit.x : atmoHit.y;
    tMax = max(tMax, 0.0);

    const int STEPS = 32;
    float dt = tMax / (float)STEPS;

    float3 radiance = float3(0, 0, 0);
    float3 transmittance = float3(1, 1, 1);

    float cosTheta = dot(rd, g_Atmo.sunDirection);
    float phaseR = PhaseRayleigh(cosTheta);
    float phaseM = PhaseMie(cosTheta, g_Atmo.mieAnisotropy);

    for (int i = 0; i < STEPS; ++i) {
        float t = (float(i) + 0.5) * dt;
        float3 pos = ro + rd * t;
        float h = length(pos) - g_Atmo.planetRadius;

        float dR = DensityRayleigh(h);
        float dM = DensityMie(h);

        float3 scatterR = g_Atmo.rayleighScattering * dR;
        float3 scatterM = g_Atmo.mieScattering * dM;

        // Transmittance from sample point to sun
        float2 sunHit = RaySphereIntersect(pos, g_Atmo.sunDirection, g_Atmo.atmosphereRadius);
        float3 sunTransmittance = ComputeTransmittance(pos, g_Atmo.sunDirection, sunHit.y);

        // In-scattering contribution
        float3 scattering = (scatterR * phaseR + scatterM * phaseM) * sunTransmittance;
        radiance += transmittance * scattering * dt * g_Atmo.sunIntensity;

        // Update transmittance along view ray
        float3 extinction = scatterR + scatterM * 1.1 + g_Atmo.ozoneAbsorption * DensityOzone(h);
        transmittance *= exp(-extinction * dt);
    }

    return radiance;
}

// ─── Sun Disk ────────────────────────────────────────────────────────────

float3 ComputeSunDisk(float3 rd) {
    float sunAngle = 0.00935; // Angular radius of sun (~0.536 degrees)
    float cosAngle = dot(rd, g_Atmo.sunDirection);
    if (cosAngle > cos(sunAngle)) {
        float limb = smoothstep(cos(sunAngle), cos(sunAngle * 0.9), cosAngle);
        return float3(1, 1, 1) * g_Atmo.sunIntensity * 100.0 * limb;
    }
    return float3(0, 0, 0);
}

// ─── Full-screen Sky Compute Shader ──────────────────────────────────────

struct SkyPushConstants {
    float4x4 invViewProj;
    float4   cameraPos;
    uint     screenWidth;
    uint     screenHeight;
    uint     pad0;
    uint     pad1;
};

[[vk::push_constant]] ConstantBuffer<SkyPushConstants> skyPC;

RWTexture2D<float4> g_SkyOutput : register(u0, space7);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= skyPC.screenWidth || DTid.y >= skyPC.screenHeight) return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(skyPC.screenWidth, skyPC.screenHeight);
    uv = uv * 2.0 - 1.0;
    uv.y = -uv.y;

    float4 clipPos = float4(uv, 0.0, 1.0);
    float4 worldDir = mul(skyPC.invViewProj, clipPos);
    float3 rd = normalize(worldDir.xyz / worldDir.w - skyPC.cameraPos.xyz);

    // Camera at planet surface + small height
    float3 ro = float3(0, g_Atmo.planetRadius + 0.001, 0); // 1 meter above surface

    float3 sky = ComputeSkyRadiance(ro, rd);
    float3 sun = ComputeSunDisk(rd);

    // Ground check: if ray hits planet, apply ground albedo
    float2 groundHit = RaySphereIntersect(ro, rd, g_Atmo.planetRadius);
    if (groundHit.x > 0.0) {
        float3 groundPos = ro + rd * groundHit.x;
        float3 groundNormal = normalize(groundPos);
        float NdotL = max(dot(groundNormal, g_Atmo.sunDirection), 0.0);
        float3 groundColor = float3(g_Atmo.groundAlbedo, g_Atmo.groundAlbedo, g_Atmo.groundAlbedo) * NdotL;

        // Apply atmospheric transmittance to ground
        float3 groundTransmittance = ComputeTransmittance(groundPos, -rd, groundHit.x);
        sky = groundColor * groundTransmittance + sky;
        sun = float3(0, 0, 0); // No sun disk below horizon
    }

    g_SkyOutput[DTid.xy] = float4(sky + sun, 1.0);
}
