// ─── PBR BRDF Functions (Disney/GGX) ─────────────────────────────────────

#ifndef NGE_BRDF_HLSL
#define NGE_BRDF_HLSL

#include "math.hlsl"

// ─── Fresnel ─────────────────────────────────────────────────────────────

float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness) {
    float3 a = max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0);
    return F0 + (a - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// ─── GGX Normal Distribution ─────────────────────────────────────────────

float DistributionGGX(float NdotH, float roughness) {
    float a  = roughness * roughness;
    float a2 = a * a;
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// ─── Smith Geometry (GGX correlated) ─────────────────────────────────────

float GeometrySmithGGXCorrelated(float NdotV, float NdotL, float roughness) {
    float a2 = roughness * roughness * roughness * roughness;
    float GGXV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float GGXL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / max(GGXV + GGXL, EPSILON);
}

// ─── Cook-Torrance Specular BRDF ─────────────────────────────────────────

float3 SpecularBRDF(float3 N, float3 V, float3 L, float3 F0, float roughness) {
    float3 H = normalize(V + L);

    float NdotH = max(dot(N, H), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotL = max(dot(N, L), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmithGGXCorrelated(NdotV, NdotL, roughness);
    float3 F = FresnelSchlick(VdotH, F0);

    return D * G * F;
}

// ─── Lambertian Diffuse BRDF ─────────────────────────────────────────────

float3 DiffuseBRDF(float3 albedo) {
    return albedo * INV_PI;
}

// ─── Full PBR BRDF Evaluation ────────────────────────────────────────────

struct BRDFInput {
    float3 N;          // Surface normal
    float3 V;          // View direction
    float3 L;          // Light direction
    float3 albedo;     // Base color
    float  metallic;   // Metallic factor
    float  roughness;  // Roughness factor
};

float3 EvaluatePBR(BRDFInput input) {
    float NdotL = max(dot(input.N, input.L), 0.0);
    if (NdotL <= 0.0) return float3(0, 0, 0);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), input.albedo, input.metallic);

    float3 specular = SpecularBRDF(input.N, input.V, input.L, F0, input.roughness);

    float3 H = normalize(input.V + input.L);
    float VdotH = max(dot(input.V, H), 0.0);
    float3 F = FresnelSchlick(VdotH, F0);

    float3 kD = (1.0 - F) * (1.0 - input.metallic);
    float3 diffuse = kD * DiffuseBRDF(input.albedo);

    return (diffuse + specular) * NdotL;
}

// ─── Importance Sampling GGX ─────────────────────────────────────────────
// For path tracing and IBL

float3 ImportanceSampleGGX(float2 xi, float3 N, float roughness) {
    float a = roughness * roughness;

    float phi = TWO_PI * xi.x;
    float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    float3x3 TBN = BuildTBN(N);
    return normalize(mul(H, TBN));
}

// GGX sampling PDF
float ImportanceSampleGGX_PDF(float NdotH, float VdotH, float roughness) {
    float D = DistributionGGX(NdotH, roughness);
    return (D * NdotH) / (4.0 * VdotH + EPSILON);
}

#endif // NGE_BRDF_HLSL
