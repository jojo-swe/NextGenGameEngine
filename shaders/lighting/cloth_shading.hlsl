// ─── Cloth/Fabric Shading Model ──────────────────────────────────────────
// Physically-based fabric rendering with:
//   - Charlie sheen BRDF for velvet/microfiber highlights
//   - Anisotropic specular for woven fabrics (silk, satin)
//   - Fuzz layer for peach-fuzz and suede
//   - Subsurface transmission for thin fabrics
//   - Thread-level detail via weave pattern normal
//
// Supports three fabric presets:
//   1. Velvet/microfiber — strong sheen at grazing angles
//   2. Silk/satin — anisotropic highlight along weave direction
//   3. Cotton/linen — rough diffuse with subtle fuzz
//
// References:
//   - "Physically-Based Shading at Disney" (Burley, SIGGRAPH 2012)
//   - "A Practical Extension to Microfacet Theory" (Estevez & Kulla, 2017)
//   - "Rendering Cloth in Unreal Engine" (Epic, GDC 2017)
//   - "The Filament Material Model" (Google, 2018)

#include "../common/math.hlsl"

// ─── Resources ───────────────────────────────────────────────────────────

Texture2D<float4> g_AlbedoTex     : register(t0); // Fabric base color
Texture2D<float4> g_NormalTex     : register(t1); // Surface normal map
Texture2D<float4> g_WeaveNormal   : register(t2); // Thread-level weave pattern
Texture2D<float>  g_RoughnessTex  : register(t3);
Texture2D<float>  g_AO            : register(t4);
Texture2D<float4> g_SheenColorTex : register(t5); // Per-texel sheen tint

SamplerState g_LinearWrap  : register(s0);
SamplerState g_LinearClamp : register(s1);

struct ClothConstants {
    float4x4 world;
    float4x4 viewProj;
    float4x4 worldInvTranspose;
    float3   cameraPos;
    float    roughness;            // Base roughness (default 0.5)
    float3   lightDir;
    float    lightIntensity;
    float3   lightColor;
    float    sheenIntensity;       // Sheen strength (default 0.5)
    float3   sheenColor;           // Sheen tint (default: white)
    float    sheenRoughness;       // Charlie distribution roughness (default 0.5)
    float3   subsurfaceColor;      // Transmission color (default: fabric color * 0.5)
    float    subsurfaceStrength;   // Thin-fabric transmission (default 0.0)
    float3   fuzzColor;            // Fuzz tint (default: lighter fabric color)
    float    fuzzStrength;         // Fuzz intensity (default 0.0)
    float2   anisotropy;           // (strength, angle) for silk; 0=isotropic
    float    weaveScale;           // Weave pattern UV scale (default 1.0)
    uint     fabricPreset;         // 0=custom, 1=velvet, 2=silk, 3=cotton
};

[[vk::push_constant]] ConstantBuffer<ClothConstants> cb;

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

// ─── Charlie Sheen Distribution ──────────────────────────────────────────
// Estevez & Kulla 2017: inverted Gaussian for fabric sheen.

float CharlieD(float roughness, float NdotH) {
    float a = roughness * roughness;
    float invA = 1.0 / a;
    float cos2H = NdotH * NdotH;
    float sin2H = 1.0 - cos2H;

    // Inverted Gaussian distribution
    return (2.0 + invA) * pow(sin2H, invA * 0.5) / (2.0 * PI);
}

// Visibility term for Charlie (Ashikhmin)
float CharlieV(float NdotV, float NdotL, float roughness) {
    float a = roughness * roughness;
    float lambdaV = NdotV < 0.5 ? exp(CharlieL(NdotV, a)) : exp(2.0 * CharlieL(0.5, a) - CharlieL(1.0 - NdotV, a));
    float lambdaL = NdotL < 0.5 ? exp(CharlieL(NdotL, a)) : exp(2.0 * CharlieL(0.5, a) - CharlieL(1.0 - NdotL, a));

    // Simplified: use Neubelt visibility
    return saturate(1.0 / (4.0 * (NdotL + NdotV - NdotL * NdotV)));
}

float CharlieL(float x, float r) {
    // Approximation of the lambda function
    float oneMinusR = 1.0 - r;
    float a = lerp(25.3245, 21.5473, oneMinusR);
    float b = lerp(3.32435, 3.82987, oneMinusR);
    float c = lerp(0.16801, 0.19823, oneMinusR);
    float d = lerp(-1.27393, -1.97760, oneMinusR);
    float e = lerp(-4.85967, -4.32054, oneMinusR);
    return a / (1.0 + b * pow(x, c)) + d * x + e;
}

// ─── Anisotropic GGX for Silk ────────────────────────────────────────────

float AnisotropicGGX(float NdotH, float TdotH, float BdotH, float ax, float ay) {
    float d = TdotH * TdotH / (ax * ax) + BdotH * BdotH / (ay * ay) + NdotH * NdotH;
    return 1.0 / (PI * ax * ay * d * d);
}

// ─── Fabric Fuzz Layer ───────────────────────────────────────────────────
// Approximates peach fuzz via a wrapped diffuse term.

float3 FuzzLayer(float3 N, float3 L, float3 fuzzColor, float strength) {
    float NdotL = dot(N, L);
    float wrap = saturate(NdotL * 0.5 + 0.5);
    float fuzz = pow(1.0 - saturate(NdotL), 4.0) * strength;

    return fuzzColor * (wrap * 0.3 + fuzz);
}

// ─── Subsurface Transmission ─────────────────────────────────────────────
// For thin fabrics (curtains, flags): light passing through.

float3 ThinTransmission(float3 N, float3 L, float3 V, float3 transColor, float strength) {
    // Transmission direction: light from behind
    float transmit = saturate(dot(-N, L)) * strength;

    // View-dependent thinning
    float VdotL = saturate(dot(V, -L));
    transmit *= pow(VdotL, 2.0);

    return transColor * transmit;
}

// ─── Fragment Shader ─────────────────────────────────────────────────────

struct PSOutput {
    float4 albedo   : SV_TARGET0; // GBuffer0: albedo + metallic
    float4 normal   : SV_TARGET1; // GBuffer1: normal + roughness
    float4 emissive : SV_TARGET2; // GBuffer2: emissive + sheen
};

PSOutput PSMain(VSOutput input) {
    PSOutput output;

    float2 uv = input.texcoord;

    // Sample textures
    float3 albedo = g_AlbedoTex.Sample(g_LinearWrap, uv).rgb;
    float3 normalMap = g_NormalTex.Sample(g_LinearWrap, uv).rgb * 2.0 - 1.0;
    float3 weaveNormal = g_WeaveNormal.Sample(g_LinearWrap, uv * cb.weaveScale).rgb * 2.0 - 1.0;
    float roughness = g_RoughnessTex.Sample(g_LinearWrap, uv) * cb.roughness;
    float ao = g_AO.Sample(g_LinearWrap, uv);

    // Sheen color: from texture or constant
    float3 sheenColor = cb.sheenColor;
    float4 sheenTex = g_SheenColorTex.Sample(g_LinearWrap, uv);
    if (any(sheenTex.rgb > 0.01)) sheenColor *= sheenTex.rgb;

    // Combine surface normal + weave detail
    float3 combinedNormal = normalize(normalMap + weaveNormal * 0.3);

    // Transform to world space
    float3x3 TBN = float3x3(
        normalize(input.worldTangent),
        normalize(input.worldBitangent),
        normalize(input.worldNormal)
    );
    float3 N = normalize(mul(combinedNormal, TBN));
    float3 T = normalize(input.worldTangent);
    float3 B = normalize(input.worldBitangent);
    float3 V = normalize(cb.cameraPos - input.worldPos);
    float3 L = normalize(-cb.lightDir);
    float3 H = normalize(V + L);

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float TdotH = dot(T, H);
    float BdotH = dot(B, H);

    // ── Apply preset overrides ───────────────────────────────────────
    float sheenInt = cb.sheenIntensity;
    float sheenRough = cb.sheenRoughness;
    float fuzzStr = cb.fuzzStrength;
    float subsurfStr = cb.subsurfaceStrength;
    float2 aniso = cb.anisotropy;

    if (cb.fabricPreset == 1) { // Velvet
        sheenInt = 0.8; sheenRough = 0.6; fuzzStr = 0.0; aniso = float2(0, 0);
    } else if (cb.fabricPreset == 2) { // Silk
        sheenInt = 0.3; sheenRough = 0.3; aniso = float2(0.8, 0); fuzzStr = 0.0;
    } else if (cb.fabricPreset == 3) { // Cotton
        sheenInt = 0.15; sheenRough = 0.8; fuzzStr = 0.3; aniso = float2(0, 0);
    }

    // ── Diffuse ──────────────────────────────────────────────────────
    float3 diffuse = albedo * NdotL / PI;

    // ── Sheen (Charlie) ──────────────────────────────────────────────
    float D = CharlieD(sheenRough, NdotH);
    float Vis = CharlieV(NdotV, NdotL, sheenRough);
    float3 sheen = sheenColor * D * Vis * sheenInt * NdotL;

    // ── Anisotropic specular (silk) ──────────────────────────────────
    float3 anisoSpec = float3(0, 0, 0);
    if (aniso.x > 0.001) {
        // Rotate anisotropy direction
        float angle = aniso.y;
        float3 anisoT = T * cos(angle) + B * sin(angle);
        float3 anisoB = -T * sin(angle) + B * cos(angle);

        float ax = roughness * (1.0 + aniso.x);
        float ay = roughness * (1.0 - aniso.x * 0.5);

        float anisoD = AnisotropicGGX(NdotH, dot(anisoT, H), dot(anisoB, H), ax, ay);
        anisoSpec = float3(1, 1, 1) * anisoD * NdotL * 0.1;
    }

    // ── Fuzz layer ───────────────────────────────────────────────────
    float3 fuzz = FuzzLayer(N, L, cb.fuzzColor, fuzzStr);

    // ── Subsurface transmission ──────────────────────────────────────
    float3 transmission = ThinTransmission(N, L, V, cb.subsurfaceColor, subsurfStr);

    // ── Combine ──────────────────────────────────────────────────────
    float3 lighting = (diffuse + sheen + anisoSpec + fuzz + transmission) *
                       cb.lightColor * cb.lightIntensity * ao;

    // Ambient
    float3 ambient = albedo * 0.03 * ao;

    // ── GBuffer output ───────────────────────────────────────────────
    output.albedo = float4(albedo, 0.0); // Non-metallic
    output.normal = float4(N * 0.5 + 0.5, roughness);
    output.emissive = float4(lighting + ambient, sheenInt); // Store sheen in alpha

    return output;
}
