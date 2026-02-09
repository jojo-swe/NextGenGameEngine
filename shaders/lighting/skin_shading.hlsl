// ─── Skin/Subsurface Scattering Shading Model ───────────────────────────
// Physically-based human skin rendering with:
//   - Pre-integrated subsurface scattering lookup (Penner & Borshukov)
//   - Transmittance for thin features (ears, nostrils, fingers)
//   - Dual-lobe specular (oil layer + dermal layer)
//   - Curvature-based SSS modulation
//   - Normal map detail blending (macro + micro)
//
// References:
//   - "Pre-Integrated Skin Shading" (Penner & Borshukov, GPU Pro 2)
//   - "Separable Subsurface Scattering" (Jimenez et al., 2015)
//   - "Next-Gen Character Rendering" (Activision, GDC 2014)
//   - "Digital Humans" (Epic, SIGGRAPH 2018)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_AlbedoTex       : register(t0); // Skin diffuse color
Texture2D<float4> g_NormalTex       : register(t1); // Detail normal map
Texture2D<float4> g_MacroNormalTex  : register(t2); // Low-freq normal (pores, wrinkles)
Texture2D<float>  g_RoughnessTex   : register(t3); // Specular roughness
Texture2D<float>  g_ThicknessTex   : register(t4); // Thickness for transmittance (0=thin, 1=thick)
Texture2D<float>  g_CurvatureTex   : register(t5); // Surface curvature for SSS width
Texture2D<float>  g_AOTex          : register(t6); // Ambient occlusion
Texture2D<float3> g_SSSLookup      : register(t7); // Pre-integrated SSS profile LUT

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap  : register(s1);

struct SkinConstants {
    float4x4 world;
    float4x4 viewProj;
    float4x4 worldInvTranspose;
    float3   cameraPos;
    float    sssWidth;              // SSS scatter radius (default 0.012)
    float3   lightDir;
    float    lightIntensity;
    float3   lightColor;
    float    transmittanceScale;    // Thin-feature transmittance (default 0.5)
    float3   transmittanceColor;    // Sub-dermal color (default: warm red)
    float    specularIntensity;     // Overall specular strength (default 1.0)
    float    oilLayerRoughness;     // Top specular lobe roughness (default 0.3)
    float    dermalLayerRoughness;  // Bottom specular lobe roughness (default 0.6)
    float    oilLayerF0;            // Fresnel F0 for oil (default 0.028)
    float    dermalLayerWeight;     // Dermal specular weight (default 0.3)
    float    normalDetailStrength;  // Micro-normal blend (default 0.5)
    float    macroNormalStrength;   // Macro-normal blend (default 1.0)
    float    curvatureMultiplier;   // SSS curvature modulation (default 1.0)
    float    shadowPenumbraWidth;   // Shadow SSS penumbra (default 0.5)
    float2   resolution;
    float2   pad;
};

[[vk::push_constant]] ConstantBuffer<SkinConstants> cb;

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

// ─── Pre-Integrated SSS Diffuse ──────────────────────────────────────────
// Looks up the pre-integrated SSS profile based on NdotL and curvature.
// The LUT encodes how light scatters through skin at different angles
// and surface curvatures.

float3 PreIntegratedSSS(float NdotL, float curvature, float3 albedo) {
    // LUT UV: x = NdotL remapped to [0,1], y = curvature
    float u = NdotL * 0.5 + 0.5;
    float v = saturate(curvature * cb.curvatureMultiplier);

    float3 sssProfile = g_SSSLookup.SampleLevel(g_LinearClamp, float2(u, v), 0);

    return albedo * sssProfile;
}

// ─── Transmittance (Back-Lighting) ───────────────────────────────────────
// For thin skin features: light passing through tissue.
// Uses thickness map to modulate how much light transmits.

float3 SkinTransmittance(float3 N, float3 L, float3 V, float thickness, float3 albedo) {
    // Light from behind the surface
    float3 negL = -L;
    float backNdotL = saturate(dot(N, negL));

    // Thickness modulation: thin = more transmission
    float transmit = exp(-thickness * 5.0) * cb.transmittanceScale;

    // View-dependent: more visible when looking toward the light
    float VdotNegL = saturate(dot(V, negL));
    float viewMod = pow(VdotNegL, 2.0);

    // Sub-dermal color absorption
    float3 transmitted = cb.transmittanceColor * albedo * transmit * (backNdotL + viewMod * 0.5);

    return transmitted;
}

// ─── Dual-Lobe Specular ──────────────────────────────────────────────────
// Oil layer (top): sharp, narrow highlight from surface oil
// Dermal layer (bottom): broader, colored highlight from dermal scatter

float GGX_D(float NdotH, float roughness) {
    float a2 = roughness * roughness;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + 0.0001);
}

float Smith_G(float NdotV, float NdotL, float roughness) {
    float k = roughness * roughness * 0.5;
    float gV = NdotV / (NdotV * (1.0 - k) + k);
    float gL = NdotL / (NdotL * (1.0 - k) + k);
    return gV * gL;
}

float Fresnel_Schlick(float cosTheta, float f0) {
    return f0 + (1.0 - f0) * pow(saturate(1.0 - cosTheta), 5.0);
}

struct DualLobeResult {
    float3 oilSpecular;
    float3 dermalSpecular;
};

DualLobeResult DualLobeSpecular(float3 N, float3 V, float3 L, float3 H,
                                  float NdotL, float NdotV, float NdotH, float VdotH) {
    DualLobeResult result;

    // Oil layer (top lobe): sharp, achromatic
    float D1 = GGX_D(NdotH, cb.oilLayerRoughness);
    float G1 = Smith_G(NdotV, NdotL, cb.oilLayerRoughness);
    float F1 = Fresnel_Schlick(VdotH, cb.oilLayerF0);
    result.oilSpecular = float3(1, 1, 1) * (D1 * G1 * F1) / (4.0 * NdotV * NdotL + 0.001);

    // Dermal layer (bottom lobe): broader, tinted by subsurface
    float D2 = GGX_D(NdotH, cb.dermalLayerRoughness);
    float G2 = Smith_G(NdotV, NdotL, cb.dermalLayerRoughness);
    float F2 = Fresnel_Schlick(VdotH, 0.04); // Lower F0 for dermal
    float3 dermalTint = float3(0.9, 0.7, 0.6); // Warm subsurface tint
    result.dermalSpecular = dermalTint * (D2 * G2 * F2) / (4.0 * NdotV * NdotL + 0.001);
    result.dermalSpecular *= cb.dermalLayerWeight;

    return result;
}

// ─── Normal Blending ─────────────────────────────────────────────────────
// Blend macro (low-freq) and detail (high-freq) normals using
// reoriented normal mapping (RNM) for correct detail preservation.

float3 BlendNormalsRNM(float3 base, float3 detail) {
    float3 t = base + float3(0, 0, 1);
    float3 u = detail * float3(-1, -1, 1);
    return normalize(t * dot(t, u) - u * t.z);
}

// ─── Shadow Penumbra SSS ─────────────────────────────────────────────────
// Approximates subsurface scattering in shadow penumbra regions.
// Softens the shadow transition with a warm-colored bleeding.

float3 ShadowPenumbraSSS(float NdotL, float3 albedo) {
    // Penumbra region: NdotL near 0
    float penumbra = saturate(1.0 - abs(NdotL * 2.0));
    penumbra = pow(penumbra, 2.0) * cb.shadowPenumbraWidth;

    // Warm bleed color
    float3 bleedColor = albedo * float3(1.0, 0.4, 0.25);
    return bleedColor * penumbra;
}

// ─── Fragment Shader ─────────────────────────────────────────────────────

struct PSOutput {
    float4 albedo   : SV_TARGET0; // GBuffer0: albedo + subsurface flag
    float4 normal   : SV_TARGET1; // GBuffer1: normal + roughness
    float4 emissive : SV_TARGET2; // GBuffer2: direct lighting
};

PSOutput PSMain(VSOutput input) {
    PSOutput output;

    float2 uv = input.texcoord;

    // Sample textures
    float3 albedo = g_AlbedoTex.Sample(g_LinearWrap, uv).rgb;
    float3 detailNormal = g_NormalTex.Sample(g_LinearWrap, uv).rgb * 2.0 - 1.0;
    float3 macroNormal = g_MacroNormalTex.Sample(g_LinearWrap, uv).rgb * 2.0 - 1.0;
    float roughness = g_RoughnessTex.Sample(g_LinearWrap, uv);
    float thickness = g_ThicknessTex.Sample(g_LinearWrap, uv);
    float curvature = g_CurvatureTex.Sample(g_LinearWrap, uv);
    float ao = g_AOTex.Sample(g_LinearWrap, uv);

    // Blend normals: macro (pores/wrinkles) + detail (fine grain)
    float3 blendedLocal = BlendNormalsRNM(
        lerp(float3(0, 0, 1), macroNormal, cb.macroNormalStrength),
        lerp(float3(0, 0, 1), detailNormal, cb.normalDetailStrength)
    );

    // Transform to world space
    float3x3 TBN = float3x3(
        normalize(input.worldTangent),
        normalize(input.worldBitangent),
        normalize(input.worldNormal)
    );
    float3 N = normalize(mul(blendedLocal, TBN));

    // Use macro normal for diffuse/SSS (less noisy)
    float3 macroWorld = normalize(mul(
        lerp(float3(0, 0, 1), macroNormal, cb.macroNormalStrength), TBN));

    float3 V = normalize(cb.cameraPos - input.worldPos);
    float3 L = normalize(-cb.lightDir);
    float3 H = normalize(V + L);

    float NdotL = dot(macroWorld, L); // Not clamped — SSS uses negative values
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    // ── Pre-Integrated SSS Diffuse ───────────────────────────────────
    float3 sssDiffuse = PreIntegratedSSS(NdotL, curvature, albedo);

    // ── Transmittance ────────────────────────────────────────────────
    float3 transmittance = SkinTransmittance(macroWorld, L, V, thickness, albedo);

    // ── Dual-Lobe Specular ───────────────────────────────────────────
    float clampedNdotL = saturate(NdotL);
    DualLobeResult spec = DualLobeSpecular(N, V, L, H, clampedNdotL, NdotV, NdotH, VdotH);
    float3 totalSpecular = (spec.oilSpecular + spec.dermalSpecular) *
                            cb.specularIntensity * clampedNdotL;

    // ── Shadow Penumbra SSS ──────────────────────────────────────────
    float3 penumbraSSS = ShadowPenumbraSSS(NdotL, albedo);

    // ── Combine ──────────────────────────────────────────────────────
    float3 directLighting = (sssDiffuse + transmittance + totalSpecular + penumbraSSS) *
                             cb.lightColor * cb.lightIntensity * ao;

    // Ambient
    float3 ambient = albedo * 0.03 * ao;

    // ── GBuffer Output ───────────────────────────────────────────────
    output.albedo = float4(albedo, 1.0); // A=1 flags subsurface material
    output.normal = float4(N * 0.5 + 0.5, roughness);
    output.emissive = float4(directLighting + ambient, thickness);

    return output;
}

// ─── Pre-Integrated SSS LUT Generator ────────────────────────────────────
// Bakes the 2D lookup texture: x=NdotL, y=1/curvatureRadius
// Based on Penner's diffusion profile integration.

RWTexture2D<float4> g_SSSLUTOutput : register(u0);

[numthreads(8, 8, 1)]
void CSGenerateSSSLUT(uint3 DTid : SV_DispatchThreadID) {
    float2 uv = (float2(DTid.xy) + 0.5) / 256.0; // 256x256 LUT

    float NdotL = uv.x * 2.0 - 1.0; // [-1, 1]
    float oneOverR = uv.y * 2.0;     // Curvature: 0 = flat, 2 = very curved

    // Integrate the diffusion profile over a ring at distance proportional to curvature
    float3 totalScatter = float3(0, 0, 0);
    const int SAMPLE_COUNT = 32;

    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        float sampleAngle = (float(i) + 0.5) / float(SAMPLE_COUNT) * PI;
        float sampleNdotL = NdotL + sampleAngle * oneOverR;

        float diffuse = saturate(sampleNdotL);

        // Gaussian diffusion profile per channel (R scatters most, B least)
        float dist = sampleAngle * oneOverR;
        float3 profile = float3(
            exp(-dist * dist / (2.0 * 0.0484)),  // R: wide scatter
            exp(-dist * dist / (2.0 * 0.0144)),  // G: medium
            exp(-dist * dist / (2.0 * 0.0064))   // B: narrow
        );

        totalScatter += diffuse * profile;
    }

    totalScatter /= float(SAMPLE_COUNT);

    // Normalize to maintain energy
    totalScatter = max(totalScatter, 0.001);

    g_SSSLUTOutput[DTid.xy] = float4(totalScatter, 1.0);
}
