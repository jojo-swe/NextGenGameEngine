// ─── Hair/Fur Shading Model ──────────────────────────────────────────────
// Physically-based hair shading using the Marschner model with dual
// specular lobes (R and TRT) and a diffuse transmission term (TT).
// Supports strand-space tangent frames and Kajiya-Kay fallback.
//
// Features:
//   - Marschner R (primary specular), TT (transmission), TRT (secondary)
//   - Longitudinal shift per lobe
//   - Azimuthal roughness for realistic highlight shape
//   - Strand-space tangent from geometry or texture
//   - Absorption-based color (melanin model)
//   - Shadow-receiving with deep opacity maps
//   - Kajiya-Kay simplified path for distant LOD
//
// References:
//   - "Light Scattering from Human Hair Fibers" (Marschner et al., SIGGRAPH 2003)
//   - "Physically-Based Hair Shading in Unreal Engine" (Karis, SIGGRAPH 2016)
//   - "The Filament Hair Shading Model" (Google, 2019)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_HairAlbedo       : register(t0); // Base color / melanin
Texture2D<float4> g_HairNormal       : register(t1); // Strand tangent map
Texture2D<float>  g_HairAO           : register(t2); // Per-strand AO
Texture2D<float>  g_HairAlpha        : register(t3); // Coverage / alpha
Texture2D<float>  g_DeepOpacityMap   : register(t4); // Shadow depth layers

SamplerState g_LinearWrap  : register(s0);
SamplerState g_LinearClamp : register(s1);

struct HairConstants {
    float4x4 world;
    float4x4 viewProj;
    float4x4 worldInvTranspose;
    float4x4 lightViewProj;      // For deep opacity shadow lookup
    float3   cameraPos;
    float    primaryShift;        // Longitudinal shift R lobe (default -5 deg)
    float3   lightDir;
    float    secondaryShift;      // Longitudinal shift TRT lobe (default -10 deg)
    float3   lightColor;
    float    lightIntensity;
    float3   hairColor;           // Base absorption color
    float    roughnessR;          // Primary specular roughness (default 0.1)
    float    roughnessTT;         // Transmission roughness (default 0.15)
    float    roughnessTRT;        // Secondary specular roughness (default 0.2)
    float    melanin;             // 0=blonde, 0.5=brown, 1.0=black
    float    melaninRedness;      // Pheomelanin ratio (0=eumelanin, 1=pheomelanin)
    float    scatterAmount;       // Diffuse wrap/scatter (default 0.3)
    float    shadowDensity;       // Deep opacity density (default 0.5)
    uint     useMelaninColor;     // 1: derive color from melanin, 0: use hairColor
    uint     useKajiyaKay;        // 1: simplified model for LOD
    float2   resolution;
};

[[vk::push_constant]] ConstantBuffer<HairConstants> cb;

// ─── Vertex Shader ───────────────────────────────────────────────────────

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 tangent  : TANGENT;    // Strand direction (along hair fiber)
    float2 texcoord : TEXCOORD0;
};

struct VSOutput {
    float4 clipPos       : SV_POSITION;
    float2 texcoord      : TEXCOORD0;
    float3 worldPos      : TEXCOORD1;
    float3 worldNormal   : TEXCOORD2;
    float3 worldTangent  : TEXCOORD3;  // Strand direction in world space
    float3 worldBitangent: TEXCOORD4;
};

VSOutput VSMain(VSInput input) {
    VSOutput output;

    float4 worldPos = mul(cb.world, float4(input.position, 1.0));
    output.clipPos = mul(cb.viewProj, worldPos);
    output.worldPos = worldPos.xyz;
    output.texcoord = input.texcoord;

    float3 N = normalize(mul((float3x3)cb.worldInvTranspose, input.normal));
    float3 T = normalize(mul((float3x3)cb.world, input.tangent.xyz));
    float3 B = cross(N, T) * input.tangent.w;

    output.worldNormal = N;
    output.worldTangent = T;
    output.worldBitangent = B;

    return output;
}

// ─── Melanin Absorption ──────────────────────────────────────────────────
// Compute hair absorption coefficient from melanin concentration.

float3 MelaninAbsorption(float melanin, float redness) {
    // Eumelanin absorption (brown/black)
    float3 eumelanin = float3(0.419, 0.697, 1.37);
    // Pheomelanin absorption (red/blonde)
    float3 pheomelanin = float3(0.187, 0.4, 1.05);

    float3 absorption = lerp(eumelanin, pheomelanin, redness) * melanin;
    return absorption;
}

float3 AbsorptionToColor(float3 absorption) {
    return exp(-absorption * 2.0); // Approximate transmission
}

// ─── Marschner Lobe Functions ────────────────────────────────────────────

// Gaussian distribution for longitudinal scattering
float GaussianD(float beta, float theta) {
    float a = 1.0 / (beta * sqrt(2.0 * PI));
    return a * exp(-theta * theta / (2.0 * beta * beta));
}

// Shifted cosine for azimuthal scattering
float CosineAzimuthal(float phi, float scale) {
    return saturate(cos(phi * 0.5) * scale);
}

// Compute the longitudinal angle (theta) from view and light vs tangent
void ComputeAngles(float3 T, float3 V, float3 L,
                    out float sinThetaI, out float cosThetaI,
                    out float sinThetaO, out float cosThetaO,
                    out float cosPhi) {
    // Project V and L onto plane perpendicular to tangent
    float3 Lperp = L - T * dot(L, T);
    float3 Vperp = V - T * dot(V, T);

    float lPerp = max(length(Lperp), 0.001);
    float vPerp = max(length(Vperp), 0.001);

    cosThetaI = lPerp;
    sinThetaI = dot(L, T);
    cosThetaO = vPerp;
    sinThetaO = dot(V, T);

    cosPhi = dot(Lperp / lPerp, Vperp / vPerp);
}

// ─── Marschner Hair BRDF ─────────────────────────────────────────────────

struct HairLobes {
    float3 R;    // Primary specular
    float3 TT;   // Transmission
    float3 TRT;  // Secondary specular
};

HairLobes MarschnerHairBRDF(float3 T, float3 V, float3 L, float3 hairColor) {
    HairLobes lobes;

    float sinThetaI, cosThetaI, sinThetaO, cosThetaO, cosPhi;
    ComputeAngles(T, V, L, sinThetaI, cosThetaI, sinThetaO, cosThetaO, cosPhi);

    // Shifted longitudinal angles per lobe
    float shiftR   = radians(cb.primaryShift);
    float shiftTRT = radians(cb.secondaryShift);

    // R lobe: primary specular (surface reflection)
    float thetaHR = (sinThetaI + sinThetaO) * 0.5 - shiftR;
    float MR = GaussianD(cb.roughnessR, thetaHR);
    float NR = CosineAzimuthal(acos(cosPhi), 1.0);
    lobes.R = float3(1, 1, 1) * MR * NR; // White specular

    // TT lobe: transmission through fiber
    float thetaHTT = (sinThetaI + sinThetaO) * 0.5;
    float MTT = GaussianD(cb.roughnessTT, thetaHTT);
    float NTT = CosineAzimuthal(acos(cosPhi) - PI, 0.5);
    float3 absorptionTT = sqrt(hairColor); // Single internal path
    lobes.TT = absorptionTT * MTT * NTT;

    // TRT lobe: secondary specular (internal reflection)
    float thetaHTRT = (sinThetaI + sinThetaO) * 0.5 - shiftTRT;
    float MTRT = GaussianD(cb.roughnessTRT, thetaHTRT);
    float NTRT = CosineAzimuthal(acos(cosPhi), 0.8);
    float3 absorptionTRT = hairColor; // Double internal path
    lobes.TRT = absorptionTRT * MTRT * NTRT;

    return lobes;
}

// ─── Kajiya-Kay Simplified Model ─────────────────────────────────────────

float3 KajiyaKay(float3 T, float3 V, float3 L, float3 hairColor) {
    float TdotL = dot(T, L);
    float TdotV = dot(T, V);

    float sinTL = sqrt(max(0.0, 1.0 - TdotL * TdotL));
    float sinTV = sqrt(max(0.0, 1.0 - TdotV * TdotV));

    // Diffuse term
    float3 diffuse = hairColor * saturate(sinTL);

    // Specular term
    float specular = pow(saturate(TdotL * TdotV + sinTL * sinTV), 20.0);

    return diffuse * 0.7 + float3(1, 1, 1) * specular * 0.3;
}

// ─── Deep Opacity Shadow ─────────────────────────────────────────────────

float DeepOpacityShadow(float3 worldPos) {
    float4 lightClip = mul(cb.lightViewProj, float4(worldPos, 1.0));
    float2 lightUV = lightClip.xy / lightClip.w * 0.5 + 0.5;
    lightUV.y = 1.0 - lightUV.y;

    if (any(lightUV < 0.0) || any(lightUV > 1.0)) return 1.0;

    float shadowDepth = g_DeepOpacityMap.SampleLevel(g_LinearClamp, lightUV, 0);
    float currentDepth = lightClip.z / lightClip.w;

    float depthDiff = currentDepth - shadowDepth;
    if (depthDiff <= 0.001) return 1.0;

    // Exponential falloff based on density
    return exp(-depthDiff * cb.shadowDensity * 100.0);
}

// ─── Fragment Shader ─────────────────────────────────────────────────────

struct PSOutput {
    float4 color : SV_TARGET0;
};

PSOutput PSMain(VSOutput input) {
    PSOutput output;

    float2 uv = input.texcoord;

    // Alpha test
    float alpha = g_HairAlpha.Sample(g_LinearWrap, uv);
    if (alpha < 0.1) discard;

    // Hair color
    float3 baseColor;
    if (cb.useMelaninColor) {
        float3 absorption = MelaninAbsorption(cb.melanin, cb.melaninRedness);
        baseColor = AbsorptionToColor(absorption);
    } else {
        baseColor = cb.hairColor * g_HairAlbedo.Sample(g_LinearWrap, uv).rgb;
    }

    // Strand tangent
    float3 T = normalize(input.worldTangent);
    float3 tangentMap = g_HairNormal.Sample(g_LinearWrap, uv).rgb;
    if (any(tangentMap > 0.01)) {
        float3 localT = tangentMap * 2.0 - 1.0;
        float3x3 TBN = float3x3(input.worldTangent, input.worldBitangent, input.worldNormal);
        T = normalize(mul(localT, TBN));
    }

    float3 V = normalize(cb.cameraPos - input.worldPos);
    float3 L = normalize(-cb.lightDir);

    float ao = g_HairAO.Sample(g_LinearWrap, uv);

    // Lighting
    float3 lighting;

    if (cb.useKajiyaKay) {
        lighting = KajiyaKay(T, V, L, baseColor);
    } else {
        HairLobes lobes = MarschnerHairBRDF(T, V, L, baseColor);

        // Diffuse scatter (wrapped diffuse along strand)
        float NdotL = dot(input.worldNormal, L);
        float diffuseWrap = saturate(NdotL * 0.5 + 0.5);
        float3 diffuse = baseColor * diffuseWrap * cb.scatterAmount;

        lighting = lobes.R + lobes.TT + lobes.TRT + diffuse;
    }

    // Shadow
    float shadow = DeepOpacityShadow(input.worldPos);

    float3 finalColor = lighting * cb.lightColor * cb.lightIntensity * shadow * ao;

    // Simple ambient
    float3 ambient = baseColor * 0.05 * ao;
    finalColor += ambient;

    output.color = float4(finalColor, alpha);
    return output;
}

// ─── Depth Pre-Pass ──────────────────────────────────────────────────────

void PSDepthPrepass(VSOutput input) {
    float alpha = g_HairAlpha.Sample(g_LinearWrap, input.texcoord);
    if (alpha < 0.5) discard;
}
