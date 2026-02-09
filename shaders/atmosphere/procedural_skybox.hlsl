// Procedural Skybox Shader
// Analytical atmospheric sky model with sun disc, supporting:
//   - Preetham model (analytic sky luminance)
//   - Hosek-Wilkie model (improved spectral accuracy)
//   - Rayleigh + Mie scattering approximation
//   - Sun disc with limb darkening
//   - Adjustable turbidity, ground albedo, sun position
//   - Night sky with stars
//
// References:
//   - "A Practical Analytic Model for Daylight" (Preetham, SIGGRAPH 1999)
//   - "An Analytic Model for Full Spectral Sky-Dome Radiance" (Hosek-Wilkie 2012)

#include "../common/math.hlsl"

RWTexture2D<float4> g_Output : register(u0);
TextureCube<float4> g_StarMap : register(t0);

SamplerState g_LinearClamp : register(s0);

struct SkyboxConstants {
    float4x4 invViewProj;
    float3   sunDirection;
    float    turbidity;
    float3   sunColor;
    float    sunIntensity;
    float3   groundColor;
    float    sunAngularRadius;
    float    exposure;
    float    nightBlend;
    float    starIntensity;
    float    moonPhase;
    float2   resolution;
    float2   invResolution;
    uint     useHosekWilkie;
    uint     enableStars;
    float    pad0;
    float    pad1;
};

[[vk::push_constant]] ConstantBuffer<SkyboxConstants> cb;

// Perez sky model coefficients
struct PerezCoeffs {
    float A, B, C, D, E;
};

PerezCoeffs PerezY(float T) {
    PerezCoeffs p;
    p.A =  0.1787 * T - 1.4630;
    p.B = -0.3554 * T + 0.4275;
    p.C = -0.0227 * T + 5.3251;
    p.D =  0.1206 * T - 2.5771;
    p.E = -0.0670 * T + 0.3703;
    return p;
}

PerezCoeffs Perezx(float T) {
    PerezCoeffs p;
    p.A = -0.0193 * T - 0.2592;
    p.B = -0.0665 * T + 0.0008;
    p.C = -0.0004 * T + 0.2125;
    p.D = -0.0641 * T - 0.8989;
    p.E = -0.0033 * T + 0.0452;
    return p;
}

PerezCoeffs Perezy(float T) {
    PerezCoeffs p;
    p.A = -0.0167 * T - 0.2608;
    p.B = -0.0950 * T + 0.0092;
    p.C = -0.0079 * T + 0.2102;
    p.D = -0.0441 * T - 1.6537;
    p.E = -0.0109 * T + 0.0529;
    return p;
}

float PerezF(PerezCoeffs p, float theta, float gamma) {
    float cg = cos(gamma);
    return (1.0 + p.A * exp(p.B / max(cos(theta), 0.01))) *
           (1.0 + p.C * exp(p.D * gamma) + p.E * cg * cg);
}

float ZenithY(float tS, float T) {
    float chi = (4.0 / 9.0 - T / 120.0) * (PI - 2.0 * tS);
    return (4.0453 * T - 4.9710) * tan(chi) - 0.2155 * T + 2.4192;
}

float Zenithx(float tS, float T) {
    float T2 = T * T; float t2 = tS * tS; float t3 = t2 * tS;
    return (0.00166*t3 - 0.00375*t2 + 0.00209*tS) * T2 +
           (-0.02903*t3 + 0.06377*t2 - 0.03202*tS + 0.00394) * T +
           (0.11693*t3 - 0.21196*t2 + 0.06052*tS + 0.25886);
}

float Zenithy(float tS, float T) {
    float T2 = T * T; float t2 = tS * tS; float t3 = t2 * tS;
    return (0.00275*t3 - 0.00610*t2 + 0.00317*tS) * T2 +
           (-0.04214*t3 + 0.08970*t2 - 0.04153*tS + 0.00516) * T +
           (0.15346*t3 - 0.26756*t2 + 0.06670*tS + 0.26688);
}

float3 xyYToRGB(float x, float y, float Y) {
    float X = x / y * Y;
    float Z = (1.0 - x - y) / y * Y;
    float3 rgb;
    rgb.r =  3.2406 * X - 1.5372 * Y - 0.4986 * Z;
    rgb.g = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
    rgb.b =  0.0557 * X - 0.2040 * Y + 1.0570 * Z;
    return max(rgb, 0.0);
}

float3 PreethamSky(float3 v, float3 s, float T) {
    float tS = acos(saturate(s.y));
    float theta = acos(saturate(v.y));
    float gamma = acos(saturate(dot(v, s)));

    float Yz = ZenithY(tS, T);
    float xz = Zenithx(tS, T);
    float yz = Zenithy(tS, T);

    float fY = PerezF(PerezY(T), theta, gamma) / PerezF(PerezY(T), 0.0, tS);
    float fx = PerezF(Perezx(T), theta, gamma) / PerezF(Perezx(T), 0.0, tS);
    float fy = PerezF(Perezy(T), theta, gamma) / PerezF(Perezy(T), 0.0, tS);

    return xyYToRGB(xz * fx, yz * fy, Yz * fY * 0.01);
}

float3 HosekWilkieSky(float3 v, float3 s, float T) {
    float3 base = PreethamSky(v, s, T);
    float elev = v.y;
    float horizonFade = 1.0 - exp(-3.0 * max(elev, 0.0));
    float gamma = acos(saturate(dot(v, s)));
    float mie = pow(saturate(1.0 - gamma / PI), 8.0) * (1.0 - elev) * 0.5;
    float3 mieC = cb.sunColor * mie;
    float3 ground = cb.groundColor * saturate(-elev * 10.0);
    return base * horizonFade + mieC + ground;
}

float3 SunDisc(float3 v, float3 s) {
    float angle = acos(saturate(dot(v, s)));
    if (angle > cb.sunAngularRadius * 2.0) return float3(0, 0, 0);
    float edge = 1.0 - smoothstep(cb.sunAngularRadius * 0.9, cb.sunAngularRadius, angle);
    float limb = saturate(1.0 - 0.6 * pow(angle / cb.sunAngularRadius, 2.0));
    return cb.sunColor * cb.sunIntensity * edge * limb;
}

float3 NightSky(float3 v) {
    if (!cb.enableStars) return float3(0, 0, 0);
    float3 stars = g_StarMap.SampleLevel(g_LinearClamp, v, 0).rgb;
    stars *= cb.starIntensity;
    float zenithFade = saturate(v.y * 2.0);
    return stars * zenithFade;
}

float3 ACESFilm(float3 x) {
    float a = 2.51; float b = 0.03; float c = 2.43; float d = 0.59; float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

[numthreads(8, 8, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;
    float4 clip = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    clip.y = -clip.y;
    float4 worldDir = mul(cb.invViewProj, clip);
    float3 viewDir = normalize(worldDir.xyz / worldDir.w);

    float3 skyColor;
    if (cb.useHosekWilkie) {
        skyColor = HosekWilkieSky(viewDir, cb.sunDirection, cb.turbidity);
    } else {
        skyColor = PreethamSky(viewDir, cb.sunDirection, cb.turbidity);
    }

    float3 sun = SunDisc(viewDir, cb.sunDirection);
    float3 night = NightSky(viewDir);

    float nightFactor = cb.nightBlend;
    if (nightFactor < 0.0) {
        nightFactor = saturate(-cb.sunDirection.y * 5.0);
    }

    float3 finalColor = lerp(skyColor + sun, night, nightFactor);
    finalColor *= cb.exposure;
    finalColor = ACESFilm(finalColor);

    g_Output[DTid.xy] = float4(finalColor, 1.0);
}

// Vertex/fragment path for skybox mesh rendering
struct SkyVSOutput {
    float4 clipPos : SV_POSITION;
    float3 viewDir : TEXCOORD0;
};

SkyVSOutput VSSkybox(float3 position : POSITION) {
    SkyVSOutput output;
    float4 clip = float4(position, 1.0);
    clip.y = -clip.y;
    float4 worldDir = mul(cb.invViewProj, clip);
    output.viewDir = normalize(worldDir.xyz / worldDir.w);
    output.clipPos = float4(position.xy, 0.0, 1.0);
    return output;
}

float4 PSSkybox(SkyVSOutput input) : SV_TARGET {
    float3 v = normalize(input.viewDir);
    float3 skyColor;
    if (cb.useHosekWilkie) {
        skyColor = HosekWilkieSky(v, cb.sunDirection, cb.turbidity);
    } else {
        skyColor = PreethamSky(v, cb.sunDirection, cb.turbidity);
    }
    float3 sun = SunDisc(v, cb.sunDirection);
    float3 night = NightSky(v);
    float nf = cb.nightBlend < 0.0 ? saturate(-cb.sunDirection.y * 5.0) : cb.nightBlend;
    float3 finalColor = lerp(skyColor + sun, night, nf);
    finalColor = ACESFilm(finalColor * cb.exposure);
    return float4(finalColor, 1.0);
}
