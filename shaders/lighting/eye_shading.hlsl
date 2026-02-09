// ─── Eye/Iris Shading Model ──────────────────────────────────────────────
// Physically-based eye rendering with:
//   - Cornea refraction (Snell's law) for parallax iris depth
//   - Iris caustic brightening from light focus
//   - Subsurface scattering in sclera (white of eye)
//   - Limbal darkening ring at iris/sclera boundary
//   - Pupil dilation based on light exposure
//   - Specular highlight on cornea (wet glossy surface)
//
// References:
//   - "Realistic Eyes in Real-Time" (Jimenez, GPU Pro 5)
//   - "Digital Emily: Achieving a Photorealistic Digital Actor" (Debevec, 2008)
//   - "A Survey on Human Eye Rendering" (Berard et al., 2019)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_IrisTexture     : register(t0); // Iris color + pattern
Texture2D<float4> g_ScleraTexture   : register(t1); // Sclera color + veins
Texture2D<float4> g_IrisNormal      : register(t2); // Iris micro-normal (fibers)
Texture2D<float>  g_IrisDepth       : register(t3); // Iris depth for refraction
Texture2D<float>  g_EyeAO           : register(t4); // Eye socket AO

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

struct EyeConstants {
    float4x4 world;
    float4x4 viewProj;
    float4x4 worldInvTranspose;
    float3   cameraPos;
    float    corneaIOR;          // Index of refraction (default 1.336)
    float3   lightDir;
    float    lightIntensity;
    float3   lightColor;
    float    irisDepth;          // Depth behind cornea (default 0.3)
    float3   scleraSSColor;      // Sclera subsurface color (default: warm pink)
    float    pupilScale;         // 0=fully dilated, 1=fully constricted
    float    pupilMinRadius;     // Min pupil radius (default 0.1)
    float    pupilMaxRadius;     // Max pupil radius (default 0.5)
    float    limbalRingWidth;    // Limbal ring width (default 0.05)
    float    limbalRingPower;    // Limbal darkening exponent (default 4.0)
    float    corneaSmoothness;   // Cornea specular (default 0.98)
    float    causticIntensity;   // Iris caustic strength (default 0.5)
    float    scleraSSStrength;   // Subsurface scatter strength (default 0.3)
    float    irisRadius;         // UV-space iris radius (default 0.35)
    float2   irisCenterUV;       // Iris center in UV space (default 0.5, 0.5)
    float2   pad;
};

[[vk::push_constant]] ConstantBuffer<EyeConstants> cb;

// ─── Vertex Shader ───────────────────────────────────────────────────────

struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 tangent  : TANGENT;
    float2 texcoord : TEXCOORD0;
};

struct VSOutput {
    float4 clipPos       : SV_POSITION;
    float2 texcoord      : TEXCOORD0;
    float3 worldPos      : TEXCOORD1;
    float3 worldNormal   : TEXCOORD2;
    float3 worldTangent  : TEXCOORD3;
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

// ─── Cornea Refraction ───────────────────────────────────────────────────
// Offset iris UV based on view direction refracted through cornea.

float2 RefractionOffset(float3 viewDirTS, float ior, float depth) {
    // Snell's law: sin(theta_t) = sin(theta_i) / ior
    float cosI = abs(viewDirTS.z);
    float sinI2 = 1.0 - cosI * cosI;
    float sinT2 = sinI2 / (ior * ior);

    // Total internal reflection check
    if (sinT2 >= 1.0) return float2(0, 0);

    float cosT = sqrt(1.0 - sinT2);

    // Refracted direction in tangent space
    float3 refracted = float3(viewDirTS.xy / max(ior, 0.001), -cosT);
    refracted = normalize(refracted);

    // UV offset proportional to iris depth
    float2 offset = refracted.xy * depth / max(abs(refracted.z), 0.001);

    return offset;
}

// ─── Pupil Dilation ──────────────────────────────────────────────────────

float PupilMask(float2 uv, float pupilRadius) {
    float dist = length(uv - cb.irisCenterUV);
    return smoothstep(pupilRadius + 0.01, pupilRadius - 0.01, dist);
}

float CurrentPupilRadius() {
    return lerp(cb.pupilMaxRadius, cb.pupilMinRadius, cb.pupilScale);
}

// ─── Limbal Ring ─────────────────────────────────────────────────────────
// Dark ring at the iris/sclera boundary.

float LimbalDarkening(float2 uv) {
    float dist = length(uv - cb.irisCenterUV);
    float limbalStart = cb.irisRadius - cb.limbalRingWidth;
    float limbalEnd = cb.irisRadius;

    float ring = smoothstep(limbalStart, limbalEnd, dist);
    return pow(ring, cb.limbalRingPower);
}

// ─── Iris Caustic ────────────────────────────────────────────────────────
// Light focused by cornea creates bright ring on iris.

float IrisCaustic(float3 N, float3 L, float3 V) {
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));

    // Caustic is strongest when light enters at shallow angle
    float caustic = pow(saturate(1.0 - NdotL), 3.0) * NdotV;
    return caustic * cb.causticIntensity;
}

// ─── Sclera Subsurface Scattering ────────────────────────────────────────

float3 ScleraSSS(float3 N, float3 L, float3 scleraColor) {
    // Wrap lighting for subsurface appearance
    float NdotL = dot(N, L);
    float sss = saturate(NdotL * 0.5 + 0.5);
    sss = pow(sss, 1.5);

    // Back-lighting term (light through thin tissue)
    float backLight = saturate(-NdotL * 0.3 + 0.1);

    return scleraColor * cb.scleraSSColor * (sss + backLight) * cb.scleraSSStrength;
}

// ─── GGX Specular for Cornea ─────────────────────────────────────────────

float GGXDistribution(float NdotH, float roughness) {
    float a2 = roughness * roughness;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 0.0001);
}

float SmithG(float NdotV, float NdotL, float roughness) {
    float k = roughness * roughness * 0.5;
    float gV = NdotV / (NdotV * (1.0 - k) + k);
    float gL = NdotL / (NdotL * (1.0 - k) + k);
    return gV * gL;
}

float3 CorneaSpecular(float3 N, float3 V, float3 L) {
    float3 H = normalize(V + L);
    float NdotH = saturate(dot(N, H));
    float NdotV = saturate(dot(N, V));
    float NdotL = saturate(dot(N, L));

    float roughness = 1.0 - cb.corneaSmoothness;
    roughness = max(roughness, 0.01);

    float D = GGXDistribution(NdotH, roughness);
    float G = SmithG(NdotV, NdotL, roughness);

    // Fresnel for cornea (IOR-based F0)
    float f0 = pow((cb.corneaIOR - 1.0) / (cb.corneaIOR + 1.0), 2.0);
    float F = f0 + (1.0 - f0) * pow(1.0 - saturate(dot(V, H)), 5.0);

    return float3(1, 1, 1) * (D * G * F) / (4.0 * NdotV * NdotL + 0.001);
}

// ─── Fragment Shader ─────────────────────────────────────────────────────

struct PSOutput {
    float4 albedo   : SV_TARGET0; // GBuffer0
    float4 normal   : SV_TARGET1; // GBuffer1
    float4 emissive : SV_TARGET2; // GBuffer2
};

PSOutput PSMain(VSOutput input) {
    PSOutput output;

    float2 uv = input.texcoord;
    float3 N = normalize(input.worldNormal);
    float3 T = normalize(input.worldTangent);
    float3 B = normalize(input.worldBitangent);
    float3 V = normalize(cb.cameraPos - input.worldPos);
    float3 L = normalize(-cb.lightDir);

    // Build TBN for tangent-space operations
    float3x3 TBN_inv = float3x3(T, B, N);
    float3 viewDirTS = mul(TBN_inv, V);

    // Determine if we're on iris or sclera
    float distFromCenter = length(uv - cb.irisCenterUV);
    float irisMask = 1.0 - smoothstep(cb.irisRadius - 0.02, cb.irisRadius + 0.02, distFromCenter);

    // ── Iris ─────────────────────────────────────────────────────────
    float2 refractionOff = RefractionOffset(viewDirTS, cb.corneaIOR, cb.irisDepth);
    float2 irisUV = uv + refractionOff * irisMask;

    // Iris depth modulation
    float irisDepthSample = g_IrisDepth.SampleLevel(g_LinearClamp, irisUV, 0);
    irisUV += refractionOff * irisDepthSample * 0.5;

    float4 irisColor = g_IrisTexture.Sample(g_LinearClamp, irisUV);

    // Pupil
    float pupilRadius = CurrentPupilRadius();
    float pupil = PupilMask(irisUV, pupilRadius);
    irisColor.rgb *= (1.0 - pupil * 0.95); // Darken pupil area

    // Iris micro-normal
    float3 irisNormal = g_IrisNormal.Sample(g_LinearClamp, irisUV).rgb * 2.0 - 1.0;
    float3 irisWorldNormal = normalize(mul(irisNormal, float3x3(T, B, N)));

    // Iris caustic
    float caustic = IrisCaustic(N, L, V) * irisMask;
    irisColor.rgb += irisColor.rgb * caustic;

    // Limbal darkening
    float limbal = LimbalDarkening(uv);
    irisColor.rgb *= (1.0 - limbal * 0.6);

    // ── Sclera ───────────────────────────────────────────────────────
    float4 scleraColor = g_ScleraTexture.Sample(g_LinearClamp, uv);
    float3 scleraSSS = ScleraSSS(N, L, scleraColor.rgb);

    // ── Blend iris + sclera ──────────────────────────────────────────
    float3 baseColor = lerp(scleraColor.rgb, irisColor.rgb, irisMask);
    float3 blendedNormal = lerp(N, irisWorldNormal, irisMask * 0.5);
    blendedNormal = normalize(blendedNormal);

    // Add sclera subsurface
    baseColor += scleraSSS * (1.0 - irisMask);

    // ── Cornea specular (entire eye) ─────────────────────────────────
    float3 specular = CorneaSpecular(N, V, L);

    // ── AO ───────────────────────────────────────────────────────────
    float ao = g_EyeAO.Sample(g_LinearClamp, uv);

    // ── Output ───────────────────────────────────────────────────────
    float3 directLighting = baseColor * saturate(dot(N, L)) * cb.lightColor * cb.lightIntensity;
    directLighting += specular * cb.lightColor * cb.lightIntensity;
    directLighting *= ao;

    float3 ambient = baseColor * 0.03 * ao;

    output.albedo = float4(baseColor, 0.0);
    output.normal = float4(blendedNormal * 0.5 + 0.5, 1.0 - cb.corneaSmoothness);
    output.emissive = float4(directLighting + ambient, 1.0);

    return output;
}
