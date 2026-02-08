// ─── Deferred Lighting Resolve (Clustered + Tiled Hybrid) ────────────────
// Final lighting pass that reads GBuffer data and evaluates all lights
// using a hybrid clustered/tiled approach:
//   - Clustered: 3D frustum-aligned grid for point/spot lights
//   - Tiled: 2D screen tiles for area lights and directional cascades
//
// Outputs HDR radiance for the post-processing chain.
//
// References:
//   - "Clustered Deferred and Forward Shading" (Olsson et al., HPG 2012)
//   - "Tiled Shading" (Olsson & Assarsson, JCG 2011)

#include "../common/math.hlsl"
#include "../common/brdf.hlsl"

// ─── GBuffer inputs ──────────────────────────────────────────────────────

Texture2D<float4> g_GBuffer0    : register(t0); // RGB: Albedo, A: metallic
Texture2D<float4> g_GBuffer1    : register(t1); // RGB: World normal (oct encoded), A: roughness
Texture2D<float4> g_GBuffer2    : register(t2); // RGB: Emissive, A: AO
Texture2D<float>  g_DepthBuffer : register(t3);

// ─── Light data ──────────────────────────────────────────────────────────

struct PointLight {
    float3 position;
    float  radius;
    float3 color;
    float  intensity;
};

struct SpotLight {
    float3 position;
    float  radius;
    float3 direction;
    float  innerAngle;
    float3 color;
    float  outerAngle;
    float  intensity;
    float3 pad;
};

struct DirectionalLight {
    float3 direction;
    float  intensity;
    float3 color;
    float  shadowBias;
};

StructuredBuffer<PointLight>       g_PointLights      : register(t4);
StructuredBuffer<SpotLight>        g_SpotLights       : register(t5);
StructuredBuffer<DirectionalLight> g_DirLights        : register(t6);

// ─── Cluster / Tile data ─────────────────────────────────────────────────

// Clustered: 3D grid of light lists
// Grid: 16x8xZ_SLICES (Z sliced logarithmically in view space)
struct ClusterData {
    uint offset;   // Into g_LightIndices
    uint count;    // Number of lights in this cluster
};

StructuredBuffer<ClusterData> g_Clusters      : register(t7);
StructuredBuffer<uint>        g_LightIndices  : register(t8);

// Tiled: 2D grid for area lights / directional shadow cascades
struct TileData {
    uint areaLightOffset;
    uint areaLightCount;
    uint cascadeMask;   // Bitfield of active shadow cascades
    uint pad;
};

StructuredBuffer<TileData> g_Tiles : register(t9);

// ─── Shadow maps ─────────────────────────────────────────────────────────

Texture2DArray<float> g_ShadowCascades : register(t10); // CSM array
TextureCubeArray<float> g_PointShadows : register(t11); // Point light cubemaps
Texture2D<float>      g_SpotShadows   : register(t12);  // Spot light atlas

SamplerComparisonState g_ShadowSampler : register(s0);
SamplerState g_PointClamp              : register(s1);
SamplerState g_LinearClamp             : register(s2);

// ─── Indirect lighting ───────────────────────────────────────────────────

Texture2D<float4> g_SSAO      : register(t13);
Texture2D<float4> g_SSR       : register(t14);
Texture2D<float4> g_SSGI      : register(t15);
TextureCube<float4> g_EnvMap  : register(t16);
TextureCube<float4> g_IrrMap  : register(t17); // Diffuse irradiance
Texture2D<float2>   g_BRDFLUT : register(t18); // Split-sum LUT

// ─── Output ──────────────────────────────────────────────────────────────

RWTexture2D<float4> g_Output : register(u0);

// ─── Constants ───────────────────────────────────────────────────────────

struct DeferredResolveConstants {
    float4x4 invViewProj;
    float4x4 view;
    float4x4 viewProj;
    float4x4 shadowCascadeVP[4];
    float4   cascadeSplits;       // View-space Z splits
    float3   cameraPos;
    float    near;
    float    far;
    float2   resolution;
    float2   invResolution;
    uint     pointLightCount;
    uint     spotLightCount;
    uint     dirLightCount;
    float    envMapIntensity;
    float    ssgiIntensity;
    float    ssaoIntensity;
    uint     clusterDimX;
    uint     clusterDimY;
    uint     clusterDimZ;
    float    clusterZNear;
    float    clusterZFar;
    float    clusterLogScale;     // For logarithmic Z slicing
    uint     tileDimX;
    uint     tileDimY;
    float    pad0;
};

[[vk::push_constant]] ConstantBuffer<DeferredResolveConstants> cb;

// ─── Utility Functions ───────────────────────────────────────────────────

float3 ReconstructWorldPos(float2 uv, float depth) {
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;
    float4 worldPos = mul(cb.invViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}

float LinearizeDepth(float d) {
    return cb.near * cb.far / (cb.far - d * (cb.far - cb.near));
}

float3 DecodeOctNormal(float2 encoded) {
    float2 f = encoded * 2.0 - 1.0;
    float3 n = float3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    if (n.z < 0.0) {
        n.xy = (1.0 - abs(n.yx)) * sign(n.xy);
    }
    return normalize(n);
}

// ─── Cluster Lookup ──────────────────────────────────────────────────────

uint GetClusterIndex(float2 screenPos, float linearZ) {
    uint clusterX = uint(screenPos.x / (cb.resolution.x / float(cb.clusterDimX)));
    uint clusterY = uint(screenPos.y / (cb.resolution.y / float(cb.clusterDimY)));

    // Logarithmic Z slicing
    float logZ = log2(linearZ / cb.clusterZNear) * cb.clusterLogScale;
    uint clusterZ = uint(clamp(logZ, 0.0, float(cb.clusterDimZ - 1)));

    return clusterX + clusterY * cb.clusterDimX + clusterZ * cb.clusterDimX * cb.clusterDimY;
}

uint GetTileIndex(float2 screenPos) {
    uint tileX = uint(screenPos.x / (cb.resolution.x / float(cb.tileDimX)));
    uint tileY = uint(screenPos.y / (cb.resolution.y / float(cb.tileDimY)));
    return tileX + tileY * cb.tileDimX;
}

// ─── Shadow Evaluation ───────────────────────────────────────────────────

float EvaluateCascadeShadow(float3 worldPos, float linearZ) {
    // Select cascade based on view-space depth
    uint cascade = 3;
    if (linearZ < cb.cascadeSplits.x) cascade = 0;
    else if (linearZ < cb.cascadeSplits.y) cascade = 1;
    else if (linearZ < cb.cascadeSplits.z) cascade = 2;

    float4 shadowCoord = mul(cb.shadowCascadeVP[cascade], float4(worldPos, 1.0));
    shadowCoord.xyz /= shadowCoord.w;
    float2 shadowUV = shadowCoord.xy * 0.5 + 0.5;
    shadowUV.y = 1.0 - shadowUV.y;

    if (any(shadowUV < 0.0) || any(shadowUV > 1.0)) return 1.0;

    // PCF 3x3
    float shadow = 0.0;
    float texelSize = 1.0 / 2048.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float2 offset = float2(x, y) * texelSize;
            shadow += g_ShadowCascades.SampleCmpLevelZero(
                g_ShadowSampler, float3(shadowUV + offset, cascade), shadowCoord.z);
        }
    }
    return shadow / 9.0;
}

float DistanceAttenuation(float distance, float radius) {
    float d2 = distance * distance;
    float r2 = radius * radius;
    float att = saturate(1.0 - d2 / r2);
    return att * att;
}

float SpotAttenuation(float3 lightDir, float3 spotDir, float innerAngle, float outerAngle) {
    float cosAngle = dot(-lightDir, spotDir);
    float cosInner = cos(innerAngle);
    float cosOuter = cos(outerAngle);
    return saturate((cosAngle - cosOuter) / max(cosInner - cosOuter, 0.001));
}

// ─── PBR Lighting ────────────────────────────────────────────────────────

float3 EvaluatePointLight(PointLight light, float3 worldPos, float3 N, float3 V,
                           float3 albedo, float metallic, float roughness) {
    float3 L = light.position - worldPos;
    float dist = length(L);
    if (dist > light.radius) return float3(0, 0, 0);
    L /= dist;

    float atten = DistanceAttenuation(dist, light.radius) * light.intensity;
    float3 radiance = light.color * atten;

    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);

    float3 specular = (D * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 0.001);
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * NdotL;
}

float3 EvaluateSpotLight(SpotLight light, float3 worldPos, float3 N, float3 V,
                          float3 albedo, float metallic, float roughness) {
    float3 L = light.position - worldPos;
    float dist = length(L);
    if (dist > light.radius) return float3(0, 0, 0);
    L /= dist;

    float distAtt = DistanceAttenuation(dist, light.radius);
    float spotAtt = SpotAttenuation(L, light.direction, light.innerAngle, light.outerAngle);
    float atten = distAtt * spotAtt * light.intensity;
    if (atten < 0.001) return float3(0, 0, 0);

    float3 radiance = light.color * atten;

    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);

    float3 specular = (D * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 0.001);
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * NdotL;
}

float3 EvaluateDirectionalLight(DirectionalLight light, float3 N, float3 V,
                                  float3 albedo, float metallic, float roughness, float shadow) {
    float3 L = -light.direction;
    float3 radiance = light.color * light.intensity * shadow;

    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float D = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);

    float3 specular = (D * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 0.001);
    float3 kD = (1.0 - F) * (1.0 - metallic);
    float3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * NdotL;
}

// ─── Image-Based Lighting ────────────────────────────────────────────────

float3 EvaluateIBL(float3 N, float3 V, float3 albedo, float metallic, float roughness, float ao) {
    float NdotV = max(dot(N, V), 0.0);
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);

    // Diffuse IBL
    float3 irradiance = g_IrrMap.SampleLevel(g_LinearClamp, N, 0).rgb;
    float3 kD = (1.0 - FresnelSchlickRoughness(NdotV, F0, roughness)) * (1.0 - metallic);
    float3 diffuseIBL = kD * albedo * irradiance;

    // Specular IBL (split-sum)
    float3 R = reflect(-V, N);
    float mipLevel = roughness * 7.0; // Assume 8 mip levels
    float3 prefilteredColor = g_EnvMap.SampleLevel(g_LinearClamp, R, mipLevel).rgb;
    float2 brdfLUT = g_BRDFLUT.SampleLevel(g_LinearClamp, float2(NdotV, roughness), 0).rg;
    float3 specularIBL = prefilteredColor * (F0 * brdfLUT.x + brdfLUT.y);

    return (diffuseIBL + specularIBL) * ao * cb.envMapIntensity;
}

// ─── Main Resolve ────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void CSResolve(uint3 DTid : SV_DispatchThreadID) {
    if (any(DTid.xy >= uint2(cb.resolution))) return;

    float2 uv = (float2(DTid.xy) + 0.5) * cb.invResolution;

    // Read GBuffer
    float4 gb0 = g_GBuffer0.SampleLevel(g_PointClamp, uv, 0);
    float4 gb1 = g_GBuffer1.SampleLevel(g_PointClamp, uv, 0);
    float4 gb2 = g_GBuffer2.SampleLevel(g_PointClamp, uv, 0);
    float  depth = g_DepthBuffer.SampleLevel(g_PointClamp, uv, 0);

    // Skip sky
    if (depth >= 1.0) {
        g_Output[DTid.xy] = float4(0, 0, 0, 1);
        return;
    }

    float3 albedo    = gb0.rgb;
    float  metallic  = gb0.a;
    float3 worldNorm = DecodeOctNormal(gb1.rg);
    float  roughness = gb1.a;
    float3 emissive  = gb2.rgb;
    float  ao        = gb2.a;

    float3 worldPos = ReconstructWorldPos(uv, depth);
    float  linearZ  = LinearizeDepth(depth);
    float3 V = normalize(cb.cameraPos - worldPos);

    // SSAO modulates ambient
    float ssao = g_SSAO.SampleLevel(g_PointClamp, uv, 0).r;
    ao *= lerp(1.0, ssao, cb.ssaoIntensity);

    float3 totalLighting = float3(0, 0, 0);

    // ── Directional lights + CSM shadows ─────────────────────────────
    for (uint d = 0; d < cb.dirLightCount; ++d) {
        float shadow = EvaluateCascadeShadow(worldPos, linearZ);
        totalLighting += EvaluateDirectionalLight(g_DirLights[d], worldNorm, V,
                                                   albedo, metallic, roughness, shadow);
    }

    // ── Clustered point + spot lights ────────────────────────────────
    uint clusterIdx = GetClusterIndex(float2(DTid.xy), linearZ);
    ClusterData cluster = g_Clusters[clusterIdx];

    for (uint i = 0; i < cluster.count; ++i) {
        uint lightIdx = g_LightIndices[cluster.offset + i];

        if (lightIdx < cb.pointLightCount) {
            totalLighting += EvaluatePointLight(g_PointLights[lightIdx], worldPos,
                                                 worldNorm, V, albedo, metallic, roughness);
        } else {
            uint spotIdx = lightIdx - cb.pointLightCount;
            if (spotIdx < cb.spotLightCount) {
                totalLighting += EvaluateSpotLight(g_SpotLights[spotIdx], worldPos,
                                                    worldNorm, V, albedo, metallic, roughness);
            }
        }
    }

    // ── Image-based lighting (ambient) ───────────────────────────────
    totalLighting += EvaluateIBL(worldNorm, V, albedo, metallic, roughness, ao);

    // ── Screen-space reflections ─────────────────────────────────────
    float4 ssrData = g_SSR.SampleLevel(g_PointClamp, uv, 0);
    if (ssrData.a > 0.0) {
        float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
        float NdotV = max(dot(worldNorm, V), 0.0);
        float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
        totalLighting += ssrData.rgb * F * ssrData.a;
    }

    // ── Screen-space global illumination ─────────────────────────────
    float3 ssgi = g_SSGI.SampleLevel(g_PointClamp, uv, 0).rgb;
    totalLighting += ssgi * cb.ssgiIntensity;

    // ── Emissive ─────────────────────────────────────────────────────
    totalLighting += emissive;

    g_Output[DTid.xy] = float4(totalLighting, 1.0);
}
